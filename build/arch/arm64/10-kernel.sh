#!/usr/bin/env bash
# ProperOS 2.5 Mobile · ARM64 内核构建
# 目标：Linux 7.3.6 / arm64 / 单 Image（无模块，容器与手机基础驱动内建）
set -euo pipefail
source /workspace/properos-2.5/build/00-env.sh

SRC="${KERNEL_SRC_ARM64:-/root/build/linux-arm64}"
JOBS="${JOBS:-5}"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=byusi KBUILD_BUILD_HOST=properos

if [ ! -f "$SRC/Makefile" ]; then
    log "解压内核源码到 $SRC"
    mkdir -p "$SRC"
    tar -xJf "$CACHE/linux-${KERNEL_VERSION}.tar.xz" -C "$SRC" --strip-components=1
fi

cd "$SRC"
log "生成 arm64 基线配置"
make -s defconfig

log "叠加 ProperOS Mobile 配置"
./scripts/config \
    --disable MODULES --disable MODULE_SIG --disable DEBUG_INFO --disable DEBUG_INFO_DWARF5 \
    --enable NAMESPACES --enable UTS_NS --enable IPC_NS --enable PID_NS --enable NET_NS --enable USER_NS \
    --enable CGROUPS --enable CGROUP_PIDS --enable CGROUP_FREEZER --enable CGROUP_BPF --enable CGROUP_SCHED \
    --enable MEMCG --enable SECCOMP --enable SECCOMP_FILTER --enable BPF_SYSCALL --enable BPF_JIT \
    --enable OVERLAY_FS --enable EROFS_FS --enable EROFS_FS_ZIP --enable TMPFS --enable DEVTMPFS \
    --enable DEVTMPFS_MOUNT --enable EXT4_FS --enable SQUASHFS --enable SQUASHFS_XZ --enable F2FS_FS \
    --enable BLK_DEV_INITRD --enable RD_GZIP \
    --enable EFI --enable EFI_STUB --enable VIRTIO --enable VIRTIO_PCI --enable VIRTIO_MMIO \
    --enable VIRTIO_BLK --enable VIRTIO_NET --enable VIRTIO_CONSOLE --enable VIRTIO_GPU \
    --enable DRM --enable DRM_SIMPLEDRM --enable FB --enable FRAMEBUFFER_CONSOLE --enable VT \
    --enable SERIAL_AMBA_PL011 --enable SERIAL_AMBA_PL011_CONSOLE \
    --enable INPUT_EVDEV --enable INPUT_TOUCHSCREEN \
    --enable TOUCHSCREEN_EDT_FT5X06 --enable TOUCHSCREEN_GOODIX --enable TOUCHSCREEN_ATMEL_MXT \
    --enable BACKLIGHT_CLASS_DEVICE --enable BACKLIGHT_PWM --enable DRM_PANEL_SIMPLE \
    --enable MMC --enable MMC_SDHCI --enable MMC_SDHCI_OF_ARASAN --enable MMC_SDHCI_OF_ESDHC \
    --enable USB_SUPPORT --enable USB --enable USB_GADGET --enable USB_CONFIGFS --enable USB_DWC3 \
    --enable PHY_QCOM_QMP --enable PINCTRL_QCOM --enable PINCTRL_MTK \
    --enable POWER_RESET --enable POWER_RESET_SYSCON --enable REGULATOR --enable QCOM_SPMI_REGULATOR \
    --enable ARCH_QCOM --enable ARCH_MEDIATEK --enable ARCH_ROCKCHIP --enable ARCH_SUNXI \
    --enable PCI --enable PCIE_QCOM --enable PCIE_DW \
    --enable INET --enable UNIX --enable PACKET --enable IPV6 --enable NETFILTER --enable NF_CONNTRACK \
    --enable NF_NAT --enable VETH --enable BRIDGE --enable TUN --enable MACVLAN --enable IPVLAN \
    --enable DUMMY --enable NFS_FS --enable CIFS --enable ISO9660_FS --enable VFAT --enable MSDOS_FS \
    --enable CRYPTO_SHA256 --enable CRYPTO_SHA512 --enable CRYPTO_AES --enable CRYPTO_XTS \
    --enable DM_CRYPT --enable BLK_DEV_DM --enable DM_VERITY --enable KEYS \
    --enable NF_TABLES --enable NF_TABLES_INET --enable NF_TABLES_NETDEV --enable NF_TABLES_IPV4 \
    --enable NF_TABLES_IPV6 --enable NFT_NAT --enable NFT_MASQ --enable NFT_CT --enable NFT_COMPAT \
    --enable NFT_FIB_INET --enable NFT_CHAIN_NAT_IPV4 --enable NFT_CHAIN_NAT_IPV6 \
    --enable NFT_REDIR --enable NFT_REJECT \
    --enable NETFILTER_XTABLES --enable NETFILTER_XTABLES_LEGACY \
    --enable IP_NF_IPTABLES --enable IP_NF_IPTABLES_LEGACY --enable IP6_NF_IPTABLES_LEGACY \
    --enable NETFILTER_ADVANCED --enable IP_NF_FILTER --enable IP_NF_NAT --enable IP_NF_MANGLE \
    --enable IP_NF_TARGET_MASQUERADE --enable IP_NF_TARGET_REDIRECT --enable IP_NF_TARGET_REJECT \
    --enable IP6_NF_FILTER --enable IP6_NF_NAT --enable IP6_NF_TARGET_MASQUERADE \
    --enable NETFILTER_XT_NAT --enable NETFILTER_XT_MATCH_CONNTRACK --enable NETFILTER_XT_MARK \
    --enable NETFILTER_XT_MATCH_MULTIPORT --enable NETFILTER_XT_MATCH_STATE \
    --enable NETFILTER_XT_MATCH_COMMENT --enable NETFILTER_XT_MATCH_ADDRTYPE \
    --enable NETFILTER_XT_MATCH_LIMIT --enable NETFILTER_XT_MATCH_TCPMSS \
    --enable NETFILTER_XT_TARGET_CHECKSUM --enable NETFILTER_XT_TARGET_REDIRECT \
    --enable NETFILTER_XT_TARGET_DNAT --enable NETFILTER_XT_TARGET_SNAT \
    --enable NETFILTER_XT_TARGET_TCPMSS --enable NETFILTER_XT_TARGET_LOG --enable NETFILTER_XT_TARGET_REJECT \
    >/dev/null

make -s olddefconfig

log "关键配置校验"
FAIL=0
for k in CONFIG_OVERLAY_FS CONFIG_EROFS_FS CONFIG_USER_NS CONFIG_CGROUPS CONFIG_VETH \
         CONFIG_NF_TABLES CONFIG_IP_NF_NAT CONFIG_NETFILTER_ADVANCED CONFIG_BPF_SYSCALL \
         CONFIG_DRM_SIMPLEDRM CONFIG_EFI_STUB CONFIG_VIRTIO_BLK CONFIG_F2FS_FS; do
    if grep -q "^$k=y" .config; then
        printf '  %-34s y\n' "$k"
    else
        printf '  %-34s MISSING\n' "$k"; FAIL=1
    fi
done
[ "$FAIL" -eq 0 ] || die "内核配置缺项，终止构建"

log "编译 Image（-j$JOBS）"
time make -s -j"$JOBS" Image
ls -lh arch/arm64/boot/Image

install -Dm644 arch/arm64/boot/Image "$DIST/kernel/Image-properos-2.5"
install -Dm644 .config "$DIST/kernel/config-2.5.0-properos-arm64"
install -Dm644 .config "$REPO/kernel/properos_2.5_arm64_defconfig"
log "ARM64 内核完成：$DIST/kernel/Image-properos-2.5"
