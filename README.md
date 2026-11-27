# ProperOS 25.0.0 "Two Dot Five" — 完整源码资源包

---

本包为该项目的全量源码资源，用于 Git 开源。系统源码、Linux 内核源码、
应用源码、官网工程、构建脚本、安装器、overlay、文档、内核产物一次性齐全。

## 一、目录结构
```text
system/                       系统本体（原 properos-2.5 仓库）
  src/                        引导、properd、papp、ppkg、shell、gfx、运行时（C）
  apps/                       11 个应用源码（terminal/files/settings/clock/music/
                              store/phone/camera/calc/notes/gallery）
  build/arch/arm64/           构建流水线：00-env → 10-kernel → 20-rootfs →
                              30-initramfs → 40-image → 50-iso
  installer/                  图形安装器（installer.c）与 live 单元
  overlay/                    写入镜像的 etc/usr 定制文件（os-release、session、apt 等）
  tools/                      ppkg-app.py（.ppkg 打包）、package-source.sh
  docs/                       设计与验证文档
  kernel/                     内核配置（properos_25.0.0_arm64_defconfig 等）
  site/                       官网工程（Node.js + EJS，npm start 运行）
  container/                  容器相关资源
  dist/kernel/                内核构建产物（Image 50MB、vmlinuz 17MB、两份 config）

kernel/arm64/                 实际用于 arm64 构建的 Linux 7.3.6 源码树
  arch/arm64/boot/Image       已编译 Image（50MB）
  .config                     arm64 实际构建配置
  （已剔除 *.o/*.a/*.cmd/*.d/*.ko/vmlinux 等编译中间物）

kernel/host/                  同版本 Linux 7.3.6 宿主（x86）源码树
  Makefile / Kconfig / COPYING / CREDITS / MAINTAINERS / .config / LICENSES/
  io_uring/ 以及 arch block certs crypto Documentation drivers fs include
  init ipc kernel lib mm net rust samples scripts security sound tools usr virt
  （同样剔除编译中间物，源码完整）

kernel/source-tarball/        linux-7.3.6.tar.xz（离线构建用原始源码包，156MB）

build-artifacts/              三个 initramfs 产物（host / arm64 / live-arm64），
                              含定制 init 脚本，可直接比对

vm-test/                      QEMU 验证脚本（vmlib.py、probe-*.py、test-*.py、
                              install-plasma.sh、rebuild-25.sh）
```


## 二、有意排除的内容（非源码，可重建）

```text
site/node_modules/            npm 依赖，执行 npm install 恢复
dist/*.iso  dist/*.img        发行产物（ISO 769MB / IMG 7.3GB），单独交付，
                              不进 Git（GitHub 单文件上限 100MB，建议放 Release）
*.qcow2  payload*.img  *.fd   QEMU 测试用硬盘与固件变量，测试时临时创建
cache/                       下载缓存（内核源码包已单列到 kernel/source-tarball/）
rootfs/ rootfs-arm64/         由 20-rootfs.sh + debootstrap 生成的根文件系统
                              （2.4GB，非源码，脚本可完整重建）
img-stage-arm64/ iso-stage-arm64/ iso-auto/ isofix/ iso/
                              打包中间目录，由 40-image.sh / 50-iso.sh 生成
*.o *.a *.cmd *.d *.ko vmlinux  编译中间物
日志、临时脚本输出             *.log 等
```

## 三、重建方式

1. 系统组件（需 aarch64-linux-gnu- 交叉工具链）
     ```bash
     cd system/src && make ARCH=arm64
     ```

2. 内核
     ```bash
     export KERNEL_SRC_ARM64=<本包>/kernel/arm64
     bash system/build/arch/arm64/10-kernel.sh
     ```

3. 根文件系统 / 镜像 / ISO
     ```bash
     bash system/build/arch/arm64/20-rootfs.sh
     bash system/build/arch/arm64/30-initramfs.sh
     bash system/build/arch/arm64/40-image.sh
     bash system/build/arch/arm64/50-iso.sh
     ```

4. 官网
     ```bash
     cd system/site && npm install && npm start        # 或 npm run export 静态导出
     ```

## 四、版本

ProperOS 25.0.0 (Two Dot Five) · arm64 · Debian bookworm 基底 · Plasma Mobile 5.27
Linux 7.3.6 · 引导 ProLoader / GRUB EFI · 应用格式 .ppkg
