#!/usr/bin/env python3
"""抓一张带标题栏的整窗截图，并核对窗口图标句柄。

用来验证两件事：
1. 标题栏左上角真的画的是新图标（不是系统默认的通用图标）；
2. WM_GETICON 能拿到非空的图标句柄 —— 任务栏和「alt+tab」用的是它。

用法：python tools/shoot_window.py
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import os
import pathlib
import subprocess
import time

from PIL import Image

try:
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

u = ctypes.windll.user32
g = ctypes.windll.gdi32

u.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
u.GetWindowRect.argtypes = [W.HWND, ctypes.POINTER(W.RECT)]
u.SendMessageW.restype = ctypes.c_ssize_t
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
g.GetObjectW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
u.GetIconInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "bin" / "Palette.exe"
OUT = ROOT / "build" / "preview"


class ICONINFO(ctypes.Structure):
    _fields_ = [("fIcon", W.BOOL), ("xHotspot", W.DWORD), ("yHotspot", W.DWORD),
                ("hbmMask", W.HBITMAP), ("hbmColor", W.HBITMAP)]


class BITMAP(ctypes.Structure):
    _fields_ = [("bmType", W.DWORD), ("bmWidth", W.LONG), ("bmHeight", W.LONG),
                ("bmWidthBytes", W.WORD), ("bmPlanes", W.WORD),
                ("bmBitCount", W.WORD), ("bmBits", ctypes.c_void_p)]


class BMI(ctypes.Structure):
    _fields_ = [("biSize", W.DWORD), ("biWidth", W.LONG), ("biHeight", W.LONG),
                ("biPlanes", W.WORD), ("biBitCount", W.WORD),
                ("biCompression", W.DWORD), ("biSizeImage", W.DWORD),
                ("biXPelsPerMeter", W.LONG), ("biYPelsPerMeter", W.LONG),
                ("biClrUsed", W.DWORD), ("biClrImportant", W.DWORD)]


def find_window(pid: int):
    found = []
    CB = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def proc(hwnd, _):
        owner = W.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            text = ctypes.create_unicode_buffer(256)
            u.GetWindowTextW(hwnd, text, 256)
            if text.value:
                found.append((hwnd, text.value))
        return True

    u.EnumWindows(CB(proc), 0)
    return found[0] if found else (None, None)


def icon_size(handle):
    info = ICONINFO()
    if not u.GetIconInfo(handle, ctypes.byref(info)):
        return None
    bmp = BITMAP()
    g.GetObjectW(info.hbmColor, ctypes.sizeof(bmp), ctypes.byref(bmp), 0)
    return bmp.bmWidth, bmp.bmHeight


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen([str(EXE)])
    try:
        time.sleep(4.0)
        hwnd, title = find_window(proc.pid)
        if not hwnd:
            print("[FAIL] 没找到主窗口")
            return 1
        print(f"window : {hex(hwnd)}  {title}")

        for flag, label in ((1, "ICON_BIG"), (2, "ICON_SMALL"), (0, "ICON_SMALLEX")):
            handle = u.SendMessageW(hwnd, 0x007F, flag, 0)   # WM_GETICON
            if not handle:
                print(f"  WM_GETICON {label:13s} = NULL   <-- 异常")
                continue
            size = icon_size(handle)
            print(f"  WM_GETICON {label:13s} = {hex(handle)}  size={size}")

        rect = W.RECT()
        u.GetWindowRect(hwnd, ctypes.byref(rect))
        width = rect.right - rect.left
        height = rect.bottom - rect.top

        hdc = u.GetDC(0)
        mdc = g.CreateCompatibleDC(hdc)
        bitmap = g.CreateCompatibleBitmap(hdc, width, height)
        g.SelectObject(mdc, bitmap)
        u.PrintWindow(hwnd, mdc, 2)   # PW_RENDERFULLCONTENT

        info = BMI()
        info.biSize = ctypes.sizeof(info)
        info.biWidth = width
        info.biHeight = -height
        info.biPlanes = 1
        info.biBitCount = 32
        info.biCompression = 0
        buf = ctypes.create_string_buffer(width * height * 4)
        g.GetDIBits(mdc, bitmap, 0, height, buf, ctypes.byref(info), 0)

        image = Image.frombuffer("RGBA", (width, height), buf, "raw", "BGRA", 0, 1).convert("RGB")
        image.save(OUT / "window_with_icon.png")
        print("saved", OUT / "window_with_icon.png", image.size)

        strip = image.crop((0, 0, min(width, 760), 56))
        strip = strip.resize((strip.width * 2, strip.height * 2), Image.LANCZOS)
        strip.save(OUT / "titlebar.png")
        print("saved", OUT / "titlebar.png")

        g.DeleteObject(bitmap)
        g.DeleteDC(mdc)
        u.ReleaseDC(hwnd, hdc)
        return 0
    finally:
        proc.terminate()


if __name__ == "__main__":
    raise SystemExit(main())
