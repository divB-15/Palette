#!/usr/bin/env python3
"""Palette 界面冒烟测试（Windows）。

Palette 是全自绘窗口，没有可读文本的子控件，所以这个脚本靠两件事确认状态：

1. **截图**：PrintWindow(PW_RENDERFULLCONTENT) 抓客户区，再靠「行背景色」
   把 5 行数值框从图里定位出来 —— 不写死坐标，改布局也不会失效。
2. **剪贴板**：点行尾的「复制」按钮，程序会把该行数值写进剪贴板，
   于是脚本能拿到精确的 HEX 文本，用来判断颜色到底有没有变。

跑一遍这些交互，失败就非零退出：

1. 点数值 → 出现原生编辑框；写入 HEX 并回车 → 颜色真的变了
2. 写 RGB / HSV / HSL / CMYK → 同样生效
3. 写错格式 → 颜色不变（弹提示，不乱改）
4. 点行尾「复制」→ 剪贴板内容 = 该行数值
5. 拖方块 / 拖色相条 → 颜色跟着变
6. 点配色色块 → 应用该色
7. 缩到最小尺寸 → 布局不塌

用法：
    python tools/ui_check.py                 # 用 bin\Palette.exe
    python tools/ui_check.py path/to.exe     # 指定别的 exe
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import pathlib
import re
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "bin" / "Palette.exe"

u = ctypes.windll.user32
g = ctypes.windll.gdi32

# 本进程必须自己声明 per-monitor V2：否则 GetWindowRect 拿到的是系统虚拟化
# 过的尺寸，PrintWindow 只会截到窗口左上角一块。
try:
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:  # pragma: no cover - 老系统退路
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        ctypes.windll.user32.SetProcessDPIAware()

u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = ctypes.c_ssize_t
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.PostMessageW.restype = W.BOOL
u.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
u.GetWindowTextLengthW.argtypes = [W.HWND]
u.IsWindowVisible.argtypes = [W.HWND]
# GetClipboardData 返回的是句柄：默认的 c_int 会把 64 位指针截断
u.GetClipboardData.argtypes = [W.UINT]
u.GetClipboardData.restype = W.HANDLE
u.OpenClipboard.argtypes = [W.HWND]
u.OpenClipboard.restype = W.BOOL
u.EmptyClipboard.restype = W.BOOL

kernel = ctypes.windll.kernel32
kernel.GlobalLock.argtypes = [W.HANDLE]
kernel.GlobalLock.restype = ctypes.c_void_p
kernel.GlobalUnlock.argtypes = [W.HANDLE]
kernel.GlobalUnlock.restype = W.BOOL

WM_KEYDOWN = 0x0100
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
WM_GETTEXT = 0x000D
WM_SETTEXT = 0x000C
EM_SETSEL = 0x00B1
VK_RETURN = 0x0D
VK_ESCAPE = 0x1B
VK_TAB = 0x09

# 主题里的数值行底色（ui::Colors::row = 0xFF23262E）
ROW_BG = (0x23, 0x26, 0x2E)
# 鼠标悬停时行底色会换成这个（ui::Colors::rowHover = 0xFF2B303A）。
# 定位行时两种都要认：只认一种的话，悬停的那一行会被整段漏掉，
# 后面所有行号就整体错位一位（曾经因此把 HSL 写进了 CMYK 行）。
ROW_HOVER = (0x2B, 0x30, 0x3A)

# RGB 调节卡里三条滑条各自的标识色（app.cpp 的 ChannelColor）。
CHANNEL_COLORS = [(0xFF, 0x5A, 0x5A), (0x4C, 0xD9, 0x7A), (0x5A, 0xA9, 0xFF)]

FAILURES: list[str] = []


def check(label: str, ok: bool, detail: str = "") -> None:
    print(('  ok   ' if ok else '  FAIL ') + label + (('  -> ' + detail) if detail else ''))
    if not ok:
        FAILURES.append(label)


# --- window plumbing -------------------------------------------------------

def windows_of(pid: int):
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, W.HWND, W.LPARAM)
    def cb(h, _l):
        p = W.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            n = u.GetWindowTextLengthW(h)
            buf = ctypes.create_unicode_buffer(n + 2)
            u.GetWindowTextW(h, buf, n + 2)
            out.append((h, buf.value))
        return True

    u.EnumWindows(cb, 0)
    return out


def children_of(hwnd):
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, W.HWND, W.LPARAM)
    def cb(h, _l):
        cls = ctypes.create_unicode_buffer(64)
        u.GetClassNameW(h, cls, 64)
        out.append((h, cls.value, u.IsWindowVisible(h)))
        return True

    u.EnumChildWindows(hwnd, cb, 0)
    return out


def text_of(hwnd) -> str:
    buf = ctypes.create_unicode_buffer(256)
    u.SendMessageW(hwnd, WM_GETTEXT, 255, ctypes.cast(buf, ctypes.c_void_p).value)
    return buf.value


def set_text(hwnd, text: str) -> None:
    buf = ctypes.create_unicode_buffer(text)
    u.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.cast(buf, ctypes.c_void_p).value)


def clipboard_text() -> str:
    if not u.OpenClipboard(None):
        return ''
    try:
        handle = u.GetClipboardData(13)  # CF_UNICODETEXT
        if not handle:
            return ''
        pointer = kernel.GlobalLock(handle)
        if not pointer:
            return ''
        try:
            return ctypes.wstring_at(pointer)
        finally:
            kernel.GlobalUnlock(handle)
    finally:
        u.CloseClipboard()


def clear_clipboard() -> None:
    if u.OpenClipboard(None):
        u.EmptyClipboard()
        u.CloseClipboard()


# --- capture ---------------------------------------------------------------

def capture_client(hwnd) -> np.ndarray:
    """PrintWindow 抓整窗，再裁掉标题栏，得到与客户区 1:1 的像素。"""
    win = W.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(win))
    width = win.right - win.left
    height = win.bottom - win.top

    hdc = u.GetDC(hwnd)
    memdc = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, width, height)
    old = g.SelectObject(memdc, bmp)
    try:
        u.PrintWindow(hwnd, memdc, 2)  # PW_RENDERFULLCONTENT
        buffer = ctypes.create_string_buffer(width * height * 4)
        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [('biSize', W.DWORD), ('biWidth', ctypes.c_long),
                        ('biHeight', ctypes.c_long), ('biPlanes', W.WORD),
                        ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                        ('biSizeImage', W.DWORD), ('biXPelsPerMeter', ctypes.c_long),
                        ('biYPelsPerMeter', ctypes.c_long), ('biClrUsed', W.DWORD),
                        ('biClrImportant', W.DWORD)]
        header = BITMAPINFOHEADER()
        header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        header.biWidth = width
        header.biHeight = -height          # top-down
        header.biPlanes = 1
        header.biBitCount = 32
        header.biCompression = 0
        g.GetDIBits(memdc, bmp, 0, height, buffer, ctypes.byref(header), 0)
        image = Image.frombuffer('RGB', (width, height), buffer, 'raw', 'BGRX', 0, 1)
    finally:
        g.SelectObject(memdc, old)
        g.DeleteObject(bmp)
        g.DeleteDC(memdc)
        u.ReleaseDC(hwnd, hdc)

    origin = W.POINT(0, 0)
    u.ClientToScreen(hwnd, ctypes.byref(origin))
    offset_x = origin.x - win.left
    offset_y = origin.y - win.top
    return np.asarray(image.convert('RGB'))[offset_y:offset_y + (height - offset_y),
                                          offset_x:offset_x + (width - offset_x)]


def find_value_rows(frame: np.ndarray):
    """靠行底色找出数值行：返回每行 (左, 上, 右, 下) 的像素矩形。

    数值卡片在最上面，所以只取最靠上的 5 条（下面那条是配色方案里的芯片行，
    底色一样但不是数值行）。
    """
    height, width = frame.shape[:2]
    mask = (np.all(frame == np.array(ROW_BG, dtype=np.uint8), axis=2) |
            np.all(frame == np.array(ROW_HOVER, dtype=np.uint8), axis=2))
    # 只看右半边：数值卡片在那里
    right = mask[:, int(width * 0.45):]

    rows = []
    y = 0
    while y < height:
        if not right[y].any():
            y += 1
            continue
        start = y
        while y < height and right[y].any():
            y += 1
        band = right[start:y]
        counts = band.sum(axis=0)
        columns = np.nonzero(counts > (y - start) * 0.5)[0]
        if len(columns) == 0:
            continue
        left = int(columns[0]) + int(width * 0.45)
        right_edge = int(columns[-1]) + int(width * 0.45)
        rows.append((left, start, right_edge, y))

    # 一行数值的高度约 34 逻辑像素；把明显偏矮的碎片（抗锯齿残留）滤掉
    rows = [r for r in rows if r[3] - r[1] > 30]
    return rows[:5]


def value_text_span(frame: np.ndarray, row):
    """在行里找「数值文字」的那一段亮色像素（行标题是灰的、「复制」更暗）。

    返回 (左, 右) 像素列；找不到就返回 None。
    """
    left, top, right, bottom = row
    band = frame[top:bottom, left:right]
    bright = np.all(band > 200, axis=2)
    columns = np.nonzero(bright.any(axis=0))[0]
    if len(columns) == 0:
        return None
    return left + int(columns[0]), left + int(columns[-1])


# --- RGB 滑条 ---------------------------------------------------------------

def find_slider_track(frame: np.ndarray, color):
    """在截图里定位一条 RGB 滑条，返回 (左端 x, 右端 x, 中线 y)。

    依据：
    * 填充部分用的是通道标识色，它的**左端就是轨道左端**；
    * 同一行上剩下的那段「行底色」连续像素是还没填满的轨道，**右端即轨道右端**。

    靠颜色找，不写死坐标 —— 布局改了也不会失效。
    """
    delta = np.abs(frame.astype(np.int16) - np.array(color, dtype=np.int16)).sum(axis=2)
    mask = delta < 40

    # 只保留横向连续 >= 40 像素的行，免得把左边那个单字标签当成滑条。
    run = 40
    cumulative = np.cumsum(mask, axis=1, dtype=np.int32)
    wide = np.zeros(mask.shape, dtype=np.int32)
    wide[:, run:] = cumulative[:, run:] - cumulative[:, :-run]
    per_row = (wide >= run).sum(axis=1)
    y = int(np.argmax(per_row))
    if per_row[y] < run:
        return None

    xs = np.nonzero(mask[y])[0]
    segments = np.split(xs, np.nonzero(np.diff(xs) > 1)[0] + 1)
    fill = max(segments, key=len)
    fill_left = int(fill[0])

    # 未填充的那段轨道（行底色）→ 右端
    row_bg = np.all(frame[y] == np.array(ROW_BG, dtype=np.uint8), axis=1)
    bg_xs = np.nonzero(row_bg)[0]
    if len(bg_xs) == 0:
        return None
    bg_segments = np.split(bg_xs, np.nonzero(np.diff(bg_xs) > 1)[0] + 1)
    track = max(bg_segments, key=len)
    return fill_left, int(track[-1]), y


def pure_hue(rgb):
    """纯色相颜色 —— 和应用里色相条上那个「当前色标记」的填充色一致。

    应用那边是 RgbFromHsv(Hsv{h, 1, 1})：彩度固定为 1、明度为 1，
    所以 offset = v - chroma = 0，颜色就是色相环上那个纯色。
    """
    hue = hue_of(rgb)
    prime = hue / 60.0
    x = 1.0 - abs(prime % 2.0 - 1.0)
    table = [(1.0, x, 0.0), (x, 1.0, 0.0), (0.0, 1.0, x),
             (0.0, x, 1.0), (x, 0.0, 1.0), (1.0, 0.0, x)]
    sector = table[int(hue // 60.0) % 6]
    return tuple(int(round(c * 255)) for c in sector)


def find_hue_marker(frame: np.ndarray, color):
    """找色相条上「当前色标记」：返回 (中心 y, 左端 x, 右端 x)。

    标记的填充色就是纯色相。SV 方块右边缘也带这个颜色，但它只有一两列宽，
    而标记有几十列宽 —— 取最宽的那一段即可。
    """
    delta = np.abs(frame.astype(np.int16) - np.array(color, dtype=np.int16)).sum(axis=2)
    mask = delta < 24
    columns = np.nonzero(mask.any(axis=0))[0]
    if len(columns) == 0:
        return None
    segments = np.split(columns, np.nonzero(np.diff(columns) > 1)[0] + 1)
    segment = max(segments, key=len)
    if len(segment) < 15:
        return None
    rows = np.nonzero(mask[:, segment[0]:segment[-1] + 1].any(axis=1))[0]
    return float(np.mean(rows)), int(segment[0]), int(segment[-1])


def find_hue_bar_extent(frame: np.ndarray, x: int):
    """在色相条的某一列上量出它的上下端点，返回 (顶 y, 底 y)。

    色相条是一整条饱和的彩虹；卡片底色是灰的，所以「饱和像素」就是色相条。
    但标记的白描边会把这一列切成两段，间隔小的段要接回去。
    """
    column = frame[:, x]
    saturated = (column.max(axis=1).astype(int) - column.min(axis=1).astype(int)) > 60
    rows = np.nonzero(saturated)[0]
    if len(rows) == 0:
        return None
    segments = np.split(rows, np.nonzero(np.diff(rows) > 1)[0] + 1)
    segment = max(segments, key=len)
    top, bottom = int(segment[0]), int(segment[-1])
    for other in segments:
        if len(other) == 0:
            continue
        if other[-1] < top and top - other[-1] < 30:
            top = int(other[0])
        elif other[0] > bottom and other[0] - bottom < 30:
            bottom = int(other[-1])
    return top, bottom


def hue_of(rgb):
    """和 color::HsvFromRgb 一样的色相算法。"""
    r, g, b = (v / 255.0 for v in rgb)
    highest = max(r, g, b)
    lowest = min(r, g, b)
    chroma = highest - lowest
    if chroma <= 0:
        return 0.0
    if highest == r:
        hue = 60.0 * (g - b) / chroma
    elif highest == g:
        hue = 60.0 * ((b - r) / chroma + 2.0)
    else:
        hue = 60.0 * ((r - g) / chroma + 4.0)
    return hue % 360.0


# --- input -----------------------------------------------------------------

def click(hwnd, x: int, y: int) -> None:
    lp = (int(y) << 16) | (int(x) & 0xFFFF)
    u.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp)
    u.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lp)
    u.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)
    time.sleep(0.25)


def drag(hwnd, x1: int, y1: int, x2: int, y2: int) -> None:
    """按下 → 移动 → 松开，中间的移动必须带着 MK_LBUTTON。"""
    lp1 = (int(y1) << 16) | (int(x1) & 0xFFFF)
    lp2 = (int(y2) << 16) | (int(x2) & 0xFFFF)
    u.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp1)
    u.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lp1)
    time.sleep(0.15)
    u.PostMessageW(hwnd, WM_MOUSEMOVE, 1, lp2)   # wParam 带 MK_LBUTTON
    time.sleep(0.15)
    u.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp2)
    time.sleep(0.3)


def main() -> int:
    exe = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else EXE
    if not exe.exists():
        print(f'找不到 {exe}')
        return 2

    proc = subprocess.Popen([str(exe)])
    try:
        time.sleep(3.5)
        main_hwnd = next((h for h, t in windows_of(proc.pid) if t.startswith('Palette')), None)
        if not main_hwnd:
            print('没找到主窗口')
            return 2

        # 等窗口稳定：刚创建时应用还会按实际 DPI 调整一次尺寸，
        # 这期间截图和后续点击的坐标会对不上。
        previous = None
        for _ in range(40):
            rect = W.RECT()
            u.GetWindowRect(main_hwnd, ctypes.byref(rect))
            current = (rect.left, rect.top, rect.right, rect.bottom)
            if current == previous:
                break
            previous = current
            time.sleep(0.25)
        time.sleep(0.5)

        def snapshot():
            """抓一张客户区，并在**同一张图**上找出数值行。

            千万别分两次抓：窗口位置一变，两张图的坐标就不在同一个坐标系里，
            点下去会落到别的地方（曾经因此把 SV 方块左下角点成了纯黑）。
            """
            frame = capture_client(main_hwnd)
            return frame, find_value_rows(frame)

        def rows():
            return snapshot()[1]

        def value_editor():
            for h, cls, visible in children_of(main_hwnd):
                if cls == 'Edit' and visible:
                    return h
            return None

        def read_hex() -> str:
            """点行尾的「复制」按钮，把 HEX 读回剪贴板。"""
            _, rects = snapshot()
            if not rects:
                return ''
            left, top, right, bottom = rects[0]
            clear_clipboard()
            click(main_hwnd, right - 20, (top + bottom) // 2)
            return clipboard_text().strip()

        def open_editor(index: int):
            frame, rects = snapshot()
            if len(rects) <= index:
                return None
            span = value_text_span(frame, rects[index])
            if span is None:
                return None
            # 点在数值文字上：那里一定是可编辑区域
            click(main_hwnd, (span[0] + span[1]) // 2, (rects[index][1] + rects[index][3]) // 2)
            return value_editor()

        def type_into(editor, text: str) -> None:
            set_text(editor, text)
            u.SendMessageW(editor, EM_SETSEL, 0, -1)
            u.PostMessageW(editor, WM_KEYDOWN, VK_RETURN, 0)
            time.sleep(0.35)

        def press(editor, key: int) -> None:
            u.PostMessageW(editor, WM_KEYDOWN, key, 0)
            time.sleep(0.35)

        print('[1] 数值行定位')
        rects = rows()
        check('截图中找到 5 行数值', len(rects) == 5, f'{len(rects)} 行')
        if len(rects) != 5:
            return 1

        baseline = read_hex()
        check('点「复制」能读回 HEX', baseline.startswith('#') and len(baseline) == 7, baseline)

        print('[2] 写 HEX 改颜色')
        editor = open_editor(0)
        check('点数值出现编辑框', editor is not None)
        if editor is not None:
            type_into(editor, '#12AB34')
            after = read_hex()
            check('写 HEX 后颜色变了', after.upper() == '#12AB34', f'{baseline} -> {after}')

        print('[3] 写 RGB / HSV / HSL / CMYK')
        editor = open_editor(1)
        if editor is not None:
            type_into(editor, 'rgb(255, 128, 0)')
            after = read_hex()
            check('写 RGB 后颜色变了', after.upper() == '#FF8000', after)
        else:
            check('RGB 行能打开编辑框', False)

        editor = open_editor(2)
        if editor is not None:
            type_into(editor, 'hsv(120, 100%, 100%)')
            after = read_hex()
            check('写 HSV 后颜色变了', after.upper() == '#00FF00', after)
        else:
            check('HSV 行能打开编辑框', False)

        editor = open_editor(3)
        if editor is not None:
            type_into(editor, 'hsl(240, 100%, 50%)')
            after = read_hex()
            check('写 HSL 后颜色变了', after.upper() == '#0000FF', after)
        else:
            check('HSL 行能打开编辑框', False)

        editor = open_editor(4)
        if editor is not None:
            type_into(editor, 'cmyk(100%, 0%, 0%, 0%)')
            after = read_hex()
            check('写 CMYK 后颜色变了', after.upper() == '#00FFFF', after)
        else:
            check('CMYK 行能打开编辑框', False)

        print('[4] 非法输入不乱改颜色')
        before = read_hex()
        editor = open_editor(0)
        if editor is not None:
            type_into(editor, 'zzz-not-a-color')
            after = read_hex()
            check('非法 HEX 被拒绝，颜色不变', after.upper() == before.upper(),
                  f'{before} -> {after}')

        print('[5] Esc 取消编辑')
        before = read_hex()
        editor = open_editor(0)
        if editor is not None:
            set_text(editor, '#010203')
            press(editor, VK_ESCAPE)
            after = read_hex()
            check('Esc 放弃编辑，颜色不变', after.upper() == before.upper(),
                  f'{before} -> {after}')
        else:
            check('Esc 场景能打开编辑框', False)

        print('[6] Tab 跳到下一行')
        editor = open_editor(0)
        if editor is not None:
            u.PostMessageW(editor, WM_KEYDOWN, VK_TAB, 0)
            time.sleep(0.4)
            # 编辑框是复用的同一个控件，所以看内容变了没有
            moved = value_editor()
            check('Tab 后编辑框挪到下一行',
                  moved is not None and text_of(moved).lower().startswith('rgb('),
                  text_of(moved) if moved else 'no editor')
        else:
            check('Tab 场景能打开编辑框', False)

        print('[7] 点行首标签也能复制')
        rects = rows()
        clear_clipboard()
        click(main_hwnd, rects[0][0] + 25, (rects[0][1] + rects[0][3]) // 2)
        copied = clipboard_text().strip()
        check('点「HEX」标签复制出 HEX', copied.startswith('#') and len(copied) == 7, copied)

        print('[8] 点方块改颜色')
        frame = capture_client(main_hwnd)
        before = read_hex()
        # 方块在左卡片里，取其中心偏左的位置
        click(main_hwnd, int(frame.shape[1] * 0.12), int(frame.shape[0] * 0.30))
        after = read_hex()
        check('拖方块后颜色变了', after.upper() != before.upper(), f'{before} -> {after}')

        print('[9] RGB 滑条')
        # 先定一个起点颜色：三个通道都有值，滑条的填充段才看得见
        editor = open_editor(0)
        if editor is not None:
            type_into(editor, '#4080C0')

        frame = capture_client(main_hwnd)
        tracks = [find_slider_track(frame, c) for c in CHANNEL_COLORS]
        check('找到 3 条 RGB 滑条', all(t is not None for t in tracks),
              ', '.join('-' if t is None else f'x{t[0]}..{t[1]}@y{t[2]}' for t in tracks))

        if all(t is not None for t in tracks):
            def read_rgb():
                """读回 RGB 那一行的数值（点行尾「复制」）。"""
                rects = rows()
                clear_clipboard()
                click(main_hwnd, rects[1][2] - 20, (rects[1][1] + rects[1][3]) // 2)
                numbers = [int(n) for n in re.findall(r'\d+', clipboard_text())]
                return tuple(numbers[:3]) if len(numbers) >= 3 else None

            start = read_rgb()
            check('读到 RGB 行', start == (64, 128, 192), str(start))

            left, right, y = tracks[0]
            span = right - left

            # 两点标定：点两个位置，读回程序算出来的值，反推「x ↔ 数值」的映射。
            # 这样断言的是程序自己的换算，而不是脚本里另写一套公式。
            p1 = left + int(span * 0.30)
            p2 = left + int(span * 0.70)
            click(main_hwnd, p1, y)
            first = read_rgb()
            click(main_hwnd, p2, y)
            second = read_rgb()
            if first is None or second is None or first[0] == second[0]:
                check('滑条能改 R 值', False, f'{first} -> {second}')
            else:
                track_width = (p2 - p1) * 255.0 / (second[0] - first[0])
                track_x = p1 - track_width * first[0] / 255.0

                target = 200
                spot = int(round(track_x + track_width * target / 255.0))
                click(main_hwnd, spot, y)
                now = read_rgb()
                check('点滑条能精确落到目标值', now is not None and abs(now[0] - target) <= 1,
                      f'R={now[0] if now else None}（目标 {target}）')
                check('只动了 R，G/B 没变', now is not None and now[1:] == start[1:],
                      f'{start} -> {now}')

                print('[10] 拾色器跟着 RGB 走')
                shot = capture_client(main_hwnd)
                marker = find_hue_marker(shot, pure_hue(now))
                check('色相条上的当前色标记跟着换了位置', marker is not None)
                if marker is not None:
                    marker_y, mark_left, mark_right = marker
                    extent = find_hue_bar_extent(shot, (mark_left + mark_right) // 2)
                    if extent is None:
                        check('能量出色相条的高度', False)
                    else:
                        top, bottom = extent
                        expected = top + (hue_of(now) / 360.0) * (bottom - top)
                        check('标记落在新色相对应的位置上',
                              abs(marker_y - expected) <= 8,
                              f'标记 y={marker_y:.0f}，理论 y={expected:.0f}（色相 {hue_of(now):.0f}°）')

                print('[11] 拖动滑条')
                drag(main_hwnd, spot, y, spot - 120, y)
                dragged = read_rgb()
                check('拖动滑条能连续改值',
                      dragged is not None and dragged[0] < target,
                      f'R={dragged[0] if dragged else None}')

        print('[12] 缩到最小尺寸不塌')
        u.SetWindowPos(main_hwnd, None, 100, 100, 980, 750, 0x0004)  # SWP_NOZORDER
        time.sleep(0.6)
        frame = capture_client(main_hwnd)
        rects = find_value_rows(frame)
        check('最小尺寸下仍有 5 行数值', len(rects) == 5, f'{len(rects)} 行')
        tracks = [find_slider_track(frame, c) for c in CHANNEL_COLORS]
        check('最小尺寸下三条滑条还在', all(t is not None for t in tracks))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print()
    if FAILURES:
        print(f'[FAIL] {len(FAILURES)} 项未通过：' + '、'.join(FAILURES))
        return 1
    print('[ok] 界面冒烟测试全部通过')
    return 0


if __name__ == '__main__':
    sys.exit(main())
