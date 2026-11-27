#!/bin/sh
# ProperOS dpkg-shim —— apt 与 dpkg 之间的转换层
#
# apt 通过 Dir::Bin::dpkg 调用本脚本。凡是要落地 .deb 的事务，这里先：
#   1. 拦截内核与引导链相关包（apt 的 pin 是第一道，这里是第二道）
#   2. 把每个 .deb 转成 .ppkg 存进 /var/cache/properos/ppkg
#   3. 交给 ppkg 安装（ppkg 内部再调真正的 dpkg）
# 其余子命令（--configure/--status/--print-avail/--remove …）原样转发。
#
# 开发者：ByUsi Studio · 北啊呢
set -u

REAL=/usr/bin/dpkg
PPKG=/usr/bin/ppkg
DEB2PPKG=/usr/lib/properos/deb2ppkg
SPOOL=/var/cache/properos/ppkg
LOG=/var/log/properos/apt-guard.log

log() {
    mkdir -p "$(dirname "$LOG")" 2>/dev/null
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG" 2>/dev/null
}

[ -x "$REAL" ] || { echo "dpkg-shim: 找不到 $REAL" >&2; exit 1; }

MODE=""
case "${1:-}" in
    --unpack)   MODE=unpack;   shift ;;
    -i|--install) MODE=install; shift ;;
    *) exec "$REAL" "$@" ;;
esac

DEBS=""
FLAGS=""
for a in "$@"; do
    case "$a" in
        *.deb)  DEBS="$DEBS $a" ;;
        --*)    FLAGS="$FLAGS $a" ;;
        *)      : ;;
    esac
done

[ -n "$DEBS" ] || exec "$REAL" $FLAGS

# ---- 第一道闸：内核与引导链 ----
BLOCKED=0
for d in $DEBS; do
    [ -f "$d" ] || { echo "dpkg-shim: 文件不存在: $d" >&2; exit 1; }
    p=$(dpkg-deb -f "$d" Package 2>/dev/null)
    case "$p" in
        linux-image*|linux-headers*|linux-modules*|linux-kbuild*|linux-source*|\
        linux-support*|linux-perf*|linux-config*|grub-efi*|grub-common|\
        grub2-common|grub-pc*|shim-signed|systemd-boot*)
            echo "properos: 拒绝安装 $p" >&2
            echo "properos: 内核与引导链由 ProperOS 管理（A/B 槽 + properos-bootctl），" >&2
            echo "          不接受 Debian 的 linux-image / grub / shim 包，否则系统将无法启动。" >&2
            log "BLOCK $p $d"
            BLOCKED=1
            ;;
    esac
done
[ "$BLOCKED" -eq 0 ] || exit 1

# ---- 第二道：转换 + 安装 ----
mkdir -p "$SPOOL"
RC=0
for d in $DEBS; do
    pkg=$(dpkg-deb -f "$d" Package 2>/dev/null)
    ver=$(dpkg-deb -f "$d" Version 2>/dev/null)
    arch=$(dpkg-deb -f "$d" Architecture 2>/dev/null)
    [ -n "$pkg" ] || { echo "dpkg-shim: 读不出包名: $d" >&2; RC=1; continue; }
    ver=$(printf '%s' "$ver" | tr '/ ' '__')
    target="$SPOOL/${pkg}_${ver}_${arch}.ppkg"

    if ! "$DEB2PPKG" "$d" "$target" >/dev/null; then
        echo "dpkg-shim: 转换失败: $d" >&2
        RC=1
        continue
    fi

    if [ "$MODE" = unpack ]; then
        "$PPKG" install --unpack-only "$target" || RC=1
    else
        "$PPKG" install "$target" || RC=1
    fi
    log "CONVERT $pkg $ver $arch -> $target"
done

exit $RC
