#!/usr/bin/env bash
# 打包 ProperOS 25.0.0 源码（不含内核源码树、rootfs、镜像等构建产物）
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dist/ProperOS-25.0.0-source.zip"
STAGE="$(mktemp -d)"
NAME="ProperOS-25.0.0"

mkdir -p "$ROOT/dist"
rm -f "$OUT"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

mkdir -p "$STAGE/$NAME"
cd "$ROOT"
cp -a README.md NOTICE .gitignore src apps build installer overlay tools docs kernel site \
      "$STAGE/$NAME/"

# 清掉编译中间产物，只保留源码
find "$STAGE/$NAME/src" -name '*.o' -o -name '*.a' -o -name '*.d' | xargs -r rm -f
rm -rf "$STAGE/$NAME"/src/build
# 打包产物（.ppkg / 旧 .jspk）不进源码包，避免校验和不一致
rm -rf "$STAGE/$NAME"/build/apps
find "$STAGE/$NAME/docs" -name '*.pdf' -delete 2>/dev/null || true

# 清单与校验
{
    echo "ProperOS 25.0.0 Mobile · source package"
    echo "Vendor      : ByUsi Studio"
    echo "Developer   : 北啊呢"
    echo "App ID prefix: miao.byusistudio"
    echo "Package date: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
    echo
    echo "Contents:"
    echo "  src/        ProLoader / StBoot / properd / papp / ppkg / bootctl / shell / setup"
    echo "  src/gfx/    帧缓冲绘制、SDF 抗锯齿、evdev 触摸输入、即时模式控件"
    echo "  src/jspk/   应用运行时：包读取（目录 / ZIP）+ JSON + 声明式界面"
    echo "  apps/       11 个系统应用的源码（manifest.json + 界面 JSON，打成 .ppkg 交付）"
    echo "  build/      ARM64 构建脚本"
    echo "  installer/  安装脚本与 live init（图形安装器调用）"
    echo "  overlay/    系统根文件系统叠加层（systemd 单元、配置、EFI 引导器）"
    echo "  tools/      字体烘焙、应用打包（ppkg-app.py）、MOK 登记、源码打包"
    echo "  docs/       架构、构建、验证、移植文档与截图"
    echo "  site/       官网源码（index.html + assets）"
    echo "  kernel/     内核 defconfig"
    echo
    echo "Build:"
    echo "  bash build/arch/arm64/10-kernel.sh   # 需要 aarch64 交叉工具链 + qemu-user-static"
    echo "  bash build/arch/arm64/20-rootfs.sh   # 内部会 make 组件并安装 .ppkg 应用"
    echo "  bash build/arch/arm64/30-initramfs.sh"
    echo "  bash build/arch/arm64/40-image.sh    # 生成预装磁盘镜像"
    echo "  bash build/arch/arm64/50-iso.sh      # 生成安装 ISO"
    echo
    echo "Package apps:"
    echo "  python3 tools/ppkg-app.py all apps build/apps   # apps/* -> build/apps/*.ppkg"
    echo
    echo "Install layout:"
    echo "  /opt/properos/apps/<name>/   ppkg 应用（PKGINFO + FILES + payload/）"
    echo "  .jspk 已停止使用，应用统一以 .ppkg 分发"
} > "$STAGE/$NAME/BUILD-INFO.txt"

( cd "$STAGE/$NAME" && find . -type f ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum > SHA256SUMS )

cd "$STAGE"
zip -rq9 "$OUT" "$NAME" -x "*/src/build/*" "*/build/apps/*"
cd "$ROOT"

SIZE=$(du -h "$OUT" | cut -f1)
echo "已生成 $OUT（$SIZE，$(( $(unzip -l "$OUT" | tail -1 | awk '{print $2}') )) 个文件）"
unzip -l "$OUT" | tail -3
