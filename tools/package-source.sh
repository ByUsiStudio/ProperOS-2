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
cp -a README.md NOTICE .gitignore src build installer overlay tools docs kernel \
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
    echo "  src/        ProLoader / StBoot / properd / papp / ppkg / bootctl / shell 源码"
    echo "  build/      ARM64 与 amd64 的构建脚本"
    echo "  installer/  Debian 风格文本安装器（whiptail）与 live init"
    echo "  overlay/    系统根文件系统叠加层（systemd 单元、配置、EFI 引导器）"
    echo "  tools/      字体烘焙、源码打包、验证脚本"
    echo "  docs/       架构与移植文档"
    echo "  kernel/     内核 defconfig"
    echo
    echo "Build:"
    echo "  bash build/arch/arm64/00-all.sh      # 需要 aarch64 交叉工具链 + qemu-user-static"
} > "$STAGE/$NAME/BUILD-INFO.txt"

( cd "$STAGE/$NAME" && find . -type f ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum > SHA256SUMS )

cd "$STAGE"
zip -rq9 "$OUT" "$NAME" -x "*/src/build/*"
cd "$ROOT"

SIZE=$(du -h "$OUT" | cut -f1)
echo "已生成 $OUT（$SIZE，$(( $(unzip -l "$OUT" | tail -1 | awk '{print $2}') )) 个文件）"
unzip -l "$OUT" | tail -3
