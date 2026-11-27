ProperOS 25.0.0 Mobile · 交付包
================================

Vendor   : ByUsi Studio
Developer: 北啊呢
App ID   : miao.byusistudio
构建时间 : 2026-11-26 23:51 (UTC+8)
平台     : ARM64 (aarch64) / UEFI / Linux 7.3.6 / KDE Plasma Mobile

一、文件清单
------------

ProperOS-25.0.0-arm64.iso.zip    769 MB   ISO 的 zip 封装（存储模式，不二次压缩）
ProperOS-25.0.0-arm64.iso        769 MB   ISO 原始文件，可直接烧写到 U 盘
ProperOS-25.0.0-source.zip       3.3 MB   系统源代码（含 build 脚本、11 个应用源码、官网源码）
ProperOS-25.0.0-site.zip         275 KB   官网代码（解压即用，index.html 在根目录）
SHA256SUMS.txt                           以上四个文件的校验值

ISO 内部已用 squashfs + xz 压缩，外部再套 zip 不会变小，因此 iso.zip 采用存储模式，
仅为满足「全部以 zip 交付」的格式要求；实际使用建议直接烧写未压缩的 .iso。

二、校验
--------

    sha256sum -c SHA256SUMS.txt

预期值：

    93f4a4c3e84e18aa162c654d93435ddde9449a05fe423b9c4d79cf0a3115fead  ProperOS-25.0.0-arm64.iso.zip
    4e9be3fb77d343b8922eceab9913b6610751341824f41de0205915b2607a98c4  ProperOS-25.0.0-site.zip
    1ace7ad2b078bacf461ec1dfa3f08e1b0a866d3b2675c344774b014e908135d7  ProperOS-25.0.0-source.zip
    6433cfc11726c5d9d089df45f7c90176756def00f5e831b032538ca541b85d66  ProperOS-25.0.0-arm64.iso

三、安装到设备
--------------

ISO 是 UEFI 可启动安装镜像，卷标 PROPEROS2500，内置 El Torito EFI 引导项，
不需要外部 GRUB。设备需支持 UEFI 启动（Secure Boot 需先登记 MOK，见 tools/mok-enroll.sh）。

写入 U 盘（Linux/macOS，/dev/sdX 换成你的盘，写错会清空整盘）：

    sudo dd if=ProperOS-25.0.0-arm64.iso of=/dev/sdX bs=4M conv=fsync status=progress

写入后从 U 盘启动，进入四步图形安装向导：
    1. 选择磁盘        2. 确认分区方案（ESP / 只读系统 / 独立数据，A/B 双槽）
    3. 设置用户与密码  4. 开始安装

启动后 GRUB 菜单停留 10 秒自动引导；内核与 live 根挂载约 20 秒；
图形安装器上屏需要 90-120 秒（Mesa 软件渲染，期间屏幕为黑属正常现象，
不要断电或重复按键盘）。实机 GPU 可用时会明显更快。

默认 rootless，密码可留空；首次启动进入 Plasma Mobile 而非桌面 shell。

QEMU 快速试用（需要 AAVMF/EDK2 ARM64 固件）：

    qemu-system-aarch64 -M virt -cpu cortex-a72 -smp 4 -m 4096 \
      -bios /usr/share/AAVMF/AAVMF_CODE.fd \
      -drive file=ProperOS-25.0.0-arm64.iso,media=cdrom,readonly=on \
      -device virtio-gpu-pci -device usb-kbd -device usb-tablet -vnc :1

四、源码结构（source.zip 解压后为 ProperOS-25.0.0/）
------------------------------------------------------

src/            ProLoader 引导、StBoot、properd 服务、papp 应用运行时、ppkg 包管理、bootctl、shell、setup
src/gfx/        帧缓冲绘制、SDF 抗锯齿、evdev 触摸输入、即时模式控件
src/jspk/       应用运行时：包读取（目录 / ZIP）、JSON 解析、声明式界面
apps/           11 个系统应用源码（calc camera clock files message music phone settings store terminal welcome）
build/          ARM64 构建脚本 00-env.sh → 10-kernel → 20-rootfs → 30-initramfs → 40-image → 50-iso
installer/      安装脚本与 live init（图形安装器调用的后端）
overlay/        根文件系统叠加层（systemd 单元、配置、EFI 引导器）
tools/          字体烘焙、应用打包 ppkg-app.py、MOK 登记、源码打包
docs/           架构、构建、验证、移植文档与截图
site/           官网源码
kernel/         内核 defconfig

重建流程（需 aarch64 交叉工具链与 qemu-user-static）：

    bash build/arch/arm64/10-kernel.sh
    bash build/arch/arm64/20-rootfs.sh
    bash build/arch/arm64/30-initramfs.sh
    bash build/arch/arm64/40-image.sh
    bash build/arch/arm64/50-iso.sh

应用打包（.ppkg 为唯一格式，.jspk 已停止使用）：

    python3 tools/ppkg-app.py all apps build/apps     # 产出 build/apps/*.ppkg
    ppkg install build/apps/properos-app-calc-25.0.0.ppkg

ppkg 目录结构：PKGINFO（元数据）+ FILES（逐文件清单与哈希，路径相对 payload/）+ payload/（实际文件）。
安装位置 /opt/properos/apps/<name>/，shell 与桌面启动器都从该目录扫描。

五、官网部署
------------

site.zip 解压后 index.html 位于根目录，纯静态，无外部字体或 CDN 依赖，可直接：

    python3 -m http.server 8000          # 本地预览
    # 或整目录上传到任意静态托管 / Nginx 站点根目录

页面包含：特性、架构、安装、应用、下载、上手、开发七个分区，含 25.0.0 版本号、
下载卡片（ISO / 源码 / 官网）、安装四步图示与已知限制说明。

六、已知限制
------------

- 仅 ARM64 + UEFI；x86_64 与 Legacy BIOS 不支持。
- 图形栈走 Mesa kms_swrast 软件渲染（virtio-gpu / 部分板载 GPU 可用），无硬件 3D 加速。
- Secure Boot 默认关闭，需用 tools/mok-enroll.sh 生成 MOK 自行签名。
- 未包含预装磁盘镜像（ProperOS-25.0.0-arm64.img，7.3 GB）；需要可直接从 ISO 安装生成。
