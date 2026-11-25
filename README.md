# ProperOS 2.5 Mobile

面向手机的 Linux 发行版：**ARM64** 为主。自研引导链（ProLoader / StBoot）、应用沙箱（papp）、系统包管理（ppkg）、服务管理器（properd）、**图形安装器**与 **Plasma Mobile 风格桌面**（`.jspk` 应用），全部随源码提供。整套界面直接渲染到 framebuffer，不依赖 X11 / Wayland / GPU 用户态。

- 开发者：**ByUsi Studio · 北啊呢**
- 应用包名前缀：`miao.byusistudio.***`
- 内核：**Linux 7.3.6**（LTS 主线，自编配置）
- 安装器：**图形向导**（触摸 / 键盘可操作），默认 root，**root 密码可留空**；无 framebuffer 时自动回退文本向导
- 容器：rootful podman + netavark + cgroup v2（overylay 存储）

---

## 1. 交付物

| 文件 | 大小 | 说明 |
|---|---|---|
| `dist/ProperOS-2.5-arm64.iso` | 217 MB | **ARM64 安装 ISO**（UEFI El Torito），图形安装向导，可刻盘 / dd 到 U 盘 / 挂虚拟机光驱 |
| `dist/ProperOS-2.5-arm64.iso.sha256` | — | ISO 校验值 |
| `dist/ProperOS-2.5-arm64.img` | 4.1 GB | **ARM64 预装磁盘镜像**，GPT 三分区（ESP + 只读系统 + /data），UEFI 直接启动，可 dd 到手机 / eMMC / SD / U 盘 |
| `dist/ProperOS-2.5-arm64.img.sha256` | — | 镜像校验值 |
| `dist/ProperOS-2.5-source.zip` | 2.9 MB | **全部源码**（`src/`、`apps/`、`installer/`、`build/`、`overlay/`、`tools/`、`docs/`、内核 defconfig），不含二进制产物 |
| `dist/ProperOS-2.5-source.zip.sha256` | — | 源码包校验值 |
| `dist/kernel/Image-properos-2.5` | 50 MB | ARM64 内核（容器 / 网络 / 显示 / 触摸 / 存储驱动全部内建，无需 initrd 加载模块） |
| `dist/kernel/config-2.5.0-properos-arm64` | 239 KB | 完整内核配置 |

仓库内目录：

| 目录 | 内容 |
|---|---|
| `src/` | 原生组件 C 源码：`proloader` `stboot` `properd` `papp` `ppkg` `bootctl` `properos-shell` `properos-setup` `properos-jspk` |
| `src/gfx/` | 自绘图形层：SDF 抗锯齿、灰度字形图集、evdev 输入、即时模式控件、软键盘 |
| `src/ui/` | 图形安装器（四步向导，Ubuntu 风格） |
| `src/jspk/` | `.jspk` 应用运行时：ZIP/DEFLATE/JSON + 声明式界面渲染 |
| `apps/` | 系统应用源码（设置、文件、时钟、计算器、电话、消息、相机、音乐、商店、欢迎） |
| `installer/` | 安装脚本（分区/格式化/拷贝，供图形前端调用）、live init、文本向导兜底 |
| `build/` | 可复现构建脚本，`build/arch/arm64/*.sh` 五步出镜像 |
| `overlay/` | 系统根叠加层：systemd 单元、网络/时区/firstboot、EFI 引导器 |
| `tools/` | 字体烘焙（`gen_font.py`）、`.jspk` 打包校验（`jspk-pack.py`）、Secure Boot MOK 登记（`mok-enroll.sh`）、源码打包 |
| `docs/` | 架构、构建、验证记录、设备移植，以及 `screenshots/` 实机截图 |

安装器、桌面与各系统应用的截图见 `docs/screenshots/`。

## 2. 架构分层

```text
┌──────────────────────────────────────────────────────────┐
│  桌面 properos-shell     Plasma Mobile 形态：面板/Dock/抽屉    │
│  安装 properos-setup     四步图形向导，与桌面共用 gfx 层    │
├──────────────────────────────────────────────────────────┤
│  properd   服务/应用管理器（Unix socket，崩溃重启）          │
│  papp      应用包 .papp：安装、沙箱运行（namespace + 降权）  │
│  ppkg      系统包 .ppkg：SHA-256 + 可选 Ed25519 签名        │
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
| ppkg 系统包 | `src/ppkg`：`.ppkg` 格式，哈希 + 签名 + 依赖检查 |
| jspk 应用包 | `src/jspk`：`.jspk` 声明式应用，ZIP + JSON，桌面直接运行 |
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

# 方式二：烧写安装 ISO，走图形安装向导（推荐，可自选磁盘与 root 密码）
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

## 5. 安装器（Ubuntu 风格图形向导）

从 ISO 启动后自动进入图形安装器，四步走完，触摸 / 键盘都能操作：

1. **欢迎** — 版本、架构、特性摘要
2. **安装位置** — 整盘清除 / 手动分区，目标磁盘单选，分区预览条
3. **账户与设备** — 设备名称、**root 密码（可留空）**、不设密码勾选框
4. **确认** — 汇总后写入，进度分七段：分区 → 格式化 → 挂载 → 拷贝 → 配置 → 引导 → 同步

root 密码留空时不设密码，`tty1` 自动登录并直接进桌面；

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
properos-bootctl sync            # 把当前槽复制到另一槽（补 B 槽）

properos-jspk /usr/share/properos/apps/clock.jspk    # 直接运行系统应用
properos-mok-enroll              # 生成 MOK 密钥并签名引导器（Secure Boot）

papp install hello.papp          # 安装应用
papp list                        # 已安装应用
papp run miao.byusistudio.hello  # 沙箱运行（uid 10000，无网络）
papp pack ./myapp myapp.papp     # 打包

ppkg install foo.ppkg            # 系统包
ppkg verify foo.ppkg             # 哈希 + 签名校验
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

| 项 | 现状 | 处理方式 |
|---|---|---|
| **Secure Boot** | 引导器自签，未购买微软 UEFI CA 签名 | 已提供 `properos-mok-enroll`：生成 MOK 密钥 → `sbsign` 签 `BOOTAA64.EFI` → `mokutil --import` 登记进固件，之后可重新打开 Secure Boot。不想登记就在固件里关掉 |
| **设备树** | 通用镜像面向 QEMU virt 与已跑 UEFI 的设备 | 真机（U-Boot 或固件不提供 DTB）构建时加 `PROPEROS_DTB=board.dtb bash build/arch/arm64/40-image.sh`，DTB 会写进 `/dtb/board.dtb` 并由 GRUB 的 `devicetree` 加载 |
| **A/B 槽** | 安装器只填充 A 槽 | `properos-bootctl sync` 把活动槽的 `Image`/`initramfs.img`/`SHA256SUMS` 复制到另一槽并标记 good，之后 `set-active b` 即可从 B 启动；OTA 沿用同一命令 |
| **中文控制台** | 图形界面全中文；纯文本控制台仍是 ASCII | Linux 内核控制台字体上限 512 字形，放不下 CJK，无法真正解决；控制台要保持英文，中文交互统一走图形界面（安装器、桌面、`.jspk` 应用） |
| **中文界面字体** | 内置 Noto Sans CJK 灰度图集（3920 字） | 不依赖系统字体，离线可用；生僻字会走逐像素降采样回退，笔画偏软 |
| **刷新方式** | fbdev 路径每帧 `pan_display` | 无 GPU 用户态，整屏重绘 25–40 ms（720×1280）；壁纸做了缓存，实际每帧只做一次 memcpy |
| **ppkg 签名** | 默认校验 SHA-256 | 完整签名：`ppkg sign` 生成 `SIGNATURE`（Ed25519/openssl），公钥放 `/etc/properos/keys/ppkg.pub`，`ppkg verify` 会同时校验哈希与签名 |
| **TCG 环境** | QEMU 无 KVM 时性能约 1/10 | 真机应使用 KVM；图形界面在 TCG 下仍可用，只是滑动动画掉帧 |

## 9. 许可

GPL-2.0（内核与自有组件），详见 `NOTICE`。
