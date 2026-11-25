#!/bin/bash
# 阶段 3：构建 initramfs（busybox，仅负责找到介质并组装 overlay 根）
source "$(dirname "$0")/00-env.sh"
require cpio gzip

rm -rf "$INITRAMFS"
mkdir -p "$INITRAMFS"/{bin,dev,proc,sys,run,tmp,cdrom,ro,overlay,newroot}

BUSYBOX="$(command -v busybox || true)"
[ -n "$BUSYBOX" ] || die "缺少 busybox（apt install busybox-static）"
if ldd "$BUSYBOX" 2>/dev/null | grep -q "libc"; then
    warn "busybox 为动态链接，initramfs 内可能缺少依赖库"
fi

cp "$BUSYBOX" "$INITRAMFS/bin/busybox"
chmod 0755 "$INITRAMFS/bin/busybox"
for a in sh mount umount mkdir mknod cat echo sleep ls grep sed awk blkid findfs \
         switch_root dmesg reboot poweroff modprobe ln rm; do
    ln -sf busybox "$INITRAMFS/bin/$a"
done

install -m 0755 "$PROJECT/installer/init" "$INITRAMFS/init"

log "打包 initramfs"
( cd "$INITRAMFS" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9 ) > "$DIST/initrd.img"
log "initramfs: $DIST/initrd.img ($(du -h "$DIST/initrd.img" | cut -f1))"
