#!/usr/bin/env bash
# ProperOS 25.0.0 Mobile · Secure Boot（MOK）登记与自签
#
# 背景：BOOTAA64.EFI 是自研引导器，没有微软 UEFI CA 签名，因此在
#       Secure Boot 开启的机器上必须把自己的密钥登记进固件（MOK），
#       再把引导器用这把密钥签一遍。做完这两步就不用关 Secure Boot。
#
# 用法（在目标系统上，root）：
#   properos-mok-enroll                 # 生成/复用密钥 → 签名 → 进入登记队列
#   properos-mok-enroll --sign-only      # 只签名（密钥已在固件里）
#   properos-mok-enroll --status         # 查看当前状态
#
# 依赖：sbsigntool（sbsign/sbverify）、mokutil、openssl
set -euo pipefail

ESP="${PROPEROS_ESP:-/boot/efi}"
EFI="$ESP/EFI/BOOT/BOOTAA64.EFI"
KEYDIR="${PROPEROS_MOK_DIR:-/etc/properos/keys}"
KEY="$KEYDIR/MOK.key"
CRT="$KEYDIR/MOK.crt"
DER="$KEYDIR/MOK.der"

c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_warn() { printf '\033[33m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }
c_dim()  { printf '\033[2m%s\033[0m\n' "$*"; }

need_tools() {
    local miss=0
    for t in openssl sbverify; do
        command -v "$t" >/dev/null || { c_err "缺少 $t"; miss=1; }
    done
    if [[ $miss -eq 1 ]]; then
        c_dim "Debian/Ubuntu：apt install -y sbsigntool mokutil openssl"
        exit 1
    fi
}

sb_state() {
    if [[ -d /sys/firmware/efi ]]; then
        if command -v mokutil >/dev/null; then
            mokutil --sb-state 2>/dev/null || echo "SecureBoot 状态未知"
        else
            c_warn "未安装 mokutil，无法查询 SecureBoot 状态"
        fi
    else
        c_warn "当前不是 UEFI 启动（无 /sys/firmware/efi），本机无需 MOK"
    fi
}

[[ -f "$ESP/EFI/BOOT/BOOTAA64.EFI" || -f "$ESP/EFI/BOOT/BOOTX64.EFI" ]] || true
if [[ ! -f "$EFI" && -f "$ESP/EFI/BOOT/BOOTX64.EFI" ]]; then
    EFI="$ESP/EFI/BOOT/BOOTX64.EFI"
fi

case "${1:-}" in
--status)
    echo "ESP：$ESP"
    echo "引导器：$EFI"
    [[ -f "$EFI" ]] && sbverify --list "$EFI" 2>/dev/null | head -5 || c_warn "找不到引导器"
    echo
    sb_state
    if [[ -f "$CRT" ]]; then
        c_ok "本地密钥：$CRT"
        c_dim "$(openssl x509 -in "$CRT" -noout -subject -dates | tr '\n' ' ')"
    else
        c_warn "尚未生成本地密钥"
    fi
    exit 0
    ;;
--sign-only) ;;
--help | -h)
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
"") ;;
*)
    c_err "未知参数：$1"; exit 1 ;;
esac

need_tools

if [[ ! -f "$KEY" || ! -f "$CRT" ]]; then
    c_ok "生成 MOK 密钥对（有效期 10 年）"
    mkdir -p "$KEYDIR"; chmod 700 "$KEYDIR"
    openssl req -new -x509 -newkey rsa:2048 -nodes -days 3650 \
        -subj "/CN=ProperOS Secure Boot MOK/O=ByUsi Studio/" \
        -keyout "$KEY" -outform PEM -out "$CRT" >/dev/null 2>&1
    chmod 600 "$KEY"
    openssl x509 -in "$CRT" -outform DER -out "$DER"
    c_dim "  私钥 $KEY"
    c_dim "  证书 $CRT"
else
    c_ok "复用已有密钥 $CRT"
    [[ -f "$DER" ]] || openssl x509 -in "$CRT" -outform DER -out "$DER"
fi

[[ -f "$EFI" ]] || { c_err "找不到引导器 $EFI（ESP 是否挂载到 $ESP？）"; exit 1; }

c_ok "签名 $EFI"
cp -f "$EFI" "$EFI.unsigned"
if command -v sbsign >/dev/null; then
    sbsign --key "$KEY" --cert "$CRT" --output "$EFI" "$EFI.unsigned" 2>/dev/null
    sbverify --cert "$CRT" "$EFI" >/dev/null && c_ok "签名校验通过"
else
    c_err "缺少 sbsign"; exit 1
fi

if [[ "${1:-}" != "--sign-only" ]]; then
    if command -v mokutil >/dev/null; then
        c_ok "把证书加入固件登记队列（重启时按提示输入一次性密码）"
        c_dim "  接下来重启，在蓝色 MOK Manager 里选 Enroll MOK → Continue → 输入密码"
        mokutil --import "$DER" || c_warn "登记失败（可能已在队列中）"
    else
        c_warn "未安装 mokutil，证书需手动导入固件"
    fi
fi

echo
c_ok "完成。固件里登记成功后，可在 BIOS 里重新打开 Secure Boot。"
c_dim "撤销：mokutil --delete $DER 并重启"
