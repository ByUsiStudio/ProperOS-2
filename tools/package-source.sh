#!/usr/bin/env bash
# 打包 ProperOS 2.5 源码（不含内核源码树、rootfs、镜像等构建产物）
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dist/ProperOS-2.5-source.zip"
STAGE="$(mktemp -d)"
NAME="ProperOS-2.5"

mkdir -p "$ROOT/dist"
rm -f "$OUT"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

mkdir -p "$STAGE/$NAME"
cd "$ROOT"
cp -a README.md NOTICE .gitignore src apps build installer overlay tools docs kernel \
      "$STAGE/$NAME/"

# 清掉编译中间产物，只保留源码
find "$STAGE/$NAME/src" -name '*.o' -o -name '*.a' -o -name '*.d' | xargs -r rm -f
rm -rf "$STAGE/$NAME"/src/build
find "$STAGE/$NAME/docs" -name '*.pdf' -delete 2>/dev/null || true

# 清单与校验
{
    echo "ProperOS 2.5 Mobile · source package"
    echo "Vendor      : ByUsi Studio"
    echo "Developer   : 北啊呢"
    echo "App ID prefix: miao.byusistudio"
    echo "Package date: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
    echo
    echo "Contents:"
    echo "  src/        ProLoader / StBoot / properd / papp / ppkg / bootctl / shell / setup"
    echo "  src/gfx/    帧缓冲绘制、SDF 抗锯齿、evdev 触摸输入、即时模式控件"
    echo "  src/jspk/   .jspk 运行时：ZIP + DEFLATE + JSON + 声明式界面"
    echo "  apps/       10 个系统应用的 .jspk 源码（声明式 JSON）"
    echo "  build/      ARM64 构建脚本"
    echo "  installer/  安装脚本与 live init（图形安装器调用）"
    echo "  overlay/    系统根文件系统叠加层（systemd 单元、配置、EFI 引导器）"
    echo "  tools/      字体烘焙、.jspk 打包、MOK 登记、源码打包"
    echo "  docs/       架构、构建、验证、移植文档与截图"
    echo "  kernel/     内核 defconfig"
    echo
    echo "Build:"
    echo "  bash build/arch/arm64/10-kernel.sh   # 需要 aarch64 交叉工具链 + qemu-user-static"
    echo "  bash build/arch/arm64/20-rootfs.sh   # 内部会 make 组件并安装 .jspk 应用"
    echo "  bash build/arch/arm64/30-initramfs.sh"
    echo "  bash build/arch/arm64/50-iso.sh"
    echo "  bash build/arch/arm64/40-image.sh"
    echo
    echo "Package apps:"
    echo "  python3 tools/jspk-pack.py --all     # apps/* -> build/apps/*.jspk"
} > "$STAGE/$NAME/BUILD-INFO.txt"

( cd "$STAGE/$NAME" && find . -type f ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum > SHA256SUMS )

cd "$STAGE"
zip -rq9 "$OUT" "$NAME" -x "*/src/build/*" "*/build/apps/*"
cd "$ROOT"

SIZE=$(du -h "$OUT" | cut -f1)
echo "已生成 $OUT（$SIZE，$(( $(unzip -l "$OUT" | tail -1 | awk '{print $2}') )) 个文件）"
unzip -l "$OUT" | tail -3
