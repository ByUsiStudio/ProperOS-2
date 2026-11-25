#!/usr/bin/env python3
"""把 TTF 字形烘焙成 8 位灰度 alpha 位图图集，供 framebuffer UI 直接绘制。

用法: gen_font.py <输出头文件> [字号]

与旧版的区别：
  * 每像素 1 字节 alpha（不再是 1 bit），边缘来自字体的抗锯齿渲染，
    放大后仍是柔和过渡，不会出现硬锯齿。
  * 字符集覆盖 ASCII + GB2312 一级汉字（3755 字）+ 界面符号，
    应用名包含常用汉字时也能正常显示。
  * 字形表按码点排序，运行时二分查找。
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]

# 界面会用到的符号（字体缺字时自动跳过）
SYMBOLS = "°％‰…—–·×÷±≤≥≠∞←→↑↓✓✔✕✖●○■□▲▼◆★☆⌂⚙☰＋－＝"

# 中文标点（GB2312 的 A1-A9 区不在汉字区里，需单独补充）
CJK_PUNCT = ("。，、；：？！…—·《》〈〉「」『』【】〔〕（）［］｛｝"
             "“”‘’＂＇·～　￥℃№")

# 补充高频字（不在 GB2312 一级里的常用简体）
EXTRA = (
    "安装器向导磁盘分区格式化引导装载内核镜像数据文稿下载图片音乐视频相机"
    "设置网络蓝牙无线局域网飞行模式勿扰亮度音量振动铃声壁纸主题字体"
    "电池充电电量剩余百分号信号强度运营商屏幕锁定解锁关机重启"
)


def gb2312_level1():
    out = []
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            out.append(ch)
    return out


def build_charset():
    chars, seen = [], set()

    def add(ch):
        if ch not in seen:
            seen.add(ch)
            chars.append(ch)

    for cp in range(32, 127):
        add(chr(cp))
    for ch in gb2312_level1():
        add(ch)
    for ch in EXTRA:
        add(ch)
    for ch in SYMBOLS:
        add(ch)
    for ch in CJK_PUNCT:
        add(ch)
    add("？")
    return chars


def main(out_path, size=24):
    path = next((p for p in FONT_CANDIDATES if os.path.exists(p)), None)
    if not path:
        sys.exit("找不到可用字体")
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    line_h = ascent + descent + 2

    chars = build_charset()
    entries, bits = [], bytearray()

    for ch in chars:
        try:
            bbox = font.getbbox(ch)
        except Exception:
            continue
        adv = int(round(font.getlength(ch)))
        x0, y0, x1, y1 = bbox
        w = max(0, x1 - x0)
        h = max(0, y1 - y0)
        if w == 0 or h == 0:
            entries.append((ord(ch), 0, 0, adv, len(bits)))
            continue

        # 渲染到整行高度的画布：y 方向不偏移，保留字体自身的 baseline 位置，
        # 这样所有字形（含标点、下伸部）在同一 y 上绘制时基线自然对齐。
        img = Image.new("L", (w + 2, line_h), 0)
        ImageDraw.Draw(img).text((1 - x0, 0), ch, font=font, fill=255)
        px = img.load()
        off = len(bits)
        any_px = False
        for y in range(line_h):
            for x in range(w + 2):
                v = px[x, y]
                if v:
                    any_px = True
                bits.append(v)
        if not any_px:
            del bits[off:]
            entries.append((ord(ch), 0, 0, adv, off))
        else:
            entries.append((ord(ch), w + 2, line_h, adv, off))

    entries.sort(key=lambda e: e[0])

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：Noto Sans CJK 灰度 alpha 图集，勿手改 */\n")
        f.write("/* 生成命令: tools/gen_font.py src/gfx/font_atlas.h %d */\n" % size)
        f.write("#ifndef PROPEROS_FONT_ATLAS_H\n#define PROPEROS_FONT_ATLAS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("struct font_glyph { uint32_t cp; uint8_t w, h, adv; uint32_t off; };\n\n")
        f.write("#define FONT_HEIGHT %d\n" % line_h)
        f.write("#define FONT_ASCENT %d\n" % ascent)
        f.write("#define FONT_GLYPH_COUNT %d\n" % len(entries))
        f.write("#define FONT_ALPHA_BITS 1\n\n")

        f.write("static const unsigned char FONT_BITS[] = {\n")
        line = []
        for b in bits:
            line.append(str(b))
            if len(line) == 32:
                f.write(",".join(line) + ",\n")
                line = []
        if line:
            f.write(",".join(line) + ",\n")
        f.write("};\n\n")

        f.write("static const struct font_glyph FONT_GLYPHS[] = {\n")
        for cp, w, h, adv, off in entries:
            f.write("{0x%X,%d,%d,%d,%d},\n" % (cp, w, h, adv, off))
        f.write("};\n\n")
        f.write("#endif\n")

    total = len(bits) + len(entries) * 13
    print("glyphs=%d bitmap=%d total≈%.1f KiB font=%s size=%d"
          % (len(entries), len(bits), total / 1024.0, os.path.basename(path), size))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: gen_font.py <out.h> [size]")
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 24)
