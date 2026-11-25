#!/usr/bin/env bash
# ProperOS 2.5 Mobile · ARM64 rootfs 装配
set -euo pipefail
source /workspace/properos-2.5/build/00-env.sh

ROOT="$ROOTFS_ARM64"
ARCH=arm64
export ARCH

MIRROR="${DEBIAN_MIRROR_ARM64:-http://mirrors.cloud.tencent.com/debian}"
PKGS="systemd,systemd-sysv,udev,dbus,sudo,ca-certificates,iproute2,iputils-ping,curl,wget,nano,less,\
openssh-server,podman,buildah,skopeo,uidmap,slirp4netns,fuse-overlayfs,netavark,aardvark-dns,\
nftables,iptables,busybox-static,parted,dosfstools,e2fsprogs,grub-efi-arm64-bin,grub2-common,\
console-setup,kbd,locales,tzdata,file,rsync,psmisc,procps,kmod,util-linux,tar,gzip,xz-utils,zstd,whiptail"

if [ ! -f "$ROOT/etc/debian_version" ]; then
    log "debootstrap arm64 → $ROOT"
    rm -rf "$ROOT"; mkdir -p "$ROOT"
    debootstrap --variant=minbase --arch=arm64 --foreign --include="$PKGS" \
        bookworm "$ROOT" "$MIRROR" >"$WORK/debootstrap-arm64.log" 2>&1
    cp /usr/bin/qemu-aarch64-static "$ROOT/"
    chroot "$ROOT" /debootstrap/debootstrap --second-stage >"$WORK/debootstrap-arm64-second.log" 2>&1
fi

log "挂载伪文件系统"
mkdir -p "$ROOT"/{proc,sys,dev,dev/pts,run}
mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
mountpoint -q "$ROOT/sys"  || mount -t sysfs sys "$ROOT/sys"
mountpoint -q "$ROOT/dev"  || mount --bind /dev "$ROOT/dev"
mountpoint -q "$ROOT/dev/pts" || mount --bind /dev/pts "$ROOT/dev/pts"
cp -f /etc/resolv.conf "$ROOT/etc/resolv.conf"

# 安装器界面依赖
if [ ! -x "$ROOT/usr/bin/whiptail" ]; then
    log "安装 whiptail"
    chroot "$ROOT" bash -c "export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && apt-get install -y -qq whiptail" || warn "whiptail 安装失败"
fi

log "安装 ProperOS 组件"
make -C "$REPO/src" ARCH=arm64 -s -j"$JOBS"
make -C "$REPO/src" ARCH=arm64 install DESTDIR="$ROOT"

log "复制系统骨架"
cp -a "$REPO/overlay/." "$ROOT/"
chmod 0755 "$ROOT/usr/lib/properos/firstboot" "$ROOT/usr/lib/properos/proloader" \
            "$ROOT/usr/lib/properos/stboot" 2>/dev/null || true

install -Dm755 "$REPO/installer/install.sh" "$ROOT/usr/local/sbin/properos-install"
install -Dm755 "$REPO/installer/tty-console.sh" "$ROOT/usr/local/sbin/properos-tty-console"
install -Dm644 "$REPO/installer/proloader.conf" "$ROOT/etc/properos/proloader.conf" 2>/dev/null || true

log "安装内核与 initramfs"
install -Dm644 "$DIST/kernel/Image-properos-2.5" "$ROOT/boot/Image-properos-2.5"
install -Dm644 "$DIST/kernel/config-2.5.0-properos-arm64" "$ROOT/boot/config-2.5.0-properos"

bash "$REPO/build/arch/arm64/30-initramfs.sh"
install -Dm644 "$WORK/initramfs-arm64.cpio.gz" "$ROOT/boot/initramfs-properos.img"

log "系统默认设置"
echo properos > "$ROOT/etc/hostname"
cat > "$ROOT/etc/hosts" <<'EOF'
127.0.0.1   localhost
127.0.1.1   properos
::1         localhost ip6-localhost ip6-loopback
EOF

echo "LANG=zh_CN.UTF-8" > "$ROOT/etc/default/locale"
cat > "$ROOT/etc/fstab" <<'EOF'
# ProperOS 2.5 Mobile
LABEL=properos-system  /          ext4   defaults,noatime              0 1
LABEL=properos-data    /data      ext4   defaults,noatime,nosuid,nodev 0 2
LABEL=PROPEROS         /boot/efi  vfat   umask=0077                    0 2
tmpfs                  /tmp       tmpfs  defaults,nosuid,nodev,mode=1777 0 0
EOF

# 默认 root、无密码、tty1 自动登录（与安装器“留空”策略一致）
chroot "$ROOT" passwd -d root >/dev/null 2>&1 || true
if [ -f "$ROOT/etc/pam.d/common-auth" ] && ! grep -q "pam_unix.so.*nullok" "$ROOT/etc/pam.d/common-auth"; then
    sed -i 's/\(pam_unix\.so\)/\1 nullok/' "$ROOT/etc/pam.d/common-auth" || true
fi
mkdir -p "$ROOT/etc/systemd/system/getty@tty1.service.d"
cat > "$ROOT/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF
chroot "$ROOT" usermod -s /bin/bash root >/dev/null 2>&1 || true

# SSH 交由 properd 管理，屏蔽 systemd 自带单元，避免端口冲突
ln -sf /dev/null "$ROOT/etc/systemd/system/ssh.service"
ln -sf /dev/null "$ROOT/etc/systemd/system/sshd.service"

log "启用服务"
W="$ROOT/etc/systemd/system/multi-user.target.wants"
mkdir -p "$W" "$ROOT/etc/systemd/system/sockets.target.wants" "$ROOT/etc/systemd/system/getty.target.wants"
for unit in properd.service properos-shell.service properos-firstboot.service properos-console.service systemd-networkd.service; do
    [ -f "$ROOT/usr/lib/systemd/system/$unit" ] && ln -sf "/usr/lib/systemd/system/$unit" "$W/"
done
ln -sf /usr/lib/systemd/system/systemd-networkd.socket "$ROOT/etc/systemd/system/sockets.target.wants/" 2>/dev/null || true
for tty in ttyAMA0 ttyS0; do
    ln -sf /usr/lib/systemd/system/serial-getty@.service "$ROOT/etc/systemd/system/getty.target.wants/serial-getty@$tty.service" 2>/dev/null || true
done

log "清理"
rm -f "$ROOT/qemu-aarch64-static"
rm -rf "$ROOT"/var/cache/apt/archives/*.deb "$ROOT"/var/lib/apt/lists/*
rm -f "$ROOT"/etc/ssh/ssh_host_* "$ROOT/etc/machine-id"
: > "$ROOT/etc/machine-id"
rm -f "$ROOT/etc/systemd/system/multi-user.target.wants/properos-live-installer.service"

log "卸载伪文件系统"
umount -R "$ROOT/dev/pts" 2>/dev/null || true
umount -R "$ROOT/dev" 2>/dev/null || true
umount -R "$ROOT/proc" 2>/dev/null || true
umount -R "$ROOT/sys" 2>/dev/null || true

du -sh "$ROOT"
log "ARM64 rootfs 完成"
