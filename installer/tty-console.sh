#!/bin/sh
# 在指定 VT 上装载 16x32 控制台字体。
#
# 为什么要换字体：1280x800 面板用内核默认的 8x16 字体只有 160x50 的栅格，
# whiptail 的对话框会缩在屏幕中间一小块；换成 16x32 后正好 80x25，
# 与 Debian Installer 的控制台栅格一致，对话框铺满屏幕。
#
# 为什么选 VGA 布局字体而不是 Terminus：安装器运行在 LC_ALL=C 下，
# ncurses 用 DEC 线条字符（ACS）画边框，这要求字体的制表符落在 CP437 位置上。
# Lat15-VGA* 是 CP437 布局 + Latin-15 字形，边框才是连续实线；
# 换成 Unicode 排序的 Terminus，边框会退化成 q/x/l/m 字母。
FONT_PRIMARY=/usr/share/consolefonts/Lat15-VGA32x16.psf.gz
FONT_FALLBACK=/usr/share/consolefonts/Lat15-Terminus32x16.psf.gz

for vt in "$@"; do
    [ -c "/dev/$vt" ] || continue
    if [ -f "$FONT_PRIMARY" ]; then
        setfont -C "/dev/$vt" "$FONT_PRIMARY" 2>/dev/null && continue
    fi
    [ -f "$FONT_FALLBACK" ] && setfont -C "/dev/$vt" "$FONT_FALLBACK" 2>/dev/null
done
exit 0
