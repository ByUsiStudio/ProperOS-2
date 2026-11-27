#!/usr/bin/env bash
# ProperOS 25.0.0 Mobile · ARM64 rootfs 装配
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/00-env.sh"

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
    # ---- Plasma Mobile：真正的 KDE 桌面，不是自绘复刻 ----
# 需要构建机能透明执行 aarch64 程序：
#   sudo update-binfmts --import     # 注册 qemu-aarch64
if [ ! -x "$ROOT/usr/bin/startplasmamobile" ]; then
    if [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
        warn "缺少 aarch64 binfmt，无法在 chroot 里跑 apt；先执行 sudo update-binfmts --import"
    fi
    log "安装 Plasma Mobile（Debian bookworm）"
    chroot "$ROOT" /bin/bash -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y --no-install-recommends \
            plasma-mobile plasma-workspace-wayland kwin-wayland kwin-common \
            plasma-nm plasma-pa powerdevil plasma-settings maliit-keyboard milou \
            xdg-desktop-portal-kde mesa-utils libgl1-mesa-dri libegl-mesa0 libgbm1 \
            xwayland fonts-noto-cjk fonts-noto-color-emoji fontconfig \
            network-manager dbus-x11 pipewire wireplumber \
            qtwayland5 qml-module-qtquick-controls2 kde-config-mobile-networking \
            kscreen plasma-mobile-tweaks
    ' >>"$WORK/plasma-arm64.log" 2>&1 || warn "Plasma Mobile 安装失败，见 $WORK/plasma-arm64.log"
fi

log "安装 whiptail"
    chroot "$ROOT" bash -c "export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && apt-get install -y -qq whiptail" || warn "whiptail 安装失败"
fi

log "安装 ProperOS 组件"
# 25.0.0 起应用以 .ppkg 交付（/opt/properos/apps）。旧 rootfs 里的 .jspk 与
# 上一版生成的 .desktop 全部清掉，避免两套格式并存导致桌面出现重复图标。
rm -rf "$ROOT/usr/share/properos/apps"
rm -f "$ROOT"/usr/share/applications/properos-*.desktop
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
install -Dm644 "$DIST/kernel/Image-properos-25.0.0" "$ROOT/boot/Image-properos-25.0.0"
install -Dm644 "$DIST/kernel/config-25.0.0-properos-arm64" "$ROOT/boot/config-25.0.0-properos"

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
# ProperOS 25.0.0 Mobile
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
G="$ROOT/etc/systemd/system/graphical.target.wants"
mkdir -p "$W" "$G" "$ROOT/etc/systemd/system/sockets.target.wants" "$ROOT/etc/systemd/system/getty.target.wants"
# 系统级服务。桌面不进 multi-user.wants：properos-session 由 graphical.target 拉起，
# properos-shell 只在没有 DRM 时由会话脚本自己 exec（或手动 systemctl start）。
# 两者同时 enable 会互相 Conflicts，抢跑的往往是 framebuffer 版，画面就不是 Plasma 了。
for unit in properd.service properos-firstboot.service properos-console.service systemd-networkd.service; do
    [ -f "$ROOT/usr/lib/systemd/system/$unit" ] && ln -sf "/usr/lib/systemd/system/$unit" "$W/"
done
ln -sf /usr/lib/systemd/system/properos-session.service "$G/" 2>/dev/null || true
# 复用旧 rootfs 时清掉历史残留，否则 shell 会抢在会话前启动
rm -f "$W/properos-session.service" "$W/properos-shell.service"
# 默认目标是图形界面，graphical.target.wants 才会被求值
ln -sf /usr/lib/systemd/system/graphical.target "$ROOT/etc/systemd/system/default.target"
ln -sf /usr/lib/systemd/system/systemd-networkd.socket "$ROOT/etc/systemd/system/sockets.target.wants/" 2>/dev/null || true
for tty in ttyAMA0 ttyS0; do
    ln -sf /usr/lib/systemd/system/serial-getty@.service "$ROOT/etc/systemd/system/getty.target.wants/serial-getty@$tty.service" 2>/dev/null || true
done

log "清理"
# 构建机上拷进去的 /etc/resolv.conf 是机房内网 DNS（如 183.60.x.x），
# 用户设备不在那个网络里，装完系统 apt 会「Temporary failure resolving」。
# 收尾必须换回公共 DNS，否则软件源等于不可用。
cat > "$ROOT/etc/resolv.conf" <<'RESOLV'
# ProperOS · 默认公共 DNS
nameserver 223.5.5.5
nameserver 119.29.29.29
nameserver 1.1.1.1
RESOLV
# 时间偏差同样会让 apt 报 Release file 过期，开机自动对时
mkdir -p "$ROOT/etc/systemd/system/sysinit.target.wants"
ln -sf /usr/lib/systemd/system/systemd-timesyncd.service \
       "$ROOT/etc/systemd/system/sysinit.target.wants/systemd-timesyncd.service" 2>/dev/null || true

rm -f "$ROOT/qemu-aarch64-static"
rm -rf "$ROOT"/var/cache/apt/archives/*.deb
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
