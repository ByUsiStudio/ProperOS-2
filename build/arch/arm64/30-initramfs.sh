#!/usr/bin/env bash
# ProperOS 2.5 Mobile · ARM64 initramfs（ProLoader 作为 /init）
set -euo pipefail
source /workspace/properos-2.5/build/00-env.sh

ROOT="$ROOTFS_ARM64"
IR="$WORK/initramfs-arm64"
OUT="$WORK/initramfs-arm64.cpio.gz"

log "构建 ARM64 initramfs"
rm -rf "$IR"
mkdir -p "$IR"/{bin,sbin,dev,proc,sys,run,tmp,usr/lib/properos}

# busybox 提供救援 shell 与 tar/cp 等基础工具
install -Dm755 "$ROOT/bin/busybox" "$IR/bin/busybox"
for applet in sh mount umount mkdir tar cp mv rm cat ls ln chmod chown sleep switch_root \
              grep sed cut head tail dmesg sync poweroff reboot find xargs stat; do
    ln -sf busybox "$IR/bin/$applet"
done
ln -sf ../bin/busybox "$IR/sbin/init" 2>/dev/null || true

# 引导阶段 1：ProLoader
install -Dm755 "$REPO/src/build/arm64/proloader" "$IR/init"
install -Dm755 "$REPO/src/build/arm64/stboot" "$IR/usr/lib/properos/stboot"

chmod 1777 "$IR/tmp"

log "打包 cpio.gz"
( cd "$IR" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9 ) > "$OUT"
ls -lh "$OUT"
