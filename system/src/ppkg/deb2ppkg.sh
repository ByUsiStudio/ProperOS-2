#!/bin/sh
# ProperOS deb2ppkg —— 把 Debian .deb 转换成 .ppkg
#
# 转换后的包 format=deb，内嵌原始 payload.deb 与维护脚本存档。
# 安装时由 ppkg 调 dpkg 落地，因此 conffiles / triggers / maintainer scripts
# 的语义与 Debian 完全一致；ppkg 侧只增加来源记录与统一审计视图。
#
# 开发者：ByUsi Studio · 北啊呢
set -e

DEB="$1"
OUT="$2"
[ -n "$DEB" ] && [ -n "$OUT" ] || { echo "用法: deb2ppkg <输入.deb> <输出.ppkg>" >&2; exit 1; }
[ -f "$DEB" ] || { echo "deb2ppkg: 文件不存在: $DEB" >&2; exit 1; }
command -v dpkg-deb >/dev/null 2>&1 || { echo "deb2ppkg: 需要 dpkg-deb" >&2; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/deb2ppkg.XXXXXX") || exit 1
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

NAME=$(dpkg-deb -f "$DEB" Package 2>/dev/null)
VER=$(dpkg-deb -f "$DEB" Version 2>/dev/null)
ARCH=$(dpkg-deb -f "$DEB" Architecture 2>/dev/null)
DESC=$(dpkg-deb -f "$DEB" Description 2>/dev/null | head -1)
DEPS=$(dpkg-deb -f "$DEB" Depends 2>/dev/null | tr '\n' ' ')
SRC=$(dpkg-deb -f "$DEB" Source 2>/dev/null)
[ -n "$NAME" ] || { echo "deb2ppkg: 无法读取 Package 字段" >&2; exit 1; }
[ -n "$VER" ] || VER=0
[ -n "$ARCH" ] || ARCH=all

SHA=$(sha256sum "$DEB" | cut -d' ' -f1)

cat > "$TMP/PKGINFO" <<EOF
name = $NAME
version = $VER
arch = $ARCH
description = ${DESC:-$NAME}
format = deb
origin = debian
origin-source = ${SRC:-$NAME}
deb-package = $NAME
deb-sha256 = $SHA
deb-file = $(basename "$DEB")
depends = $DEPS
EOF

cp -f "$DEB" "$TMP/payload.deb"

# 维护脚本存档（审计用；真正执行由 dpkg 负责）
dpkg-deb -e "$DEB" "$TMP/control" 2>/dev/null || true
mkdir -p "$TMP/scripts"
for s in preinst postinst prerm postrm conffiles triggers; do
    [ -f "$TMP/control/$s" ] && cp -f "$TMP/control/$s" "$TMP/scripts/$s"
done
rm -rf "$TMP/control"

mkdir -p "$(dirname "$OUT")"
if [ -n "$(ls -A "$TMP/scripts" 2>/dev/null)" ]; then
    tar -czf "$OUT" -C "$TMP" PKGINFO payload.deb scripts
else
    tar -czf "$OUT" -C "$TMP" PKGINFO payload.deb
fi

OUTSHA=$(sha256sum "$OUT" | cut -d' ' -f1)
echo "$OUTSHA  $(basename "$OUT")" > "$OUT.sha256"

printf '%s %s -> %s (%s)\n' "$NAME" "$VER" "$OUT" "$OUTSHA"
