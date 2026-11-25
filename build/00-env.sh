#!/bin/bash
# 公共环境变量
set -euo pipefail

export KVER="${KVER:-7.3.6}"
export KERNEL_VERSION="$KVER"
export KMAJOR="${KMAJOR:-7.3}"
export DEBIAN_SUITE="${DEBIAN_SUITE:-bookworm}"
export DEBIAN_MIRROR="${DEBIAN_MIRROR:-http://mirrors.tuna.tsinghua.edu.cn/debian}"
export KERNEL_MIRROR="${KERNEL_MIRROR:-https://mirrors.tuna.tsinghua.edu.cn/kernel}"
export JOBS="${JOBS:-$(nproc)}"

export PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export REPO="$PROJECT"
export WORK="${WORK:-/root/build}"
export CACHE="$WORK/cache"
export DIST="$PROJECT/dist"
export ISO_NAME="ProperOS-2.5-amd64.iso"
export VOLID="PROPEROS250"

export KERNEL_SRC="$WORK/linux-$KVER"
export KERNEL_SRC_ARM64="$WORK/linux-arm64"
export ROOTFS="$WORK/rootfs"
export ROOTFS_ARM64="$WORK/rootfs-arm64"
export INITRAMFS="$WORK/initramfs"
export ISODIR="$WORK/iso"
export ISODIR_ARM64="$WORK/iso-arm64"

mkdir -p "$WORK" "$DIST"

log()  { printf '\033[1;36m[build]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

require() {
    for t in "$@"; do
        command -v "$t" >/dev/null 2>&1 || die "缺少工具: $t"
    done
}
