#!/usr/bin/env python3
"""Palette 应用图标渲染引擎。

构图
----
就是程序拾色器的微缩版：

    ┌──────────────────┐
    │  ▓▓▓▓▓▓   ▒▒▒   │   深色圆角底 = 应用窗口
    │  ▓▓▓▓▓▓   ▒▒▒   │   ▓ = 色相场（横向铺满整个色相环，纵向由亮到暗）
    │  ▓▓▓▓▓▓   ▒▒▒   │   ▒ = 色相条（红→黄→绿→青→蓝→品红→红，纵向）
    │  ▓▓▓▓▓▓   ━━━   │   ━ = 色相条上的当前色标记（白框）
    └──────────────────┘

为什么方块用「色相环」而不是应用里的「饱和度/明度方块」
------------------------------------------------------
应用里的 SV 方块横轴是**饱和度**，所以整个方块只有一种色相 —— 缩到 16px
一平均就是一块灰蓝色，配上旁边的彩条，整个图标看着像「灰盒子 + 一条彩带」。

图标要的是「这是个颜色工具」，所以把方块的横轴改成**色相**：整条色相环
铺开，纵向再压一道明度。这样任何尺寸下方块本身都是彩色的，一眼就知道
是配色工具；同时它和右边的色相条是同一套色标（红→黄→绿→青→蓝→品红→红），
和程序里的 `hueStops` 完全一致。

配色全部取自应用本身：

* 底色 `#333D52 → #1A2029` = 应用卡片的深色主题
* 当前色 `#3A83D6`（色相 212°），色相条上那个标记就是它的纯色相 `#0075FF`

小尺寸
------
所有几何按 256px 参考画布定义，渲染时先放大 4 倍再 LANCZOS 缩回去，
所以 16px 也不会出现锯齿。纵向明度只压到 30%（不到纯黑），方块底部不会变成
一块死黑糊进深色底板。
"""

from __future__ import annotations

import pathlib

import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
PREVIEW = ROOT / "build" / "preview"

SS = 4                      # 超采样倍数
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
REF = 256                   # 参考画布尺寸

# ==========================================================================
# 配色
# ==========================================================================
BODY_TOP = "#333D52"        # 应用卡片色
BODY_BOTTOM = "#1A2029"     # 应用窗口底色
BODY_RIM = "#4A5670"        # 底板外沿的细描边（深色任务栏上也要有轮廓）
PURE_HUE = "#0075FF"        # 色相 212° 的**纯**色相（色相条上那个颜色）
FIELD_RIM = "#4A5670"       # 色相场外沿细描边（应用里是 cardBorder）

# 色相条的 7 个色标 —— 与 app.cpp 的 hueStops 同序
HUE_STOPS = [
    (0.00, (255, 0, 0)),
    (1 / 6, (255, 255, 0)),
    (2 / 6, (0, 255, 0)),
    (3 / 6, (0, 255, 255)),
    (4 / 6, (0, 0, 255)),
    (5 / 6, (255, 0, 255)),
    (1.00, (255, 0, 0)),
]

# ==========================================================================
# 几何（单位：参考画布像素）
# ==========================================================================
BODY = (6.0, 6.0, 244.0)         # x, y, 边长
BODY_RADIUS = 58.0

FIELD = (28.0, 48.0, 152.0)      # x, y, 边长（色相场）
FIELD_RADIUS = 18.0

HUE_BAR = (194.0, 48.0, 34.0, 152.0)   # x, y, 宽, 高（色相条）
HUE_RADIUS = 16.0

CURSOR_H = 212.0            # 当前色的色相（与应用默认色 #3A83D6 同色相）

HUE_MARK_HEIGHT = 17.0      # 色相条上当前色标记的高度
FIELD_FLOOR = 0.30          # 色相场底边保留的亮度（不到纯黑，免得发闷）


def _rgb(hex_color: str) -> np.ndarray:
    h = hex_color.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32)


def _round_mask(size: int, box, radius: float) -> Image.Image:
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle(
        [float(box[0]), float(box[1]), float(box[0] + box[2]), float(box[1] + box[3])],
        radius=radius, fill=255)
    return m


def _ramp(stops, samples: int) -> np.ndarray:
    """按 stops=[(pos,(r,g,b)), ...] 在 [0,1] 上取 samples 个颜色，返回 (samples,3)。"""
    xs = np.linspace(0.0, 1.0, samples, dtype=np.float32)
    out = np.zeros((samples, 3), dtype=np.float32)
    for i in range(len(stops) - 1):
        p0, c0 = stops[i]
        p1, c1 = stops[i + 1]
        m = (xs >= p0) & (xs <= p1)
        if not m.any():
            continue
        t = (xs[m] - p0) / max(p1 - p0, 1e-6)
        for ch in range(3):
            out[m, ch] = c0[ch] * (1.0 - t) + c1[ch] * t
    return out


def _hue_field(size: int) -> np.ndarray:
    """色相场：横向铺满整个色相环，纵向由亮（顶）压到 FIELD_FLOOR（底）。"""
    cols = _ramp(HUE_STOPS, size)
    arr = np.repeat(cols[None, :, :], size, axis=0)
    ys = np.linspace(1.0, FIELD_FLOOR, size, dtype=np.float32)[:, None, None]
    return arr * ys


def _hue_strip(size: int) -> np.ndarray:
    """色相条：纵向铺满色相环。"""
    cols = _ramp(HUE_STOPS, size)
    return np.repeat(cols[:, None, :], size, axis=1)


def render(size: int) -> Image.Image:
    big = size * SS
    scale = big / REF

    def S(v: float) -> float:
        return v * scale

    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    def layer_from(arr: np.ndarray, x: float = 0.0, y: float = 0.0,
                   w: float = 0.0, h: float = 0.0) -> Image.Image:
        """把 (h,w,3) 的浮点数组变成 RGBA 图，放到画布的 (x,y) 处。

        注意是**贴**上去，不是拉伸铺满整张画布再靠遮罩裁 —— 后者会让控件只
        显示渐变中段（比如色相环的红色和品红直接消失），很难看出是哪里错了。
        """
        hh, ww, _ = arr.shape
        rgba = np.concatenate(
            [arr, np.full((hh, ww, 1), 255.0, np.float32)], axis=2).astype(np.uint8)
        target_w = max(1, int(round(w))) if w else ww
        target_h = max(1, int(round(h))) if h else hh
        small = Image.fromarray(rgba, "RGBA").resize((target_w, target_h), Image.BILINEAR)
        canvas = Image.new("RGBA", (big, big), (0, 0, 0, 0))
        canvas.paste(small, (int(round(x)), int(round(y))))
        return canvas

    # ---------------- 底板 ----------------
    body_stops = [(0.0, tuple(_rgb(BODY_TOP).astype(int))),
                  (1.0, tuple(_rgb(BODY_BOTTOM).astype(int)))]
    body_grad = np.repeat(_ramp(body_stops, big)[None, :, :], big, axis=0)
    body_mask = _round_mask(big, (S(BODY[0]), S(BODY[1]), S(BODY[2]), S(BODY[2])),
                             S(BODY_RADIUS))
    img.alpha_composite(Image.composite(
        layer_from(body_grad), Image.new("RGBA", (big, big), (0, 0, 0, 0)), body_mask))

    # 底板外沿描一圈浅色细线：深色任务栏上也需要一道轮廓把图标「兜」住。
    rim = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle(
        [S(BODY[0]), S(BODY[1]), S(BODY[0] + BODY[2]), S(BODY[1] + BODY[2])],
        radius=S(BODY_RADIUS), outline=tuple(_rgb(BODY_RIM).astype(int)) + (190,),
        width=max(1, int(round(S(2.2)))))
    img.alpha_composite(rim)

    # ---------------- 色相场 ----------------
    fx, fy, fs = S(FIELD[0]), S(FIELD[1]), S(FIELD[2])
    field_px = int(round(fs))
    field_mask = _round_mask(big, (fx, fy, fs, fs), S(FIELD_RADIUS))
    img.alpha_composite(Image.composite(
        layer_from(_hue_field(field_px), fx, fy, fs, fs),
        Image.new("RGBA", (big, big), (0, 0, 0, 0)), field_mask))

    # 方块外沿描一圈浅色细线（应用里是 StrokeRounded(sv, cardBorder)），
    # 否则方块下半部的暗部会直接溶进深色底板。
    field_rim = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(field_rim).rounded_rectangle(
        [fx, fy, fx + fs, fy + fs], radius=S(FIELD_RADIUS),
        outline=tuple(_rgb(FIELD_RIM).astype(int)) + (255,),
        width=max(1, int(round(S(2.0)))))
    img.alpha_composite(field_rim)

    # ---------------- 色相条 ----------------
    hx, hy, hw, hh = (S(HUE_BAR[0]), S(HUE_BAR[1]), S(HUE_BAR[2]), S(HUE_BAR[3]))
    hue_px = int(round(hh))
    hue_mask = _round_mask(big, (hx, hy, hw, hh), S(HUE_RADIUS))
    img.alpha_composite(Image.composite(
        layer_from(_hue_strip(hue_px), hx, hy, hw, hh),
        Image.new("RGBA", (big, big), (0, 0, 0, 0)), hue_mask))

    # ---------------- 色相条上的当前色标记 ----------------
    # 应用里：填充 = 该色相的纯色，白色描边。
    marker_y = hy + (CURSOR_H / 360.0) * hh
    marker = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(marker).rounded_rectangle(
        [hx - S(3.0), marker_y - S(HUE_MARK_HEIGHT * 0.5), hx + hw + S(3.0),
         marker_y + S(HUE_MARK_HEIGHT * 0.5)],
        radius=S(HUE_MARK_HEIGHT * 0.5),
        fill=tuple(_rgb(PURE_HUE).astype(int)) + (255,),
        outline=(255, 255, 255, 255), width=max(1, int(round(S(2.4)))))
    img.alpha_composite(marker)

    return img.resize((size, size), Image.LANCZOS)


def build_sheet(out_path: pathlib.Path) -> None:
    """评估图：上排 256px，下排 16/32px 实际像素放大展示。"""
    cell_w = 300
    sheet = Image.new("RGB", (cell_w, 300 + 150 + 60), (238, 240, 246))
    d = ImageDraw.Draw(sheet)
    big = render(256)
    sheet.paste(big, (22, 10), big)
    for j, s in enumerate((32, 16)):
        icon = render(s)
        scaled = icon.resize((s * 4, s * 4), Image.NEAREST)
        sheet.paste(scaled, (40 + j * 160, 300), scaled)
    d.text((22, 300 + 128), "256px 原图 / 32px / 16px（4 倍最近邻放大）",
           fill=(40, 42, 56))
    sheet.save(out_path)
    print("[ok]", out_path)


if __name__ == "__main__":
    ASSETS.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)

    # ico：每个尺寸都单独渲染一次（而不是把 256 缩下来），小尺寸才不会糊。
    frames = [render(s) for s in ICO_SIZES]
    ico_path = ASSETS / "palette.ico"
    frames[-1].save(ico_path, format="ICO",
                    sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])
    print("[ok]", ico_path)

    render(256).save(PREVIEW / "icon_256.png")
    print("[ok]", PREVIEW / "icon_256.png")
    build_sheet(PREVIEW / "icon_compare.png")
