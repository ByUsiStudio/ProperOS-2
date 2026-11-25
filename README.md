# ProperOS 2.5 Mobile

面向手机的 Linux 发行版：**ARM64** 为主，保留 AMD64 版本。自研引导链（ProLoader / StBoot）、应用沙箱（papp）、系统包管理（ppkg）、服务管理器（properd）与 framebuffer 启动器，全部随源码提供。

- 开发者：**ByUsi Studio · 北啊呢**
- 应用包名前缀：`miao.byusistudio.***`
- 内核：**Linux 7.3.6**（LTS 主线，自编配置）
- 安装器：Debian Installer 风格文本界面（whiptail），默认 root，**root 密码可留空**
- 容器：rootful podman + netavark + cgroup v2（overylay 存储）

---

## 1. 交付物

| 文件 | 大小 | 说明 |
|---|---|---|
| `dist/ProperOS-2.5-arm64.iso` | 214 MB | **ARM64 安装 ISO**（UEFI El Torito），Debian 风格文本安装向导，可刻盘 / dd 到 U 盘 / 挂虚拟机光驱 |
| `dist/ProperOS-2.5-arm64.iso.sha256` | — | ISO 校验值 |
| `dist/ProperOS-2.5-arm64.img` | 4.1 GB | **ARM64 预装磁盘镜像**，GPT 三分区（ESP + 只读系统 + /data），UEFI 直接启动，可 dd 到手机 / eMMC / SD / U 盘 |
| `dist/ProperOS-2.5-arm64.img.sha256` | — | 镜像校验值 |
| `dist/kernel/Image-properos-2.5` | 50 MB | ARM64 内核（容器 / 网络 / 显示 / 触摸 / 存储驱动全部内建，无需 initrd 加载模块） |
| `dist/kernel/config-2.5.0-properos-arm64` | 239 KB | 完整内核配置 |
| `dist/ProperOS-2.5-source.zip` | 480 KB | **全部源码**（`src/`、`installer/`、`build/`、`overlay/`、`tools/`、`docs/`、内核 defconfig），不含二进制产物 |

仓库内目录：

| 目录 | 内容 |
|---|---|
| `src/` | 原生组件 C 源码：`proloader` `stboot` `properd` `papp` `ppkg` `bootctl` `properos-shell` |
| `installer/` | Debian 风格安装器（whiptail 向导 + live init + 控制台字体脚本） |
| `build/` | 可复现构建脚本，`build/arch/arm64/*.sh` 五步出镜像 |
| `overlay/` | 系统根叠加层：systemd 单元、网络/时区/firstboot、EFI 引导器 |
| `tools/` | 字体烘焙（`gen_font.py`）、源码打包 |
| `docs/` | 架构、构建、验证记录、设备移植，以及 `screenshots/` 实机截图 |

安装器与启动器实机截图见 `docs/screenshots/`。

## 2. 架构分层

```text
┌──────────────────────────────────────────────────────────┐
│  启动器 properos-shell   framebuffer + 触摸/键盘，中文界面  │
├──────────────────────────────────────────────────────────┤
│  properd   服务/应用管理器（Unix socket，崩溃重启）          │
│  papp      应用包 .papp：安装、沙箱运行（namespace + 降权）  │
│  ppkg      系统包 .jspk：SHA-256 + 可选 Ed25519 签名        │
├──────────────────────────────────────────────────────────┤
│  systemd + podman（rootful、netavark、overlay、cgroup v2）  │
├──────────────────────────────────────────────────────────┤
│  StBoot    挂载 /data，首启播种 /etc /var /home，可写叠加    │
│  ProLoader A/B 槽选择、SHA-256 校验、失败回滚、应急 shell    │
├──────────────────────────────────────────────────────────┤
│  GRUB 2.06 (arm64-efi / i386-pc) → Linux 7.3.6            │
└──────────────────────────────────────────────────────────┘
```

与原始 ProperOS 2 概念的对应关系：

| 原始概念（iApp 应用） | 2.5 实现 |
|---|---|
| ProLoader（切界面的 Activity） | `src/proloader`：initramfs 第一阶段，槽选择/校验/挂载 |
| StBoot（第二段初始化） | `src/stboot`：数据分区、首启播种、switch_root |
| system.myu / core.myu | `properd` + systemd 单元 |
| ppkg / jspk 包 | `src/ppkg`：`.jspk` 格式，哈希 + 签名 + 依赖检查 |
| codeProcessor（脚本执行器） | `papp` 沙箱运行器 + `.papp` 应用包 |
| Termux 终端 | 原生 podman 容器 + tty1 控制台 |

## 3. 分区布局

| 分区 | 大小 | 类型 | 挂载 |
|---|---|---|---|
| 1 `properos-esp` | 256 MiB | FAT32 | `/boot/efi`（内核、initramfs、ProLoader 状态） |
| 2 `properos-system` | 3–4 GiB | ext4 | `/`（只读挂载由 ProLoader 完成） |
| 3 `properos-data` | 剩余 | ext4 | `/data`（应用、用户数据、可写层） |

ARM64 无 BIOS 兼容分区；AMD64 额外有 1 MiB `bios_grub`。

## 4. 烧写与首次启动

```bash
# 方式一：直接烧写预装镜像（UEFI 固件设备：postmarketOS 设备、树莓派 4 UEFI、QEMU virt）
sudo dd if=ProperOS-2.5-arm64.img of=/dev/sdX bs=4M status=progress oflag=sync

# 方式二：烧写安装 ISO，走 Debian 风格安装向导（推荐，可自选磁盘与 root 密码）
sudo dd if=ProperOS-2.5-arm64.iso of=/dev/sdX bs=4M status=progress oflag=sync
# 或刻录为光盘 / 挂到虚拟机光驱

# 启动后默认进入 root（安装/镜像默认无密码）
```

在 QEMU 中体验 ARM64：

```bash
# 预装镜像直接启动（-vnc 换成 -display gtk 可开窗口；键盘必须显式挂
# virtio-keyboard-pci，否则 ARM virt 机器上没有可用键盘设备）
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 -smp 2 \
  -drive if=pflash,format=raw,file=/usr/share/AAVMF/AAVMF_CODE.fd,readonly=on \
  -drive if=none,file=ProperOS-2.5-arm64.img,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -device virtio-gpu-pci -device virtio-keyboard-pci -device virtio-tablet-pci \
  -netdev user,id=n0 -device virtio-net-device,netdev=n0 \
  -vnc 127.0.0.1:0

# 从 ISO 启动安装器（光驱用 virtio-scsi + scsi-cd，El Torito 才认）
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 -smp 2 \
  -drive if=pflash,format=raw,file=/usr/share/AAVMF/AAVMF_CODE.fd,readonly=on \
  -drive if=none,file=ProperOS-2.5-arm64.iso,format=raw,media=cdrom,readonly=on,id=cd0 \
  -device virtio-scsi-device -device scsi-cd,drive=cd0 \
  -drive if=none,file=target.img,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -device virtio-gpu-pci -device virtio-keyboard-pci -device virtio-tablet-pci \
  -vnc 127.0.0.1:0 -boot d
```

## 5. 安装器（Debian 风格）

从 ISO 启动后自动进入安装向导：

1. 选择目标磁盘
2. 主机名
3. **root 密码（可留空）**：留空时不设密码，tty1 自动登录 root
4. 确认分区（ESP 256 MiB + 系统 4 GiB + 数据剩余）
5. 进度条安装 → 完成重启

无人值守：

```bash
properos-install --disk /dev/mmcblk0 --yes --blank-password
# 或内核参数
properos.autoinstall=/dev/mmcblk0 properos.blankpass=1
```

## 6. 常用命令

```bash
properos-bootctl status          # A/B 槽状态
properos-bootctl mark-good       # 标记本次启动成功（首启已自动执行）
properos-bootctl rollback        # 回滚到另一槽

papp install hello.papp          # 安装应用
papp list                        # 已安装应用
papp run miao.byusistudio.hello  # 沙箱运行（uid 10000，无网络）
papp pack ./myapp myapp.papp     # 打包

ppkg install foo.jspk            # 系统包
ppkg verify foo.jspk             # 哈希 + 签名校验
ppkg list

properd status properd           # 服务状态（socket: /run/properd.sock）
podman run --rm -p 8080:80 nginx:alpine
```

## 7. 从源码构建

```bash
# 依赖：gcc-aarch64-linux-gnu qemu-user-static debootstrap grub-efi-arm64-bin
sudo bash build/arch/arm64/all.sh
```

分步：

```bash
sudo bash build/arch/arm64/10-kernel.sh   # 交叉编译内核（约 20 分钟）
sudo bash build/arch/arm64/20-rootfs.sh   # debootstrap + 组件安装
sudo bash build/arch/arm64/30-initramfs.sh
sudo bash build/arch/arm64/40-image.sh    # 组装 GPT 镜像
```

## 8. 已知限制

- **Secure Boot 需关闭**：BOOTAA64.EFI 自签，未做 shim/MOK。
- **设备树**：通用镜像面向 QEMU virt / 已运行 UEFI 的设备；具体手机需替换 DTB 并按 `docs/05-设备移植.md` 打包 boot 分区。
- **A/B 槽**：当前 A 槽预置，B 槽为空；`properos-bootctl` 与 ProLoader 已支持切换/回滚，槽复制工具在路线图中。
- **中文控制台**：安装器使用英文文本界面（Linux 控制台无 CJK 字形），安装后的图形启动器为中文。
- **ppkg 签名**：默认校验哈希；签名需放置 `/etc/properos/keys/ppkg.pub` 并用 openssl 生成 `SIGNATURE`。

## 9. 许可

GPL-2.0（内核与自有组件），详见 `NOTICE`。
