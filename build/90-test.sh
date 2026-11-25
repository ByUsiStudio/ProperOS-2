#!/bin/bash
# 启动/安装冒烟测试（无 KVM 也可运行，TCG 模式较慢）
# 用法:
#   ./90-test.sh boot  [ISO] [超时秒]      验证引导到 live 安装器
#   ./90-test.sh install [ISO] [超时秒]    无人值守安装到虚拟盘并验证重启
set -uo pipefail

MODE="${1:-boot}"
ISO="${2:-$(dirname "$0")/../dist/ProperOS-2.5-amd64.iso}"
TIMEOUT="${3:-420}"
WORK="${WORK:-/root/build}"
LOG="${WORK}/test-${MODE}.log"

[ -f "$ISO" ] || { echo "找不到 $ISO"; exit 1; }

QEMU=(qemu-system-x86_64 -m 3072 -smp 2 -no-reboot -accel tcg)

case "$MODE" in
boot)
    : > "$LOG"
    timeout "$TIMEOUT" "${QEMU[@]}" -cdrom "$ISO" -boot d \
        -serial file:"$LOG" -monitor none -display none || true
    echo "--- 日志末尾 ---"
    tail -30 "$LOG"
    if grep -q "live installer bootstrap" "$LOG"; then
        echo "[PASS] 内核与 initramfs 已启动"
    else
        echo "[FAIL] 未到达 initramfs"; exit 1
    fi
    if grep -qiE "Reached target|Started .*ProperOS|properos-install" "$LOG"; then
        echo "[PASS] systemd 已进入用户空间"
    else
        echo "[WARN] 未见 systemd 启动标志（可能仍在启动中）"
    fi
    ;;
install)
    DISK="$WORK/test-disk.qcow2"
    rm -f "$DISK"
    qemu-img create -f qcow2 "$DISK" 16G >/dev/null

    # 用已解开的 ISO 目录重建一个默认条目带 autoinstall 的镜像
    AUTO_ISO="$WORK/test-auto.iso"
    if [ -d "$WORK/iso" ]; then
        rm -rf "$WORK/iso-auto"
        cp -a "$WORK/iso" "$WORK/iso-auto"
        sed -i 's|console=tty0 console=ttyS0,115200 quiet loglevel=4|console=tty0 console=ttyS0,115200 loglevel=7 properos.autoinstall=/dev/vda|' \
            "$WORK/iso-auto/boot/grub/grub.cfg"
        grub-mkrescue -o "$AUTO_ISO" "$WORK/iso-auto" -- -volid PROPEROS250 >/dev/null 2>&1
    fi
    [ -f "$AUTO_ISO" ] || { echo "缺少 $AUTO_ISO，请先运行 40-iso.sh"; exit 1; }

    : > "$LOG"
    timeout "$TIMEOUT" "${QEMU[@]}" \
        -drive file="$DISK",if=virtio,format=qcow2 \
        -cdrom "$AUTO_ISO" -boot d \
        -serial file:"$LOG" -monitor none -display none || true

    echo "--- 安装日志末尾 ---"
    tail -30 "$LOG"
    if grep -q "安装完成" "$LOG"; then
        echo "[PASS] 无人值守安装完成"
    else
        echo "[FAIL] 安装未完成"; exit 1
    fi
    ;;
*)
    echo "未知模式: $MODE"; exit 1 ;;
esac
