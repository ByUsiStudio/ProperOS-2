#!/usr/bin/env bash
# ProperOS 2.5 Mobile · ARM64 安装 ISO（UEFI 启动，Debian 风格文本安装界面）
set -euo pipefail
source /workspace/properos-2.5/build/00-env.sh

ROOT="$ROOTFS_ARM64"
STAGE="$WORK/iso-stage-arm64"
LIVE="$WORK/live-rootfs-arm64"
OUT="$DIST/ProperOS-2.5-arm64.iso"

rm -rf "$STAGE" "$LIVE"
mkdir -p "$STAGE"/{boot/grub,live} "$STAGE/efi/EFI/BOOT"

log "构建 live initramfs"
IR="$WORK/initramfs-live-arm64"
rm -rf "$IR"
mkdir -p "$IR"/{bin,sbin,dev,proc,sys,run,tmp}
install -Dm755 "$ROOT/bin/busybox" "$IR/bin/busybox"
for a in sh mount umount mkdir mknod ln ls cat sleep switch_root grep sed awk blkid df; do
    ln -sf busybox "$IR/bin/$a"
done
install -Dm755 "$REPO/installer/init" "$IR/init"
chmod 1777 "$IR/tmp"
( cd "$IR" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9 ) > "$STAGE/boot/initramfs-live.img"

log "准备 live 根（硬链接副本，避免复制大文件）"
cp -al "$ROOT" "$LIVE"
install -Dm755 "$REPO/installer/properos-live-installer.service" \
    "$LIVE/usr/lib/systemd/system/properos-live-installer.service"
mkdir -p "$LIVE/etc/systemd/system/multi-user.target.wants"
ln -sf /usr/lib/systemd/system/properos-live-installer.service \
       "$LIVE/etc/systemd/system/multi-user.target.wants/properos-live-installer.service"
# 注意：这里不能再改 $LIVE 的内容（清空 fstab、mask 单元……），因为 squashfs
# 就是安装器要拷贝给目标设备的“系统本体”，改了会把 live 专用配置装到设备上。
# live 运行期需要的改动一律放到 installer/init 里写进 overlay 上层。
cat > "$LIVE/etc/issue" <<'EOF'
ProperOS 2.5 Mobile Installer
EOF

log "压缩 rootfs → squashfs（xz，约 3-6 分钟）"
# 注意：-e 之后的参数全部被当作排除项，其它开关必须写在 -e 前面。
mksquashfs "$LIVE" "$STAGE/live/rootfs.squashfs" \
    -comp xz -Xbcj arm -b 1M -no-progress -noappend -no-xattrs \
    -e "$LIVE/proc/*" -e "$LIVE/sys/*" -e "$LIVE/dev/*" -e "$LIVE/run/*" \
    -e "$LIVE/tmp/*" -e "$LIVE/var/cache/apt/archives/*" >/dev/null

log "复制内核与引导器"
cp -f "$DIST/kernel/Image-properos-2.5" "$STAGE/boot/Image"
cp -f "$REPO/overlay/usr/lib/properos/boot/BOOTAA64.EFI" "$STAGE/efi/EFI/BOOT/BOOTAA64.EFI"

cat > "$STAGE/boot/grub/grub.cfg" <<'EOF'
set timeout=10
set default=0
set gfxpayload=keep

menuentry "ProperOS 2.5 Mobile  ·  Installer" --class properos {
    search --no-floppy --label --set=root PROPEROS250
    linux /boot/Image root=/dev/ram0 rw rootwait quiet loglevel=4 console=tty0 console=ttyAMA0,115200
    initrd /boot/initramfs-live.img
}

menuentry "ProperOS 2.5 Mobile  ·  Installer (verbose serial log)" {
    search --no-floppy --label --set=root PROPEROS250
    linux /boot/Image root=/dev/ram0 rw rootwait loglevel=7 console=tty0 console=ttyAMA0,115200
    initrd /boot/initramfs-live.img
}

menuentry "Unattended install to /dev/vda (test)" {
    search --no-floppy --label --set=root PROPEROS250
    linux /boot/Image root=/dev/ram0 rw rootwait quiet loglevel=4 console=tty0 console=ttyAMA0,115200 properos.autoinstall=/dev/vda properos.blankpass=1
    initrd /boot/initramfs-live.img
}

menuentry "Boot from hard disk" {
    exit
}
EOF

log "生成 EFI 启动镜像"
# El Torito 的扇区计数字段只有 16 位：镜像必须 < 32 MiB，否则计数溢出为 0，
# 固件读不到引导镜像会报 "failed to load ... Not Found"。16 MiB 是 FAT16 的下限，
# 既满足 16 位扇区计数（32768），又能放下 800 KB 的 BOOTAA64.EFI。
dd if=/dev/zero of="$STAGE/boot/efi.img" bs=1M count=16 status=none
mkfs.vfat -F 16 -n PROPEROSEFI "$STAGE/boot/efi.img" >/dev/null
mmd -i "$STAGE/boot/efi.img" ::/EFI ::/EFI/BOOT ::/boot ::/boot/grub
mcopy -i "$STAGE/boot/efi.img" "$STAGE/efi/EFI/BOOT/BOOTAA64.EFI" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$STAGE/boot/efi.img" "$STAGE/boot/grub/grub.cfg" ::/boot/grub/grub.cfg

log "生成 ISO9660（El Torito EFI）"
rm -f "$OUT"
xorriso -as mkisofs -o "$OUT" -V PROPEROS250 -iso-level 3 -J -joliet-long \
    -e boot/efi.img -no-emul-boot "$STAGE" >/dev/null

rm -rf "$LIVE"
sha256sum "$OUT" > "$OUT.sha256"
ls -lh "$OUT" "$STAGE/live/rootfs.squashfs"
log "ARM64 ISO 完成：$OUT"
