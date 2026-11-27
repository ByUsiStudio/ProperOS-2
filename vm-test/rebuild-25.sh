#!/usr/bin/env bash
# 重建 25.0.0：rootfs → 磁盘镜像 → ISO
set -euo pipefail
cd /workspace/properos-2.5
echo "===== 20 rootfs ====="
bash build/arch/arm64/20-rootfs.sh
echo "===== 30 initramfs ====="
bash build/arch/arm64/30-initramfs.sh
echo "===== 40 image ====="
bash build/arch/arm64/40-image.sh
echo "===== 50 iso ====="
bash build/arch/arm64/50-iso.sh
echo "===== DONE ====="
