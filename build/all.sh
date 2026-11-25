#!/bin/bash
# 一键构建：内核 → rootfs → initramfs → squashfs → ISO
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
START=$(date +%s)

"$HERE/10-kernel.sh"
"$HERE/20-rootfs.sh"
"$HERE/30-initramfs.sh"
"$HERE/40-iso.sh"

echo
echo "构建耗时: $(( ($(date +%s)-START)/60 )) 分 $(( ($(date +%s)-START)%60 )) 秒"
ls -lh "$HERE/../dist"
