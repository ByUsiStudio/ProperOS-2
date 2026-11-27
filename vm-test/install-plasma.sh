#!/bin/bash
# 在 ARM64 rootfs 中安装 Plasma Mobile 完整栈
set -e
ROOT=/root/build/rootfs-arm64
LOG=/root/build/plasma-install.log
exec >>"$LOG" 2>&1
echo "===== start $(date) ====="

mkdir -p "$ROOT"/{proc,sys,dev,dev/pts,run,tmp}
mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
mountpoint -q "$ROOT/sys"  || mount -t sysfs sys "$ROOT/sys"
mountpoint -q "$ROOT/dev"  || mount --bind /dev "$ROOT/dev"
mountpoint -q "$ROOT/dev/pts" || mount --bind /dev/pts "$ROOT/dev/pts"
cp -f /etc/resolv.conf "$ROOT/etc/resolv.conf"

printf '#!/bin/sh\nexit 101\n' > "$ROOT/usr/sbin/policy-rc.d"
chmod +x "$ROOT/usr/sbin/policy-rc.d"

cat > "$ROOT/etc/apt/sources.list" <<'EOF'
deb http://mirrors.cloud.tencent.com/debian bookworm main contrib non-free-firmware
deb http://mirrors.cloud.tencent.com/debian bookworm-updates main contrib non-free-firmware
deb http://mirrors.cloud.tencent.com/debian-security bookworm-security main contrib non-free-firmware
EOF

chroot "$ROOT" /bin/bash -c '
export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C
apt-get update || exit 1
echo "--- 开始安装 plasma-mobile ---"
apt-get install -y --no-install-recommends \
    plasma-mobile plasma-workspace-wayland \
    kwin-wayland kwin-common \
    plasma-nm plasma-pa powerdevil \
    plasma-settings maliit-keyboard milou \
    xdg-desktop-portal-kde \
    mesa-utils libgl1-mesa-dri libegl-mesa0 libgbm1 \
    xwayland \
    fonts-noto-cjk fonts-noto-color-emoji fontconfig \
    network-manager dbus-x11 \
    pipewire wireplumber \
    qtwayland5 qml-module-qtquick-controls2 \
    kde-config-mobile-networking
rc=$?
echo "--- apt 返回 $rc ---"
dpkg -l | grep -c "^ii"
exit $rc
'
echo "===== done $(date) rc=$? ====="
