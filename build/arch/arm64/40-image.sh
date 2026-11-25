#!/usr/bin/env bash
# ProperOS 2.5 Mobile · ARM64 磁盘镜像（GPT + ESP + system + data）
# 无需 loop 挂载：mke2fs -d / mtools 直接填充文件系统
set -euo pipefail
source /workspace/properos-2.5/build/00-env.sh

ROOT="$ROOTFS_ARM64"
OUT="$DIST/ProperOS-2.5-arm64.img"
STAGE="$WORK/img-stage-arm64"
ESP_IMG="$STAGE/esp.img"
SYS_IMG="$STAGE/system.img"
DATA_IMG="$STAGE/data.img"

ESP_MIB=256
SYS_MIB=3072
DATA_MIB=768
TOTAL_MIB=$((1 + ESP_MIB + SYS_MIB + DATA_MIB))

SYS_PARTUUID="1A2B3C4D-0002-4A2B-9C3D-000000000002"
ESP_PARTUUID="1A2B3C4D-0001-4A2B-9C3D-000000000001"
DATA_PARTUUID="1A2B3C4D-0003-4A2B-9C3D-000000000003"

rm -rf "$STAGE"; mkdir -p "$STAGE/esp"

log "准备 ESP 内容"
mkdir -p "$STAGE/esp"/{EFI/BOOT,boot/grub,slots/a,slots/b,proloader}
cp -f "$REPO/overlay/usr/lib/properos/boot/BOOTAA64.EFI" "$STAGE/esp/EFI/BOOT/BOOTAA64.EFI"
cp -f "$DIST/kernel/Image-properos-2.5" "$STAGE/esp/slots/a/Image"
cp -f "$ROOT/boot/initramfs-properos.img" "$STAGE/esp/slots/a/initramfs.img"
( cd "$STAGE/esp/slots/a" && sha256sum Image initramfs.img > SHA256SUMS )

# 可选设备树：真机（U-Boot / 无 UEFI 内建 DTB 的板子）用 PROPEROS_DTB 指定
DTB_ARG=""
if [[ -n "${PROPEROS_DTB:-}" ]]; then
    [[ -f "$PROPEROS_DTB" ]] || { echo "PROPEROS_DTB 不存在：$PROPEROS_DTB" >&2; exit 1; }
    mkdir -p "$STAGE/esp/dtb"
    cp -f "$PROPEROS_DTB" "$STAGE/esp/dtb/board.dtb"
    DTB_ARG="devicetree /dtb/board.dtb"
    log "设备树：$PROPEROS_DTB → /dtb/board.dtb"
fi

cat > "$STAGE/esp/boot/grub/grub.cfg" <<EOF
# ProperOS 2.5 Mobile · ARM64（镜像内置）
set timeout=3
set default=0
set gfxpayload=keep

menuentry "ProperOS 2.5 Mobile  ·  槽 A" --class properos {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/a/Image root=PARTUUID=$SYS_PARTUUID rootfstype=ext4 rw rootwait quiet loglevel=4 console=ttyAMA0,115200 console=tty0 properos.slot=a
$DTB_ARG
    initrd /slots/a/initramfs.img
}

menuentry "ProperOS 2.5 Mobile  ·  槽 A（详细日志）" {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/a/Image root=PARTUUID=$SYS_PARTUUID rootfstype=ext4 rw rootwait loglevel=7 console=ttyAMA0,115200 console=tty0 properos.slot=a
$DTB_ARG
    initrd /slots/a/initramfs.img
}

menuentry "Recovery shell（单用户）" {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/a/Image root=PARTUUID=$SYS_PARTUUID rootfstype=ext4 rw rootwait loglevel=7 console=ttyAMA0,115200 console=tty0 properos.slot=a init=/bin/bash
$DTB_ARG
    initrd /slots/a/initramfs.img
}
EOF

cat > "$STAGE/esp/proloader/proloader.conf" <<EOF
# ProLoader 配置 · ProperOS 2.5 Mobile
slots=a,b
default_slot=a
max_tries=3
verify=1
splash_message=ProperOS 2.5 Mobile · ByUsi Studio
system_partuuid=$SYS_PARTUUID
EOF

cat > "$STAGE/esp/proloader/slotstate" <<EOF
active=a
a_tries=0
b_tries=0
a_good=1
b_good=0
last_slot=a
EOF

log "生成文件系统镜像"
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_MIB status=none
mkfs.vfat -F32 -n PROPEROS -i 50524f50 "$ESP_IMG" >/dev/null
mcopy -s -i "$ESP_IMG" "$STAGE/esp"/* ::

dd if=/dev/zero of="$SYS_IMG" bs=1M count=$SYS_MIB status=none
mke2fs -q -t ext4 -L properos-system -U 2a2b3c4d-0002-4a2b-9c3d-000000000002 -d "$ROOT" "$SYS_IMG"

mkdir -p "$STAGE/data-seed"
: > "$STAGE/data-seed/.first-boot"
dd if=/dev/zero of="$DATA_IMG" bs=1M count=$((DATA_MIB - 2)) status=none
mke2fs -q -t ext4 -L properos-data -U 2a2b3c4d-0003-4a2b-9c3d-000000000003 -d "$STAGE/data-seed" "$DATA_IMG"

log "组装 GPT 磁盘（${TOTAL_MIB} MiB）"
rm -f "$OUT"
truncate -s "${TOTAL_MIB}M" "$OUT"
sgdisk -a 2048 \
    -n "1:1M:+${ESP_MIB}M" -t 1:ef00 -c 1:properos-esp -u "1:$ESP_PARTUUID" \
    -n "2:$((1 + ESP_MIB))M:+${SYS_MIB}M" -t 2:8300 -c 2:properos-system -u "2:$SYS_PARTUUID" \
    -n "3:$((1 + ESP_MIB + SYS_MIB))M:0" -t 3:8300 -c 3:properos-data -u "3:$DATA_PARTUUID" \
    "$OUT" >/dev/null

dd if="$ESP_IMG"  of="$OUT" bs=1M seek=1 conv=notrunc status=none
dd if="$SYS_IMG"  of="$OUT" bs=1M seek=$((1 + ESP_MIB)) conv=notrunc status=none
dd if="$DATA_IMG" of="$OUT" bs=1M seek=$((1 + ESP_MIB + SYS_MIB)) conv=notrunc status=none

log "校验分区表"
sgdisk -p "$OUT" | sed -n '1,12p'

sha256sum "$OUT" > "$OUT.sha256"
ls -lh "$OUT"
log "ARM64 镜像完成：$OUT"
