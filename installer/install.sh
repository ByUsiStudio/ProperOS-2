#!/bin/bash
# ProperOS 2.5 Mobile · 安装程序
#
# 界面风格参考 Debian Installer（whiptail，蓝底灰框，分步向导）。
# 默认账户为 root，root 密码允许留空：留空时不设密码并在 tty1 自动登录。
# 支持无人值守：properos-install --disk /dev/vda --yes [--password X]
# 也支持内核参数：properos.autoinstall=/dev/vda [properos.blankpass=1]
#
# 开发者：ByUsi Studio · 北啊呢

set -Euo pipefail

VERSION="2.5"
LOG=/var/log/properos-install.log
MNT=/target
ESP_MNT=/target/boot/efi
LIVE_ROOT="${LIVE_ROOT:-/}"
ARCH="${ARCH:-$(uname -m)}"
case "$ARCH" in
    aarch64|arm64) ARCH=arm64 ;;
    x86_64|amd64)  ARCH=amd64 ;;
esac

DISK=""
HOSTNAME_T="properos"
ROOT_PASS=""
AUTO=0
FORCE=0
TTY_UI=0
[ -t 0 ] && [ -t 1 ] && TTY_UI=1

export DEBIAN_FRONTEND=noninteractive
# 界面必须是纯 ASCII，并且强制 C 语言环境：whiptail 在 UTF-8 locale 下会用
# UTF-8 制表符画边框，而控制台字体只有 CP437 字位，边框会变成 âöé 之类乱码。
# 用 LC_ALL=C 后 ncurses 改走 ACS 线条字符，配合 16x32 的 VGA 布局字体，
# 边框是连续实线，屏幕栅格 80x25，与 Debian Installer 一致。
export LANG=C LC_ALL=C TERM=linux

# 串口双写：无屏设备/自动化测试可在串口看到完整过程。
# 按架构挑一个真实存在的串口，避免在不存在的设备上留一个失败的 tee 进程。
if [ "$ARCH" = "arm64" ]; then SERIAL_TTY=/dev/ttyAMA0; else SERIAL_TTY=/dev/ttyS0; fi
if [ -w "$SERIAL_TTY" ] && [ -c "$SERIAL_TTY" ]; then
    exec > >(tee -a "$SERIAL_TTY" 2>/dev/null) 2>&1
fi

trap 'rc=$?; [ $rc -ne 0 ] && { printf "\n[error] installation failed: line %s, exit code %s\n" "$LINENO" "$rc"; \
      case "$(cat /proc/cmdline 2>/dev/null)" in *properos.debug*) exec /bin/bash ;; esac; }' ERR

# ---------------- 输出 ----------------

c_ok()   { printf '\033[1;32m  [ok]\033[0m %s\n' "$*"; }
c_info() { printf '\033[1;36m  [..] %s\033[0m\n' "$*"; }
c_warn() { printf '\033[1;33m  [!!] %s\033[0m\n' "$*"; }
c_err()  { printf '\033[1;31m  [xx] %s\033[0m\n' "$*" >&2; }
log()    { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*" >>"$LOG"; }

die() {
    c_err "$*"
    log "FATAL: $*"
    if [ "$TTY_UI" = 1 ] && command -v whiptail >/dev/null; then
        exec 3>&- 2>/dev/null || true
        whiptail --title "Installation failed" --backtitle "$BACKTITLE" \
                 --msgbox "$*\n\nSee $LOG for details." 14 70 1>&9 2>&9 9>&-
    fi
    exit 1
}

BACKTITLE="ProperOS $VERSION Mobile Installer   |   ByUsi Studio"

# ---------------- 交互封装 ----------------

# whiptail 把界面画在 stdout 上。若调用方用 $(...) 取返回值，stdout 就变成管道，
# 界面会整块画不出来（表现为上一个对话框卡住不动）。所以凡是需要取返回值的，
# 必须做 fd 交换：3>&1 1>&2 2>&3 —— 界面走 tty，返回值走原来的管道。
ui_msg()     { whiptail --backtitle "$BACKTITLE" --title "$1" --msgbox "$2" "${3:-16}" "${4:-72}"; }
ui_yesno()   { whiptail --backtitle "$BACKTITLE" --title "$1" --yesno "$2" "${3:-14}" "${4:-72}"; }
ui_input()   { whiptail --backtitle "$BACKTITLE" --title "$1" --inputbox "$2" "${3:-12}" "${4:-72}" "${5:-}" 3>&1 1>&2 2>&3; }
ui_password(){ whiptail --backtitle "$BACKTITLE" --title "$1" --passwordbox "$2" "${3:-12}" "${4:-72}" 3>&1 1>&2 2>&3; }

# ---------------- 进度 ----------------

gauge_open() {
    # 保存终端（fd 9）：进度条阶段要把普通输出全部改道到日志，
    # 否则 c_info 之类的文字会直接画在 whiptail 的进度框上，把界面糊掉。
    exec 9>&1
    if [ "$TTY_UI" = 1 ] && command -v whiptail >/dev/null; then
        exec 3> >(whiptail --backtitle "$BACKTITLE" --title "Installing the system" \
                           --gauge "Copying the system, please wait..." 10 74 0)
        exec >>"$LOG" 2>&1
    else
        exec 3>&1
    fi
}
gauge() {
    local pct="$1"; shift
    if [ "$TTY_UI" = 1 ] && command -v whiptail >/dev/null; then
        # whiptail --gauge 只吃百分比行（文本行会被当成百分比解析失败而丢弃），
        # 所以说明文字用固定的 --gauge 文案，进度只发数字最稳。
        printf '%s\n' "$pct" >&3
    else
        printf '  [%3d%%] %s\n' "$pct" "$*"
    fi
    log "$pct% $*"
}
gauge_close() {
    exec 3>&- 2>/dev/null || true
    # 恢复终端输出，后面的提示（含失败对话框）才能显示
    [ -e /dev/fd/9 ] && exec 1>&9 2>&9 9>&-
    return 0
}

# ---------------- 工具 ----------------

part_dev() {
    local disk="$1" idx="$2"
    case "$disk" in
        *[0-9]) echo "${disk}p${idx}" ;;
        *)      echo "${disk}${idx}" ;;
    esac
}

# 找到"纯净"的只读系统来源。
# 绝不能从 "/"（live 的 overlay 运行根）拷贝：那里带着 live 专用改动
# （被 mask 的单元、空的 /etc/fstab、/ro 与 /overlay 挂载点），装到设备上会直接坏掉。
find_live_source() {
    local d img
    for d in /media/live-root /run/live/rootfs /run/properos/rootfs; do
        [ -d "$d/etc" ] && [ -d "$d/usr" ] && { echo "$d"; return; }
    done
    # 兜底：自己把安装介质上的 squashfs 挂起来，取最干净的原始系统
    for img in /media/cdrom/live/rootfs.squashfs /media/cdrom/live/*.squashfs; do
        [ -f "$img" ] || continue
        mkdir -p /run/properos/live-ro
        if mount -t squashfs -o ro,loop "$img" /run/properos/live-ro 2>/dev/null; then
            [ -d /run/properos/live-ro/etc ] && { echo /run/properos/live-ro; return; }
            umount /run/properos/live-ro 2>/dev/null || true
        fi
    done
    echo "$LIVE_ROOT"
}

require_tools() {
    for t in parted mkfs.ext4 mkfs.fat tar blkid; do
        command -v "$t" >/dev/null || die "Required tool missing: $t"
    done
}

list_disks() {
    # 只列真正的整盘：排除 loop / rom / sr / nbd / ram / zram，
    # 并丢掉容量为 0 的占位设备（内核编译进去但没接后端的 nbd、ram 等）。
    lsblk -dpno NAME,SIZE,TYPE,MODEL 2>/dev/null |
        awk '$3 == "disk" && $2 != "0B" {
                 name = $1; sub(/^\/dev\//, "", name)
                 if (name ~ /^(loop|ram|zram|nbd|sr|fd)/) next
                 model = ""
                 if (NF > 3) { model = $4; for (i = 5; i <= NF; i++) model = model " " $i }
                 printf "%s|%s %s\n", $1, $2, model
             }'
}

# ---------------- 分区 ----------------

partition_disk() {
    local disk="$1" total_mib
    total_mib=$(blockdev --getsize64 "$disk" 2>/dev/null || echo 0)
    total_mib=$((total_mib / 1024 / 1024))
    [ "$total_mib" -ge 6000 ] || die "Disk too small (${total_mib} MiB), need at least 6 GiB"

    local sys_mib=4096
    [ "$total_mib" -lt 12000 ] && sys_mib=3072
    [ "$total_mib" -lt 8000 ]  && sys_mib=2048

    log "partition: disk=$disk total=${total_mib}MiB system=${sys_mib}MiB arch=$ARCH"

    wipefs -a "$disk" >/dev/null 2>&1 || true
    sgdisk --zap-all "$disk" >/dev/null 2>&1 || true
    parted -s "$disk" mklabel gpt

    local idx=1 esp_start=1
    if [ "$ARCH" = "amd64" ]; then
        # GPT + Legacy BIOS 需要 bios_grub 分区嵌入 core.img
        parted -s "$disk" mkpart bios-boot 1MiB 2MiB
        parted -s "$disk" set 1 bios_grub on
        idx=2; esp_start=2
    fi

    local esp_end=$((esp_start + 256))
    local sys_start=$esp_end
    local sys_end=$((sys_start + sys_mib))

    parted -s "$disk" mkpart properos-esp fat32 "${esp_start}MiB" "${esp_end}MiB"
    parted -s "$disk" set "$idx" esp on
    local esp_idx=$idx; idx=$((idx + 1))

    parted -s "$disk" mkpart properos-system ext4 "${sys_start}MiB" "${sys_end}MiB"
    local sys_idx=$idx; idx=$((idx + 1))

    parted -s "$disk" mkpart properos-data ext4 "${sys_end}MiB" 100%
    local data_idx=$idx

    partprobe "$disk" 2>/dev/null || true
    udevadm settle 2>/dev/null || sleep 2

    ESP=$(part_dev "$disk" "$esp_idx")
    SYSTEM=$(part_dev "$disk" "$sys_idx")
    DATA=$(part_dev "$disk" "$data_idx")

    for p in "$ESP" "$SYSTEM" "$DATA"; do
        [ -b "$p" ] || die "Partition not created: $p"
    done
    sync
    log "partitions: ESP=$ESP SYSTEM=$SYSTEM DATA=$DATA"
}

format_partitions() {
    c_info "Formatting ESP ($ESP)"
    mkfs.fat -F32 -n PROPEROS "$ESP" >/dev/null || die "mkfs.fat failed"

    c_info "Formatting system ($SYSTEM)"
    mkfs.ext4 -q -L properos-system -F "$SYSTEM" || die "mkfs.ext4 failed"

    c_info "Formatting data ($DATA)"
    mkfs.ext4 -q -L properos-data -F "$DATA" || die "mkfs.ext4 failed"
}

mount_target() {
    mkdir -p "$MNT"
    mount "$SYSTEM" "$MNT" || die "Cannot mount system partition"
    mkdir -p "$MNT/data"
    mount "$DATA" "$MNT/data" || die "Cannot mount data partition"
    mkdir -p "$ESP_MNT"
    mount "$ESP" "$ESP_MNT" || die "Cannot mount ESP"
}

# ---------------- 复制 ----------------

copy_system() {
    local src
    src=$(find_live_source)
    c_info "Copying system from $src"
    log "copy source: $src"
    case "$src" in
        /run/properos/live-ro*|/media/live-root*|/run/live/rootfs*) : ;;
        *) c_warn "copying from the live overlay root; masked units may leak into the target" ;;
    esac

    ( cd "$src" && tar -cf - \
        --exclude=./proc --exclude=./sys --exclude=./dev --exclude=./run \
        --exclude=./tmp --exclude=./target --exclude=./media --exclude=./mnt \
        --exclude=./ro --exclude=./overlay --exclude=./newroot \
        --exclude=./var/cache/apt/archives --exclude=./var/lib/apt/lists \
        --exclude=./data --exclude=./boot/efi \
        . ) | ( cd "$MNT" && tar -xpf - --warning=no-timestamp ) || die "System copy failed"

    for d in proc sys dev dev/pts run tmp media mnt root home var/tmp var/log boot/efi; do
        mkdir -p "$MNT/$d"
    done
    chmod 1777 "$MNT/tmp" "$MNT/var/tmp"
    chmod 0700 "$MNT/root"
}

# ---------------- 配置 ----------------

setup_identity() {
    echo "$HOSTNAME_T" > "$MNT/etc/hostname"
    cat > "$MNT/etc/hosts" <<EOF
127.0.0.1   localhost
127.0.1.1   $HOSTNAME_T
::1         localhost ip6-localhost ip6-loopback
EOF

    : > "$MNT/etc/machine-id"
    rm -f "$MNT/var/lib/dbus/machine-id" 2>/dev/null || true

    cat > "$MNT/etc/locale.gen" <<EOF
en_US.UTF-8 UTF-8
zh_CN.UTF-8 UTF-8
EOF
    echo "LANG=zh_CN.UTF-8" > "$MNT/etc/default/locale"
    echo "zh_CN.UTF-8" > "$MNT/etc/locale.conf" 2>/dev/null || true

    cat > "$MNT/etc/resolv.conf" <<EOF
nameserver 223.5.5.5
nameserver 1.1.1.1
EOF
}

setup_root() {
    if [ -z "$ROOT_PASS" ]; then
        chroot "$MNT" passwd -d root >/dev/null 2>&1 || true
        # PAM 允许空密码（Debian 默认有 nullok，缺失时补上）
        if [ -f "$MNT/etc/pam.d/common-auth" ] && ! grep -q "pam_unix.so.*nullok" "$MNT/etc/pam.d/common-auth"; then
            sed -i 's/\(pam_unix\.so\)/\1 nullok/' "$MNT/etc/pam.d/common-auth" 2>/dev/null || true
        fi

        mkdir -p "$MNT/etc/systemd/system/getty@tty1.service.d"
        cat > "$MNT/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF
        cat > "$MNT/etc/issue" <<'EOF'
ProperOS 2.5 Mobile  \n \l
root password is empty: login without password (set at install time)
EOF
    else
        printf 'root:%s\n' "$ROOT_PASS" | chroot "$MNT" chpasswd || die "Cannot set root password"
        rm -rf "$MNT/etc/systemd/system/getty@tty1.service.d" 2>/dev/null || true
    fi
    chroot "$MNT" usermod -s /bin/bash root >/dev/null 2>&1 || true
}

setup_fstab() {
    local sys_uuid data_uuid esp_uuid
    sys_uuid=$(blkid -s UUID -o value "$SYSTEM")
    data_uuid=$(blkid -s UUID -o value "$DATA")
    esp_uuid=$(blkid -s UUID -o value "$ESP")

    cat > "$MNT/etc/fstab" <<EOF
# ProperOS 2.5 Mobile - generated by installer
UUID=$sys_uuid   /          ext4   defaults,noatime              0 1
UUID=$data_uuid  /data      ext4   defaults,noatime,nosuid,nodev 0 2
UUID=$esp_uuid   /boot/efi  vfat   umask=0077                    0 2
tmpfs            /tmp       tmpfs  defaults,nosuid,nodev,mode=1777 0 0
EOF
}

setup_boot_files() {
    local slot_dir="$ESP_MNT/slots/a"
    mkdir -p "$slot_dir" "$ESP_MNT/proloader" "$ESP_MNT/boot/grub" "$ESP_MNT/EFI/BOOT" "$ESP_MNT/slots/b"

    local kimg initrd
    if [ "$ARCH" = "arm64" ]; then kimg="/boot/Image-properos-2.5"; else kimg="/boot/vmlinuz-properos-2.5"; fi
    initrd="/boot/initramfs-properos.img"
    [ -f "$kimg" ] || die "Kernel not found: $kimg"
    [ -f "$initrd" ] || die "Initramfs not found: $initrd"

    cp -f "$kimg" "$slot_dir/Image"
    cp -f "$initrd" "$slot_dir/initramfs.img"
    ( cd "$slot_dir" && sha256sum Image initramfs.img > SHA256SUMS )

    # 系统内保留一份，便于 bootctl 升级/救援
    mkdir -p "$MNT/boot"
    cp -f "$kimg" "$MNT/boot/"
    cp -f "$initrd" "$MNT/boot/"
    mkdir -p "$MNT/usr/lib/properos/boot"
    if [ -f /usr/lib/properos/boot/BOOTAA64.EFI ] || [ -f /usr/lib/properos/boot/BOOTX64.EFI ]; then
        cp -f /usr/lib/properos/boot/*.EFI "$MNT/usr/lib/properos/boot/" 2>/dev/null || true
    fi

    local sys_partuuid
    sys_partuuid=$(blkid -s PARTUUID -o value "$SYSTEM")

    cat > "$ESP_MNT/proloader/proloader.conf" <<EOF
# ProLoader configuration - ProperOS 2.5 Mobile
slots=a,b
default_slot=a
max_tries=3
verify=1
splash_message=ProperOS 2.5 Mobile - ByUsi Studio
system_partuuid=$sys_partuuid
EOF

    cat > "$ESP_MNT/proloader/slotstate" <<EOF
active=a
a_tries=0
b_tries=0
a_good=1
b_good=0
last_slot=a
EOF
}

write_grub_cfg() {
    local sys_partuuid
    sys_partuuid=$(blkid -s PARTUUID -o value "$SYSTEM")

    local console="console=ttyAMA0,115200 console=tty0"
    [ "$ARCH" = "amd64" ] && console="console=tty0 console=ttyS0,115200"

    cat > "$ESP_MNT/boot/grub/grub.cfg" <<EOF
# ProperOS 2.5 Mobile - GRUB configuration (generated by installer)
set timeout=3
set default=0
set gfxpayload=keep

menuentry "ProperOS 2.5 Mobile   Slot A" --class properos {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/a/Image root=PARTUUID=$sys_partuuid rootfstype=ext4 rw rootwait quiet loglevel=4 $console properos.slot=a
    initrd /slots/a/initramfs.img
}

menuentry "ProperOS 2.5 Mobile   Slot A (verbose)" {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/a/Image root=PARTUUID=$sys_partuuid rootfstype=ext4 rw rootwait loglevel=7 $console properos.slot=a
    initrd /slots/a/initramfs.img
}

menuentry "ProperOS 2.5 Mobile   Slot B" {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/b/Image root=PARTUUID=$sys_partuuid rootfstype=ext4 rw rootwait quiet $console properos.slot=b
    initrd /slots/b/initramfs.img
}

menuentry "Recovery shell (single user)" {
    search --no-floppy --label --set=root PROPEROS
    linux /slots/a/Image root=PARTUUID=$sys_partuuid rootfstype=ext4 rw rootwait loglevel=7 $console properos.slot=a init=/bin/bash
    initrd /slots/a/initramfs.img
}
EOF
}

install_bootloader() {
    if [ "$ARCH" = "arm64" ]; then
        if [ -f /usr/lib/properos/boot/BOOTAA64.EFI ]; then
            cp -f /usr/lib/properos/boot/BOOTAA64.EFI "$ESP_MNT/EFI/BOOT/BOOTAA64.EFI"
            c_ok "ARM64 UEFI boot loader installed"
        else
            chroot "$MNT" grub-install --target=arm64-efi --efi-directory=/boot/efi \
                --boot-directory=/boot/efi/boot --removable --no-nvram 2>&1 | sed 's/^/    /' | log \
                || c_warn "grub-install arm64-efi failed"
        fi
    else
        cp -f /usr/lib/properos/boot/BOOTX64.EFI "$ESP_MNT/EFI/BOOT/BOOTX64.EFI" 2>/dev/null || true
        chroot "$MNT" grub-install --target=i386-pc --recheck \
            --boot-directory=/boot/efi/boot "$DISK" 2>&1 | sed 's/^/    /' | log \
            || c_warn "BIOS grub-install failed (UEFI-only devices can ignore)"
    fi
}

setup_services() {
    rm -f "$MNT/etc/systemd/system/multi-user.target.wants/properos-live-installer.service"
    rm -f "$MNT/etc/systemd/system/properos-live-installer.service"

    local wants="$MNT/etc/systemd/system/multi-user.target.wants"
    mkdir -p "$wants" "$MNT/etc/systemd/system/sockets.target.wants"

    for unit in properd.service properos-shell.service properos-firstboot.service properos-console.service; do
        [ -f "$MNT/usr/lib/systemd/system/$unit" ] && ln -sf "/usr/lib/systemd/system/$unit" "$wants/"
    done
    ln -sf /usr/lib/systemd/system/systemd-networkd.service "$wants/" 2>/dev/null || true
    ln -sf /usr/lib/systemd/system/systemd-networkd.socket "$MNT/etc/systemd/system/sockets.target.wants/" 2>/dev/null || true
    ln -sf /usr/lib/systemd/system/systemd-resolved.service "$wants/" 2>/dev/null || true

    # 串口 getty 按架构分开：ARM64 设备是 ttyAMA0，x86 是 ttyS0。
    # 两个都开会留下一个永远超时的失败单元（dev-ttyS0.device not found）。
    mkdir -p "$MNT/etc/systemd/system/getty.target.wants"
    local serial_tty="ttyS0"
    [ "$ARCH" = "arm64" ] && serial_tty="ttyAMA0"
    ln -sf /usr/lib/systemd/system/serial-getty@.service \
           "$MNT/etc/systemd/system/getty.target.wants/serial-getty@$serial_tty.service" 2>/dev/null || true
}

cleanup_target() {
    for m in "$ESP_MNT" "$MNT/data" "$MNT"; do
        mountpoint -q "$m" && umount -R "$m" 2>/dev/null || true
    done
    sync
}

# ---------------- 安装 ----------------

do_install() {
    [ "$(id -u)" -eq 0 ] || die "Installer must run as root"
    require_tools
    [ -n "$DISK" ] || die "No target disk"
    [ -b "$DISK" ] || die "Not a block device: $DISK"

    if [ "$FORCE" -ne 1 ] && [ "$AUTO" -ne 1 ]; then
        ui_yesno "Confirm" "All data on $DISK will be destroyed.\n\nContinue?" || exit 1
    fi

    : >"$LOG"
    gauge_open

    gauge 5  "Partitioning $DISK"
    partition_disk "$DISK"

    gauge 15 "Formatting partitions"
    format_partitions

    gauge 25 "Mounting target"
    mount_target

    gauge 35 "Copying system files"
    copy_system

    gauge 65 "Configuring system"
    setup_identity
    setup_root
    setup_fstab
    setup_services

    gauge 78 "Installing boot loader"
    setup_boot_files
    write_grub_cfg
    install_bootloader || c_warn "Boot loader reported warnings (see $LOG)"

    gauge 92 "Finalizing"
    mkdir -p "$MNT/data/apps" "$MNT/data/.trash"
    rm -f "$MNT/data/.first-boot"
    : > "$MNT/data/.first-boot"

    gauge 97 "Syncing"
    sync
    cleanup_target

    gauge 100 "Installation complete"
    gauge_close

    c_ok "ProperOS $VERSION installed on $DISK"
    log "install complete: disk=$DISK arch=$ARCH hostname=$HOSTNAME_T blank_password=$([ -z "$ROOT_PASS" ] && echo yes || echo no)"
}

# ---------------- 向导 ----------------

wizard() {
    command -v whiptail >/dev/null || die "whiptail not installed"

    ui_msg "Welcome" "Welcome to the ProperOS $VERSION Mobile installer.\n\nThis installer copies the live system to internal storage, sets up the boot loader and configures the root account.\n\nDeveloped by Bei A Ni, ByUsi Studio." 16 74

    local menu=() i=0 dev desc
    while IFS='|' read -r dev desc; do
        [ -n "$dev" ] || continue
        menu+=("$dev" "$desc"); i=$((i + 1))
    done < <(list_disks)
    [ "$i" -gt 0 ] || die "No installable disk found"

    DISK=$(whiptail --backtitle "$BACKTITLE" --title "Partition disks" \
        --menu "Select the disk to install ProperOS on.\nAll existing data will be erased." \
        20 76 10 "${menu[@]}" 3>&1 1>&2 2>&3) || exit 1

    HOSTNAME_T=$(ui_input "Hostname" "Enter a hostname for this device:" 12 70 "$HOSTNAME_T") || exit 1
    HOSTNAME_T=${HOSTNAME_T:-properos}

    local p1 p2
    while :; do
        p1=$(ui_password "Root password" "Enter a password for the root account.\n\nLeave empty to install WITHOUT a password (auto-login on tty1)." 14 74) || exit 1
        if [ -z "$p1" ]; then
            ui_yesno "No root password" "You chose to leave the root password empty.\n\nThe system will boot straight to a root shell.\n\nAre you sure?" 14 74 || continue
            ROOT_PASS=""
            break
        fi
        p2=$(ui_password "Confirm password" "Re-enter the root password:" 12 74) || exit 1
        [ "$p1" = "$p2" ] || { ui_msg "Mismatch" "Passwords do not match. Please try again." 10 60; continue; }
        ROOT_PASS="$p1"
        break
    done

    local pwdesc="(empty, auto-login)"
    [ -n "$ROOT_PASS" ] && pwdesc="(set)"

    ui_yesno "Ready to install" \
        "Device       : $DISK\nArchitecture : $ARCH\nHostname     : $HOSTNAME_T\nRoot password: $pwdesc\n\nPartitions:\n  ESP      256 MiB (FAT32, /boot/efi)\n  System   $([ "$ARCH" = arm64 ] && echo '4 GiB' || echo '4 GiB')  (ext4, /)\n  Data     remaining (ext4, /data)\n\nStart installation?" 18 74 || exit 1

    do_install
    ui_msg "Finished" "Installation finished successfully.\n\nYou can now remove the installation media and restart the device." 12 70
    sync
    if [ "$TTY_UI" = 1 ]; then
        ui_yesno "Reboot" "Reboot now?" 10 60 && reboot -f
    fi
}

# ---------------- 参数 ----------------

# 内核参数自动安装
for arg in $(cat /proc/cmdline 2>/dev/null); do
    case "$arg" in
        properos.autoinstall=*) DISK="${arg#*=}"; AUTO=1 ;;
        properos.hostname=*)    HOSTNAME_T="${arg#*=}" ;;
        properos.password=*)    ROOT_PASS="${arg#*=}" ;;
        properos.blankpass=1)   ROOT_PASS="" ;;
    esac
done

while [ $# -gt 0 ]; do
    case "$1" in
        --disk) DISK="$2"; AUTO=1; shift 2 ;;
        --hostname) HOSTNAME_T="$2"; shift 2 ;;
        --password) ROOT_PASS="$2"; shift 2 ;;
        --blank-password) ROOT_PASS=""; shift ;;
        --yes|-y) AUTO=1; shift ;;
        --force) FORCE=1; shift ;;
        --auto) AUTO=1; shift ;;
        --version) echo "ProperOS installer $VERSION"; exit 0 ;;
        --help)
            cat <<EOF
ProperOS $VERSION installer
usage: properos-install [options]
  --disk <device>      target disk (implies unattended mode)
  --password <pass>    root password; omit to leave it empty
  --blank-password     explicitly install without a root password
  --hostname <name>    hostname (default: properos)
  --yes                skip confirmation
  --force              skip the confirmation dialog even in auto mode
EOF
            exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

if [ -n "$DISK" ] && [ "$AUTO" = 1 ]; then
    do_install
else
    wizard
fi
