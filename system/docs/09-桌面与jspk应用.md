# 09 · 桌面（Plasma Mobile）与应用运行时

ProperOS 25.0.0 的桌面是 **KDE Plasma Mobile** 本体，不是照着它重写的仿制品。
系统里装的是 Debian bookworm 的 `plasma-mobile 5.27.2`，会话、窗口管理、状态栏、
通知、锁屏、输入法全部由 KDE 的组件提供。

| 组成 | 实现 |
|---|---|
| 桌面外壳 | Plasma Mobile 5.27：`org.kde.plasma.phoneshell` + KWin Wayland |
| 应用运行时 | `properos-app`，在会话里作为普通 Wayland 客户端开全屏窗口 |
| 系统应用 | `apps/*` 声明式 JSON → `.ppkg`，装到 `/opt/properos/apps` 并注册进应用抽屉 |
| 兜底桌面 | `properos-shell`（framebuffer 直绘），仅在没有 DRM 设备时启动 |

---

## 1. 桌面：Plasma Mobile

### 1.1 组件来源

| 包 | 作用 |
|---|---|
| `plasma-mobile` | 手机形态的 shell（`org.kde.plasma.phoneshell`）、设置、状态栏 |
| `plasma-workspace-wayland` | `startplasma-wayland`、`plasma_session` |
| `kwin-wayland` | 合成器：DRM/KMS 输出、触摸与指针、窗口管理 |
| `plasma-nm` / `plasma-pa` / `powerdevil` | 网络、音量、电源与亮度 |
| `maliit-keyboard` | 虚拟键盘（触摸输入必需） |
| `xdg-desktop-portal-kde` | 应用的文件选择、截图等 portal |
| `milou` | 应用启动器搜索 |
| `fonts-noto-cjk` | 中文显示（Plasma 走 fontconfig，不吃内置图集） |

装完后 `/usr/share/wayland-sessions/plasma-mobile.desktop` 就是标准会话入口，
`startplasmamobile` 会设好 `PLASMA_PLATFORM=phone:handset` 与 phoneshell 再拉起 KWin。

### 1.2 启动链

```
systemd (multi-user)
  └─ properos-session.service        Type=simple, PAMName=login, TTYPath=/dev/tty1
       └─ /usr/bin/properos-session  设环境变量（软件渲染、XDG_RUNTIME_DIR、dbus）
            └─ startplasma-wayland --xwayland --inputmethod maliit-keyboard
                 └─ kwin_wayland      拿走 DRM master，接管屏幕与触摸
                      └─ plasmashell  加载 org.kde.plasma.phoneshell
```

服务只在 `/dev/dri/card0` 存在时启动（`ConditionPathExists`）。
没有 DRM 的设备（老式 framebuffer、部分开发板）会自动落到 `properos-shell`，
两条路径互斥（`Conflicts=`），不会出现两个桌面抢屏幕。

### 1.3 显示与输入

| 场景 | 路径 |
|---|---|
| 有 GPU（真机） | KWin → GBM/EGL → 厂商驱动 |
| 无 GPU（QEMU virtio-gpu、软件渲染） | KWin → `kms_swrast`（llvmpipe），`LIBGL_ALWAYS_SOFTWARE=1` |
| 触摸 | KWin 的 libinput，边缘手势由 KWin 的 touch edges 配置 |
| 鼠标/数位板 | KWin 按指针处理，`virtio-tablet` 在 QEMU 里可直接点 |

`properos-session` 会读 `/sys/class/drm/card0/device/vendor`：识别到 virtio-gpu（`0x1af4`）
或 bochs（`0x1234`）就强制软件渲染，因为这两类虚拟 GPU 没有可用的 GL 驱动。

### 1.4 系统应用怎么进抽屉

应用本身只描述界面，Plasma 不认识 JSON。所以打包时 `tools/ppkg-app.py`
按 manifest 把 `.desktop` 一起写进 `.ppkg` 的 payload，安装后落在
`/usr/share/applications/properos-<名字>.desktop`：

```ini
[Desktop Entry]
Type=Application
Name=设置
Exec=/usr/bin/properos-app /opt/properos/apps/settings
Icon=preferences-system
Categories=Settings;System;
X-ProperOS-Package=properos-app-settings
```

`properos-app` 检测到 `WAYLAND_DISPLAY` 就把自己当成 Wayland 客户端
（`src/gfx/wl.c`：xdg-shell + wl_shm），申请全屏 toplevel，KWin 负责把它放到前台。
没有 Wayland 时退回 `/dev/fb0` 直绘，所以同一份二进制在安装器环境、救援模式、
桌面会话里都能跑。

### 1.5 切回 framebuffer 桌面

```bash
systemctl stop properos-session
systemctl start properos-shell
```

或者在引导参数里加 `properos.fbdev=1`（内核没有 DRM 时等价）。
`properos-shell` 只依赖 `/dev/fb0` 与 evdev，用来排查「Plasma 起不来」很方便。

---

## 2. 触摸与手势

桌面里的触摸由 **KWin** 处理：单指点击、双指缩放、边缘内滑返回、下拉通知栏
都是 Plasma Mobile 自带行为，不需要 ProperOS 干预。

`.jspk` 应用内的手势由运行时统一识别，evdev 与 Wayland 两条输入路径共用同一套
状态机（`src/gfx/input.c` 的 `in_press/in_move/in_release`），所以同一个应用在
Plasma 窗口里和在 framebuffer 全屏下行为一致：

| 手势 | 触发条件 | 行为 |
|---|---|---|
| 点击 | 位移 < 36 px 且未长按 | 触发控件动作 |
| 长按 | 按住 520 ms 未移动 | 卡片菜单 / 编辑 |
| 上下滑 | 位移 ≥ 36 px | 列表滚动（带惯性） |
| 左边缘内滑 | 起手在左侧 1/22 区域右滑 | 返回上一页 / 关闭应用 |
| 右边缘内滑 | 起手在右侧 1/22 区域左滑 | 同上 |
| 底部上滑 | 起手在底部 1/12 区域上滑 | 返回桌面（仅 framebuffer 模式） |

实现要点：

- **多指追踪**：`ABS_MT_SLOT` + `ABS_MT_TRACKING_ID`，事件带 `fingers` 计数；
  单指设备（只有 `ABS_X/ABS_Y` + `BTN_TOUCH`）与鼠标自动降级为同一套语义。
- **速度**：`vx/vy` 为一阶平滑后的 px/ms，供惯性滚动与甩动判定，避免瞬时抖动。
- **防误触**：`SYN_REPORT` 才合并移动事件；移动超过 6 px 立即取消长按；
  拖动期间不产生 `TAP`，所以滚动不会误点开按钮。
- **按压反馈**：控件按下时绘制高亮底色，抬手即消失。

---

## 3. `.jspk` 包格式

`.jspk` 是一个 ZIP 容器（deflate），结构固定：

```
manifest.json      应用元信息
ui/main.json       声明式界面（可拆多个文件）
res/*              资源（可选，当前版本未使用位图）
```

不依赖动态库、不需要编译，第三方只写 JSON 就能出应用；需要原生能力时用
`run` 动作调用系统命令（受 `permissions` 限制）。

### 3.1 manifest.json

```json
{
  "id": "miao.byusistudio.settings",
  "name": "设置",
  "version": "2.5",
  "icon": "settings",
  "color": "#3584E4",
  "developer": "ByUsi Studio · 北啊呢",
  "summary": "系统设置",
  "entry": "ui/main.json",
  "permissions": ["run"],
  "categories": ["system"],
  "init": { "brightness": 70 }
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | 是 | 反向域名，`miao.byusistudio.*` 为官方前缀 |
| `name` | 是 | 桌面显示名 |
| `entry` | 是 | 界面描述文件路径 |
| `icon` | 否 | 内置矢量图标名（见下表） |
| `color` | 否 | 图标底色 `#RRGGBB` |
| `permissions` | 否 | `run`（执行命令）、`url`（打开链接） |
| `init` | 否 | 首次运行的初始状态键值 |

内置图标：`phone message browser camera settings files terminal music photos
clock weather maps mail calc record notes store about search wifi battery
bluetooth airplane brightness volume lock power back menu close check plus
minus grid download shield chip update user refresh trash edit play pause
skip signal dnd rotate`。

### 3.2 界面描述

```json
{
  "title": "设置",
  "start": "root",
  "pages": {
    "root": {
      "title": "设置",
      "widgets": [
        { "t": "group", "title": "网络与互联网" },
        { "t": "switch", "icon": "wifi", "title": "WLAN", "sub": "ProperOS-5G",
          "key": "wifi", "setting": "wifi" },
        { "t": "row", "icon": "brightness", "title": "显示与亮度", "to": "display" }
      ],
      "footer": [
        { "t": "button", "text": "完成", "kind": "primary", "action": { "type": "back" } }
      ]
    },
    "display": { "title": "显示与亮度", "widgets": [ ... ] }
  }
}
```

- `pages` 是页面字典，`start` 为入口页。
- 每个页面可有 `title`、`widgets`、`footer`（固定在底部的按钮组）、`bar`（顶栏高度）。
- 页面内内容超出屏幕时自动支持拖动滚动与惯性。

### 3.3 控件

| `t` | 主要字段 | 说明 |
|---|---|---|
| `group` | `title` | 分组小标题 |
| `row` | `icon title sub value to action` | 列表行；有 `to`/`action` 时显示右箭头 |
| `switch` | `key title sub icon setting` | 开关行；`key` 绑定状态，`setting` 同步到系统设置 |
| `slider` | `key title min max value setting` | 滑块，拖动即写状态 |
| `button` | `text kind` | `primary` / `secondary` / `danger` / `ghost` |
| `text` | `text px align color empty` | 段落；`align` 支持 `left/right/center`；`empty` 是变量为空时的占位文字 |
| `note` | `text` | 左侧竖条的提示块 |
| `card` | `title children` | 卡片容器 |
| `grid` | `cols cell icon items` 或 `src arg` | 图标网格；带 `src` 时由原生数据源现场生成条目 |
| `rows` | `src arg rowh empty` | 动态列表：条目来自 `native.c`（目录、音频、包、接口、会话…），点击执行条目自带动作 |
| `filetext` | `text` | 多行文本预览（文件应用读入的真实内容） |
| `term` | — | 真终端占满整页：伪终端 + VT 解析 + 触摸键盘 |
| `tiles` | `cols cell items` | 圆角色块（磁贴），适合计算器、快捷入口；`items[].span` 可横向跨列（如计算器的 `=`） |
| `progress` | `pct text` | 进度条 |
| `input` | `key title hint password` | 输入框，点击弹出软键盘 |
| `keyvalue` | `key value` | 键值行（关于页） |
| `stats` | `items[].value/label` | 三栏统计卡 |
| `image` | `icon title sub` | 大图标 + 标题 |
| `divider` / `spacer` | `h` | 分隔线 / 空白 |

尺寸单位是 **dp**，运行时按屏幕密度换算（720×1280 为 2.0，1080×1920 为 3.0）。

文本里可用模板：`{{time}}`、`{{date}}`、`{{battery}}`、`{{wifi}}`、`{{model}}`、
`{{kernel}}`、`{{mem}}`、`{{disk}}`、`{{rootdisk}}`、`{{uptime}}`、`{{cpu}}`、`{{temp}}`、`{{media}}`、`{{sw}}`，
以及任意状态变量名（如计算器的 `{{expr}}`）。前一组全部是 `/proc`、`/sys`、`statvfs` 的实时读数。

### 3.4 动作

```json
{ "type": "nav",  "to": "display" }
{ "type": "back" }
{ "type": "close" }
{ "type": "toast", "text": "已保存" }
{ "type": "set", "key": "name", "value": "x" }
{ "type": "toggle", "key": "wifi" }
{ "type": "append", "key": "expr", "text": "7" }
{ "type": "bs", "key": "expr" }
{ "type": "clear", "key": "expr" }
{ "type": "calc", "key": "result" }
{ "type": "setting", "key": "dark", "value": "1" }
{ "type": "run", "cmd": "properos-bootctl status" }
{ "type": "url", "url": "https://example.org" }
{ "type": "copy", "text": "..." }
{ "type": "call", "number": "10086" }
{ "type": "native", "op": "fs.up" }
{ "type": "native", "op": "clock.add", "arg": "{{newalarm}}" }
{ "type": "sw.start" }    /* 秒表：start / stop / reset 三个动作 */
```

- `to` 字段是 `{"type":"nav","to":...}` 的简写。
- `calc` 不传 `expr` 时使用变量 `expr` 的值，结果写入 `key`（默认 `result`）。
- `setting` 写入 `/data/home/.config/properos/system.conf`，桌面与快捷设置立即读取。
- `run` / `url` 需要 `permissions` 里声明对应权限。

### 3.5 状态持久化

每个应用的状态保存在 `/data/home/.config/<id>/state.conf`，格式为 `key=value` 每行一条。
`init` 里的键只在文件里不存在时写入，因此升级不会覆盖用户设置。

**系统级键单独处理**：`wifi`、`bt`、`dnd`、`airplane`、`auto_rotate`、
`battery_saver`、`brightness`、`volume`、`dark`、`wallpaper`、`timeout`、
`flash`、`hotspot`、`mobile_data`、`sound`、`vibrate` 这些键统一读写
`/data/home/.config/properos/system.conf`。桌面快捷设置和设置应用看到的是同一份数据，
不会出现「桌面开着 Wi-Fi、设置里显示关」的矛盾。应用退出后桌面会重读配置并即时生效。

### 3.6 调试

```bash
# 离屏渲染某一页，不用开虚拟机
src/build/x86_64/properos-app build/apps/settings.jspk --render 720x1280 /tmp/a.bmp display

# 把手势事件写进内核日志（串口可读），排查触摸不灵
PROPEROS_JSPK_DEBUG=1 properos-app /usr/share/properos/apps/settings.jspk
```

---

## 4. 开发与打包

```bash
# 目录结构
apps/settings/
├── manifest.json
└── ui/main.json

# 校验 + 打包（会检查 entry 是否存在、to 是否指向真实页面）
python3 tools/jspk-pack.py apps/settings

# 打包全部系统应用到 build/apps/
python3 tools/jspk-pack.py --all

# 本地预览（离屏渲染成 BMP，不需要 framebuffer）
src/build/x86_64/properos-app apps/settings --render 720x1280 /tmp/s.png 1
```

`--render WxH out.bmp [第几页]` 会按 `pages` 的声明顺序压栈到指定页，用于截图核对版式。

安装到系统：

```bash
make -C src ARCH=arm64 install DESTDIR=/root/build/rootfs-arm64
# 会执行 jspk-pack.py --all 并把 *.jspk 装到 /usr/share/properos/apps
```

---

## 5. 系统应用清单

| 应用 | id | 说明 |
|---|---|---|
| 终端 | `miao.byusistudio.terminal` | 真 PTY 终端：bash 登录会话、256 色、回看、触摸键盘 |
| 设置 | `miao.byusistudio.settings` | 真实状态与硬件控制：Wi-Fi 开关、亮度、音量、分区、电池 |
| 文件 | `miao.byusistudio.files` | 真实目录浏览、容量、文本预览、建目录 |
| 时钟 | `miao.byusistudio.clock` | 实时时间、四地时区、可开关的闹钟表、真实秒表 |
| 计算器 | `miao.byusistudio.calc` | 四则运算，表达式实时显示 |
| 电话 | `miao.byusistudio.phone` | modem 检测、拨号、真实通话记录 |
| 消息 | `miao.byusistudio.message` | 本机会话、真实发送状态 |
| 相机 | `miao.byusistudio.camera` | V4L2 设备探测与真实拍照（PPM） |
| 音乐 | `miao.byusistudio.music` | 扫描本地音频、真实 PCM 播放 |
| 应用商店 | `miao.byusistudio.store` | papp/.jspk/本地 deb·ppkg 三类真实列表 |
| 欢迎 | `miao.byusistudio.welcome` | 本机信息与上手引导 |

数据来源与动作细节见 `docs/11-应用与终端.md`。

新增系统应用只需在 `apps/` 下建目录、写两个 JSON、重新 `make install`，
不需要改桌面代码——桌面扫描目录即可发现。

---

## 6. 运行时实现

`src/jspk/` 四个文件，约 2600 行：

| 文件 | 作用 |
|---|---|
| `inflate.c` | DEFLATE 解压（RFC 1951）。交叉工具链没有 zlib 静态库，自带实现 |
| `zip.c` | ZIP 中央目录解析、条目读取（store / deflate） |
| `json.c` | 递归下降 JSON 解析，UTF-8 直通，支持 `\uXXXX` 与代理对 |
| `jspk.c` | 包加载、布局测量、控件绘制、动作分发、滚动与手势、软键盘 |

设计约束与取舍：

- **不引入脚本引擎**：界面是纯数据，动作用固定的语义集合表达。这样启动时间在
  10 ms 量级，内存占用约 3 MB，也不会因为第三方脚本拖垮桌面。
- **两遍布局**：`measure_widget()` 先算高度，绘制时按滚动偏移定位；屏幕外的控件
  跳过绘制但保留占位，保证滚动条与点击位置稳定。
- **测量与绘制共用同一组公式**，否则会出现“看得到点不到”。
- 所有尺寸走 `DP()` 换算，字体走运行时字号变量，同一份 JSON 在 720p 手机与
  1280×800 平板上都保持合理密度。

---

## 7. 已知限制

- 界面是声明式的，**没有任意绘图与自定义控件**；需要复杂图形的应用仍应写成原生程序。
- 资源目录 `res/` 尚未接入（当前只用内置矢量图标），图片控件是占位。
- 软键盘为内置 QWERTY + 数字页，没有输入法联想与多语言键盘。
- 窗口模型是全屏单任务，没有分屏与浮窗（与 Plasma Mobile 一致，但与桌面 Plasma 不同）。
- 系统应用的部分数据（文件列表、通话记录、短信）为演示数据，真实数据源在路线图中。
