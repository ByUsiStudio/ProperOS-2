#!/bin/bash
# 阶段 1：获取并编译 Linux 内核（默认 x86_64 defconfig + 容器所需项）
source "$(dirname "$0")/00-env.sh"
require make gcc bc flex bison cpio xz

mkdir -p "$WORK"
if [ ! -f "$WORK/linux-$KVER/Makefile" ]; then
    log "下载 Linux $KVER"
    TARBALL="$WORK/linux-$KVER.tar.xz"
    for base in "$KERNEL_MIRROR" "https://mirrors.edge.kernel.org/pub/linux/kernel"; do
        url="$base/v${KMAJOR}.x/linux-${KVER}.tar.xz"
        log "尝试 $url"
        if curl -fL --retry 2 --max-time 1800 -o "$TARBALL" "$url"; then break; fi
    done
    [ -s "$TARBALL" ] || die "内核源码下载失败"
    tar -xJf "$TARBALL" -C "$WORK"
fi

cd "$WORK/linux-$KVER"
[ -f .config ] || make defconfig

log "应用 ProperOS 25.0.0 内核配置"
./scripts/config \
  --enable NAMESPACES --enable UTS_NS --enable IPC_NS --enable PID_NS --enable NET_NS --enable USER_NS \
  --enable CGROUPS --enable CGROUP_PIDS --enable CGROUP_FREEZER --enable CGROUP_BPF --enable CGROUP_SCHED --enable MEMCG \
  --enable SECCOMP --enable SECCOMP_FILTER --enable OVERLAY_FS --enable FUSE_FS --enable TMPFS \
  --enable DEVTMPFS --enable DEVTMPFS_MOUNT --enable EXT4_FS --enable BTRFS_FS \
  --enable VETH --enable BRIDGE --enable NETFILTER --enable NF_CONNTRACK --enable NF_NAT \
  --enable IP_NF_NAT --enable IP_NF_FILTER --enable IP_NF_TARGET_MASQUERADE --enable NETFILTER_XT_MATCH_ADDRTYPE \
  --enable BRIDGE_NETFILTER --enable NF_TABLES --enable NF_TABLES_INET --enable NF_TABLES_NETDEV \
  --enable NF_TABLES_IPV4 --enable NF_TABLES_IPV6 --enable NFT_NAT --enable NFT_MASQ --enable NFT_CT \
  --enable NFT_COMPAT --enable NFT_FIB_INET --enable NFT_CHAIN_NAT_IPV4 --enable NFT_CHAIN_NAT_IPV6 \
  --enable NETFILTER_XTABLES --enable NETFILTER_XT_NAT --enable NETFILTER_XT_MATCH_CONNTRACK \
  --enable NETFILTER_XTABLES_LEGACY --enable IP_NF_IPTABLES_LEGACY --enable IP6_NF_IPTABLES_LEGACY \
  --enable NETFILTER_ADVANCED --enable IP_NF_IPTABLES --enable IP_NF_FILTER --enable IP_NF_NAT \
  --enable IP_NF_MANGLE --enable IP_NF_TARGET_MASQUERADE --enable IP6_NF_IPTABLES --enable IP6_NF_FILTER \
  --enable IP6_NF_NAT --enable IP6_NF_TARGET_MASQUERADE --enable NETFILTER_XT_TARGET_MASQUERADE \
  --enable NETFILTER_XT_MARK --enable NETFILTER_XT_MATCH_MULTIPORT \
  --enable NETFILTER_XT_TARGET_CHECKSUM --enable NETFILTER_XT_TARGET_REDIRECT --enable NETFILTER_XT_TARGET_DNAT \
  --enable NETFILTER_XT_TARGET_SNAT --enable NETFILTER_XT_MATCH_COMMENT --enable NETFILTER_XT_MATCH_ADDRTYPE \
  --enable NETFILTER_XT_MATCH_LIMIT --enable NETFILTER_XT_MATCH_STATE --enable NETFILTER_XT_MATCH_TCPMSS \
  --enable NETFILTER_XT_TARGET_TCPMSS --enable NETFILTER_XT_TARGET_LOG --enable NETFILTER_XT_TARGET_REJECT \
  --enable IP_NF_TARGET_REDIRECT --enable IP_NF_TARGET_REJECT --enable NFT_REDIR --enable NFT_REJECT \
  --enable BLK_DEV_INITRD --enable RD_XZ --enable RD_GZIP \
  --enable EFI --enable EFI_STUB --enable VIRTIO --enable VIRTIO_PCI --enable VIRTIO_BLK --enable VIRTIO_NET \
  --enable VIRTIO_CONSOLE --enable SCSI_VIRTIO --enable ATA --enable SATA_AHCI --enable BLK_DEV_NVME \
  --enable USB_XHCI_HCD --enable USB_HID --enable DRM_SIMPLEDRM --enable FB --enable FRAMEBUFFER_CONSOLE \
  --enable VT --enable SERIAL_8250 --enable SERIAL_8250_CONSOLE --enable BINFMT_ELF --enable BINFMT_SCRIPT \
  --enable POSIX_MQUEUE --enable KEYS --enable MODULES --enable INET --enable UNIX --enable PACKET --enable IPV6 \
  --enable BPF_SYSCALL --enable BPF_JIT --enable CGROUP_BPF --enable CGROUP_DEVICE \
  --enable NETFILTER_XT_MATCH_BPF --enable NET_CLS_BPF --enable NET_ACT_BPF --enable BPF_UNPRIV_DEFAULT_OFF \
  --enable TUN --enable MACVLAN --enable IPVLAN --enable DUMMY --enable NFS_FS --enable CIFS \
  --enable ISO9660_FS --enable SQUASHFS --enable SQUASHFS_XZ \
  --disable SOUND --disable MEDIA_SUPPORT --disable WLAN --disable BT --disable INFINIBAND \
  --disable MODULE_SIG --disable DEBUG_INFO >/dev/null

make olddefconfig

for k in CONFIG_OVERLAY_FS CONFIG_USER_NS CONFIG_CGROUPS CONFIG_VETH CONFIG_NF_NAT \
         CONFIG_SQUASHFS CONFIG_VIRTIO_BLK CONFIG_EFI_STUB CONFIG_DRM_SIMPLEDRM; do
    grep -q "^$k=y" .config || die "内核配置缺少 $k"
done

cp .config "$PROJECT/kernel/properos_25.0.0_defconfig"

log "编译内核（-j$JOBS）"
make -j"$JOBS" bzImage

mkdir -p "$DIST/kernel"
cp -f arch/x86/boot/bzImage "$DIST/kernel/vmlinuz-properos-25.0.0"
cp -f .config "$DIST/kernel/config-25.0.0-properos"
log "内核产物: $DIST/kernel/vmlinuz-properos-25.0.0 ($(du -h "$DIST/kernel/vmlinuz-properos-25.0.0" | cut -f1))"
