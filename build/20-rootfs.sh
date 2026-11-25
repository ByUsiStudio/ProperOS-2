#!/bin/bash
# 阶段 2：构建 live 根文件系统并注入安装器、容器运行时、内核
source "$(dirname "$0")/00-env.sh"
require debootstrap chroot

if [ ! -d "$ROOTFS/etc" ]; then
    log "debootstrap $DEBIAN_SUITE → $ROOTFS"
    debootstrap --variant=minbase --arch=amd64 \
        --include=systemd,systemd-sysv,udev,dbus,sudo,ca-certificates,iproute2,iputils-ping,\
curl,wget,nano,less,openssh-server,podman,buildah,skopeo,uidmap,slirp4netns,\
fuse-overlayfs,netavark,aardvark-dns,nftables,iptables,busybox-static,parted,dosfstools,e2fsprogs,grub-pc-bin,grub-efi-amd64-bin,grub2-common,\
console-setup,kbd,locales,tzdata,file,rsync,psmisc \
        "$DEBIAN_SUITE" "$ROOTFS" "$DEBIAN_MIRROR"
fi

log "写入系统标识"
cat > "$ROOTFS/etc/properos-release" <<EOF
NAME="ProperOS"
VERSION="2.5"
VERSION_ID="2.5"
PRETTY_NAME="ProperOS 2.5 (Kernel $KVER)"
ID=properos
ID_LIKE=debian
HOME_URL="https://www.properos.cn"
EOF
cp -f "$ROOTFS/etc/properos-release" "$ROOTFS/etc/os-release"
echo "properos" > "$ROOTFS/etc/hostname"

log "部署安装器"
install -D -m 0755 "$PROJECT/installer/install.sh" "$ROOTFS/usr/local/sbin/properos-install"
install -D -m 0644 "$PROJECT/installer/proloader.conf" "$ROOTFS/usr/local/share/properos/proloader.conf" 2>/dev/null || true
install -D -m 0644 "$PROJECT/installer/properos-live-installer.service" \
    "$ROOTFS/etc/systemd/system/properos-live-installer.service"

log "部署容器运行时"
install -D -m 0755 "$PROJECT/container/properos-container-setup" "$ROOTFS/usr/local/sbin/properos-container-setup"
install -D -m 0755 "$PROJECT/container/properos-container" "$ROOTFS/usr/local/bin/properos-container"

log "部署内核"
install -D -m 0644 "$DIST/kernel/vmlinuz-properos-2.5" "$ROOTFS/boot/vmlinuz-properos-2.5"

log "配置 live 启动行为"
# 安装器独占 tty1；tty2 保留交互登录，便于安装器异常时排查
ln -sf /dev/null "$ROOTFS/etc/systemd/system/getty@tty1.service"
mkdir -p "$ROOTFS/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/properos-live-installer.service \
    "$ROOTFS/etc/systemd/system/multi-user.target.wants/properos-live-installer.service"
ln -sf /lib/systemd/system/getty@.service \
    "$ROOTFS/etc/systemd/system/getty.target.wants/getty@tty2.service" 2>/dev/null || true

cat > "$ROOTFS/etc/issue" <<'EOF'

  ██████╗ ██████╗  ██████╗ ██████╗ ███████╗██████╗  ██████╗ ███████╗
  ██╔══██╗██╔══██╗██╔═══██╗██╔══██╗██╔════╝██╔══██╗██╔═══██╗██╔════╝
  ██████╔╝██████╔╝██║   ██║██████╔╝█████╗  ██████╔╝██║   ██║███████╗
  ██╔═══╝ ██╔══██╗██║   ██║██╔══██╗██╔══╝  ██╔══██╗██║   ██║╚════██║
  ██║     ██║  ██║╚██████╔╝██║  ██║███████╗██║  ██║╚██████╔╝███████║
  ╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝

  ProperOS 2.5  ·  Kernel KVER_PLACEHOLDER  ·  \l

EOF
sed -i "s/KVER_PLACEHOLDER/$KVER/" "$ROOTFS/etc/issue"

log "清理 live 环境残留"
rm -rf "$ROOTFS/var/cache/apt/archives/"*.deb
rm -f "$ROOTFS/etc/ssh/ssh_host_"*
: > "$ROOTFS/etc/machine-id"

# live 环境不需要 sshd 与串口登录，避免启动期反复失败与设备超时噪音
ln -sf /dev/null "$ROOTFS/etc/systemd/system/ssh.service"
ln -sf /dev/null "$ROOTFS/etc/systemd/system/serial-getty@ttyS0.service"

log "rootfs 就绪：$(du -sh "$ROOTFS" | cut -f1)"
