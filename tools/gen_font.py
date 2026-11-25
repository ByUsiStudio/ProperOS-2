#!/usr/bin/env python3
"""把 TTF 字形烘焙成 C 位图图集，供 framebuffer 启动器直接绘制。
用法: gen_font.py <输出头文件>
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]
SIZE = 24

# 界面固定文案 + 常用符号
CJK = list("终端应用设置容器关于返回主页正在启动已安装存储电量版本开发者北啊呢"
           "无线网络蓝牙显示声音安全更新存储空间电池充电请稍候完成失败错误"
           "系统信息内核运行时间内存处理器架构设备名称型号序列号构建日期"
           "应用商店文件相机相册时钟日历天气音乐视频浏览器地图邮件短信电话"
           "联系人计算器录音笔记天气设置开发者选项恢复出厂重启关机锁屏")

def main(out_path):
    path = next((p for p in FONT_CANDIDATES if __import__("os").path.exists(p)), None)
    if not path:
        sys.exit("找不到可用字体")
    font = ImageFont.truetype(path, SIZE)
    ascent, descent = font.getmetrics()
    height = ascent + descent

    chars = []
    for cp in range(32, 127):
        chars.append(chr(cp))
    seen = set(chars)
    for ch in CJK:
        if ch not in seen:
            chars.append(ch); seen.add(ch)
    chars.append("？")

    glyphs = []
    bits = bytearray()
    for ch in chars:
        bbox = font.getbbox(ch)
        w = max(0, bbox[2])
        h = max(0, bbox[3])
        adv = int(round(font.getlength(ch)))
        if w == 0 or h == 0:
            w, h = max(1, adv), height
            img = Image.new("1", (w, h), 0)
        else:
            img = Image.new("1", (w, h), 0)
            d = ImageDraw.Draw(img)
            d.text((0, 0), ch, font=font, fill=1)
        stride = (w + 7) // 8
        off = len(bits)
        px = img.load()
        for y in range(h):
            for bx in range(stride):
                byte = 0
                for b in range(8):
                    x = bx * 8 + b
                    if x < w and px[x, y]:
                        byte |= 0x80 >> b
                bits.append(byte)
        glyphs.append((ord(ch), w, h, adv, off))

    with open(out_path, "w") as f:
        f.write("/* 自动生成：Noto Sans CJK / DejaVu 位图图集，勿手改 */\n")
        f.write("#ifndef PROPEROS_FONT_ATLAS_H\n#define PROPEROS_FONT_ATLAS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("struct font_glyph { uint32_t cp; uint8_t w, h, adv; uint32_t off; };\n\n")
        f.write("static const unsigned char FONT_BITS[] = {\n")
        for i in range(0, len(bits), 20):
            f.write("  " + ",".join(str(b) for b in bits[i:i+20]) + ",\n")
        f.write("};\n\n")
        f.write("static const struct font_glyph FONT_GLYPHS[] = {\n")
        for cp, w, h, adv, off in glyphs:
            f.write(f"  {{0x{cp:x},{w},{h},{adv},{off}}},\n")
        f.write("};\n\n")
        f.write(f"#define FONT_GLYPH_COUNT {len(glyphs)}\n")
        f.write(f"#define FONT_HEIGHT {height}\n")
        f.write(f"#define FONT_ASCENT {ascent}\n\n")
        f.write("#endif\n")
    print(f"字形 {len(glyphs)} 个，位图 {len(bits)} 字节 → {out_path}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "font_atlas.h")
