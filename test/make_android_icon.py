#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 Android 传统（API < 26）启动图标 PNG。

    python3 test/make_android_icon.py [res_dir]

自适应图标（mipmap-anydpi-v26/ic_launcher.xml + drawable 矢量）是主路径，
但 API 24/25（Qt6 默认 minSdk 24）不认 adaptive-icon，必须有按密度分桶的
位图兜底，否则启动器找不到 android:icon 引用的资源。

设计与矢量前景一致：暗色底 + 对角斜面 + 翠绿 ">_"（PIL 画多段线/圆角条，
不依赖字体）。图标改版时改 drawable/*.xml 与本脚本，再跑一次即可。
"""
import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("需要 Pillow：pip install pillow（或 dnf install python3-Pillow）")

BACK = (21, 24, 30)        # #15181E
BACK_SHEEN = (31, 36, 45)  # #1F242D
FORE = (108, 227, 168)     # #6CE3A8

# 密度桶 → 边长（ldpi 起步，launcher 自己缩放，给到 xxxhdpi 覆盖主流屏）
DENSITIES = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}


def render(size: int) -> Image.Image:
    """画 size×size 的图标：108dp 设计稿按比例缩放。"""
    img = Image.new("RGB", (size, size), BACK)
    d = ImageDraw.Draw(img)
    k = size / 108.0

    # 对角斜面（与 ic_launcher_background.xml 的 path 同形）
    d.polygon([(0, 0), (size, 0), (size, 42 * k), (0, 86 * k)], fill=BACK_SHEEN)

    w = max(2, round(9 * k))
    # ">""：两段折线，圆角连接（joint='curve' 需要把两段画成一条折线）
    d.line([(37 * k, 39 * k), (58 * k, 54 * k), (37 * k, 69 * k)],
           fill=FORE, width=w, joint="curve")
    # 圆角线段无法直接画Joint两端，首末端点补圆点模拟 round cap
    r = w / 2.0
    for px, py in [(37 * k, 39 * k), (37 * k, 69 * k)]:
        d.ellipse([px - r, py - r, px + r, py + r], fill=FORE)
    # 光标 "_"
    d.rounded_rectangle([66 * k, 69 * k - r, 83 * k, 69 * k + r],
                        radius=r, fill=FORE)
    return img


def main() -> None:
    res_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), os.pardir, "android", "res")
    for bucket, size in DENSITIES.items():
        out_dir = os.path.join(res_dir, bucket)
        os.makedirs(out_dir, exist_ok=True)
        path = os.path.join(out_dir, "ic_launcher.png")
        render(size).save(path, "PNG")
        print(f"{path} ({size}x{size})")


if __name__ == "__main__":
    main()
