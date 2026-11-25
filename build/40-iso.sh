#!/bin/bash
# 阶段 4：生成 squashfs 与可引导 ISO（BIOS + UEFI 双模式）
source "$(dirname "$0")/00-env.sh"
require mksquashfs xorriso grub-mkrescue

rm -rf "$ISODIR"
mkdir -p "$ISODIR"/{boot/grub,live}

log "压缩 rootfs → squashfs（$( [ "${COMP:-xz}" = gzip ] && echo gzip || echo xz )）"
if [ "${COMP:-xz}" = "gzip" ]; then
    SQ_OPTS="-comp gzip"
else
    SQ_OPTS="-comp xz -Xbcj x86"
fi
mksquashfs "$ROOTFS" "$ISODIR/live/rootfs.squashfs" \
    $SQ_OPTS -b 1M -no-xattrs -no-progress \
    -e proc sys dev run tmp var/cache/apt/archives var/lib/apt/lists

install -m 0644 "$DIST/kernel/vmlinuz-properos-2.5" "$ISODIR/boot/vmlinuz"
install -m 0644 "$DIST/initrd.img" "$ISODIR/boot/initrd.img"
install -m 0644 "$PROJECT/installer/grub.cfg" "$ISODIR/boot/grub/grub.cfg"

cat > "$ISODIR/README.txt" <<EOF
ProperOS 2.5 安装介质
内核: Linux $KVER
构建时间: $(date -u +%Y-%m-%dT%H:%M:%SZ)

BIOS/UEFI 均可引导。启动后自动进入安装程序（tty1）。
手动安装: properos-install --disk /dev/sdX
EOF

log "生成 ISO"
grub-mkrescue -o "$DIST/$ISO_NAME" "$ISODIR" \
    -- -volid "$VOLID" -joliet on -rockridge on 2>&1 | tail -5

[ -f "$DIST/$ISO_NAME" ] || die "ISO 生成失败"
( cd "$DIST" && sha256sum "$ISO_NAME" > "$ISO_NAME.sha256" )

log "完成: $DIST/$ISO_NAME ($(du -h "$DIST/$ISO_NAME" | cut -f1))"
