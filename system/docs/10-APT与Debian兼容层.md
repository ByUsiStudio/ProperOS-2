# 10 · apt 与 Debian 兼容层

ProperOS 25.0.0 的软件来源是 **Debian bookworm**。系统里保留了完整的 `apt` / `dpkg`，
但所有 `.deb` 在落地前都要经过一层转换：**deb → .ppkg → 安装**。
目的有三个：

1. **统一包视图** —— `ppkg list` 能同时看到原生包和 Debian 包，装机、审计、回滚都用同一套记录。
2. **拦住会毁掉系统的包** —— Debian 的 `linux-image-*` / `grub-*` / `shim-signed`
   一旦装进来，`/boot` 与 EFI 分区会被换成和 ProperOS 引导器不匹配的组合，重启就进不去。
3. **可审计** —— 每个转换出来的包都带原始 deb 的 SHA-256、来源套件、维护脚本存档。

---

## 1. 软件源

`/etc/apt/sources.list`：

```
deb http://mirrors.cloud.tencent.com/debian bookworm main contrib non-free-firmware
deb http://mirrors.cloud.tencent.com/debian bookworm-updates main contrib non-free-firmware
deb http://mirrors.cloud.tencent.com/debian-security bookworm-security main contrib non-free-firmware
```

换源直接改这个文件，也可以用 `properos-mirror`（见 §6）一键切到国内镜像。

---

## 2. 三道关卡

```
apt install foo
      │
      ├─① apt preferences（解析阶段）
      │     linux-image-* / grub-* / shim-* 的 Pin-Priority 是 -1
      │     → apt 根本不会把它们放进事务
      │
      ├─② dpkg-shim（执行阶段，apt 通过 Dir::Bin::dpkg 调用）
      │     再查一遍包名，命中就中止整个事务并打印原因
      │
      └─③ ppkg install（落地阶段）
            format=deb 的包会再次检查，然后调 dpkg 解包
```

三处都拦，是因为前两处都可能被绕过：用户可以直接 `dpkg -i`，也可能用
`apt install ./linux-image-xxx.deb` 绕过 pin。最后一道在 `ppkg` 内部，绕不过去。

### 2.1 被拦截的包

| 前缀 | 原因 |
|---|---|
| `linux-image-*` | 内核镜像由 ProperOS 自己构建（含触摸/显示/容器驱动内建） |
| `linux-modules-*` `linux-headers-*` `linux-kbuild-*` | 与自建内核 ABI 不匹配，装了也没用 |
| `linux-source-*` `linux-support-*` `linux-perf-*` `linux-config-*` | 同上，属于内核构建链 |
| `grub-efi-*` `grub-common` `grub2-common` `grub-pc*` | 会覆盖 ProperOS 的 `BOOTAA64.EFI` 与 `grub.cfg` |
| `shim-signed` `systemd-boot*` | 引导链上的其它实现，混装会导致固件选错入口 |

拦截日志写在 `/var/log/properos/apt-guard.log`。

### 2.2 内核怎么升级

不走 apt，走 A/B 槽：

```bash
properos-bootctl status          # 看当前槽与两侧校验值
properos-bootctl sync            # 把当前槽复制到另一槽
properos-bootctl set-active b    # 下次从 B 启动
properos-bootctl rollback        # 回滚
```

---

## 3. 转换：deb2ppkg

`/usr/lib/properos/deb2ppkg` 把 `.deb` 拆成 ppkg 容器：

```
foo_1.2-3_arm64.ppkg
├── PKGINFO         格式见下
├── payload.deb     原始 deb（字节不变）
└── scripts/        preinst / postinst / prerm / postrm / conffiles / triggers 存档
```

`PKGINFO`：

```
name = foo
version = 1.2-3
arch = arm64
description = ...
format = deb
origin = debian
origin-source = foo
deb-package = foo
deb-sha256 = <原始 deb 的 sha256>
deb-file = foo_1.2-3_arm64.deb
depends = libc6 (>= 2.34), ...
```

转换后的包放在 `/var/cache/properos/ppkg/`，同名 `.sha256` 一并生成。

### 3.1 为什么保留原始 deb

ppkg 不重新实现 dpkg 的语义。`conffiles` 的三方合并、`triggers` 的延迟执行、
`preinst` 里对旧版本的判断——这些在 Debian 里是二十年积累的边界情况，
自己写一遍只会更差。所以 ppkg 的做法是：

> 负责「转换、校验、记录、拦截」，落地仍然交给 `dpkg`。

这样 apt 的依赖解析、`dpkg --audit`、`apt-get -f install` 全部照常工作，
同时系统里多了一层统一的包视图。

---

## 4. dpkg-shim

`/etc/apt/apt.conf.d/90-properos`：

```
Dir::Bin::dpkg "/usr/lib/properos/dpkg-shim";
```

apt 所有对 dpkg 的调用都会走这个脚本。它只接管两种子命令：

| 子命令 | 处理 |
|---|---|
| `--unpack` | 收集 `.deb` → 查黑名单 → `deb2ppkg` → `ppkg install --unpack-only` |
| `-i` / `--install` | 同上，但走 `ppkg install`（完整安装） |
| 其它（`--configure` `--status` `--remove` `--print-avail` …） | 原样转给 `/usr/bin/dpkg` |

`--unpack` 之后 apt 还会调 `dpkg --configure`，这一步直接转发——解包和配置
本来就是两个阶段，shim 只替换「解包」这一段。

---

## 5. 常用操作

```bash
apt update                       # 正常用
apt install htop                 # → /var/cache/properos/ppkg/htop_*.ppkg → ppkg → dpkg
apt remove htop                  # 走 dpkg --purge，ppkg 记录同步删除
apt install linux-image-arm64    # 被拒绝，退出码 100，提示用 properos-bootctl

ppkg list                        # 原生包与 Debian 包一起列，来源列区分
ppkg info zlib1g                 # 版本、依赖、文件数
ppkg verify /var/cache/properos/ppkg/zlib1g_*.ppkg

# 手工转换（不安装）
/usr/lib/properos/deb2ppkg foo_1.2-3_arm64.deb /tmp/foo.ppkg
ppkg verify /tmp/foo.ppkg
ppkg install /tmp/foo.ppkg
```

`ppkg list` 输出示例：

```
包名                     版本         架构       来源     描述
zlib1g                   1:1.2.13.dfsg-1  arm64   debian   压缩库
properos-welcome         2.5          all        ppkg     首次启动引导
```

---

## 6. 常见问题

**`apt install` 报「无法定位软件包」**
先 `apt update`。镜像里的索引是构建时生成的，首次启动后可能已过期。

**装完某个包后命令找不到**
Debian 包可能装在 `/usr/sbin` 或 `/usr/games`，检查 `$PATH`：
`echo $PATH`。

**想看某个 deb 装了什么**
```bash
dpkg -L <包名>
ppkg info <包名>
```

**转换后的包占空间**
`/var/cache/properos/ppkg/` 可以定期清理：
```bash
find /var/cache/properos/ppkg -name '*.ppkg' -mtime +30 -delete
```
清理不影响已安装的包（记录在 `/var/lib/ppkg/db/`）。

**想临时绕过 shim**
不提供。内核包一旦装进来，系统就可能无法启动；如果确实要手工调试，
在 live 环境里操作，不要动已安装的系统。

---

## 7. 与上游的关系

| 组件 | 来源 |
|---|---|
| `apt` `dpkg` `dpkg-deb` | Debian 原版，未打补丁 |
| `deb2ppkg` `dpkg-shim` `ppkg` | ProperOS 自研 |
| 包数据库 | dpkg 的 `/var/lib/dpkg` 为主，`/var/lib/ppkg/db` 做统一视图与来源记录 |

卸载用 `apt remove` 或 `ppkg remove` 都可以，两边最终都落到 `dpkg --purge`，
不会出现「一边删了一边还在」的情况。
