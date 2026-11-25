# 09 · 桌面与 .jspk 应用

本文说明 ProperOS 2.5 的桌面外壳形态、触摸手势，以及 `.jspk` 应用包格式与开发方式。

- 桌面：`properos-shell`（原生，framebuffer 直绘）
- 应用：`properos-jspk`（运行时）+ `*.jspk`（应用包）
- 系统应用：`apps/` 目录，构建时打包并在安装时自动部署到 `/usr/share/properos/apps`

---

## 1. 桌面形态

参考 **KDE Plasma Mobile**，但保留普通 Linux 桌面的结构，而不是做成 Android 的平铺图标墙：

| 区域 | 内容 | 说明 |
|---|---|---|
| 顶部面板 | 时钟、WLAN/信号/电量、勿扰与蓝牙指示 | 点击右侧区域或下拉打开快捷设置 |
| 主屏 | 壁纸 + 分页图标网格（竖屏 4×4，横屏 6×3） | 虚拟桌面，左右滑动翻页 |
| 底部 Dock | 抽屉按钮 + 5 个固定应用 | 固定位由应用 id 匹配，缺省按名称补齐 |
| 应用抽屉 | 全屏网格 + 搜索框 | 上滑或点 Dock 左侧按钮打开 |
| 快捷设置 | 亮度/音量滑块 + 8 个磁贴 | 顶部下拉打开 |
| 长按菜单 | 打开 / 应用信息 / 添加到主屏 / 卸载 | 图标长按 520 ms |

壁纸有三套（`mist` / `dusk` / `bamboo`），在「设置 → 主屏与壁纸」切换，写入
`/data/home/.config/properos/system.conf` 的 `wallpaper=` 键，桌面下一帧生效。

桌面本身不实现窗口管理：应用以全屏方式运行（手机形态），退出后回到主屏。
这与 Plasma Mobile 的行为一致，也是触摸设备上最省事、最不容易误操作的模型。

### 安装位置

```
/usr/share/properos/apps/*.jspk     系统应用（随镜像安装，只读分区）
/data/apps/<pkg>/manifest.conf      papp 旧式应用（兼容）
/data/home/Desktop/*.plink          用户快捷方式（原系统格式）
```

`properos-shell` 启动时扫描这三处，合并成应用列表；`.jspk` 的清单直接从 ZIP 里读，
不解包、不执行代码即可显示图标与名称。

---

## 2. 触摸与手势

输入层 `src/gfx/input.c` 把 evdev 事件归一化为语义手势，桌面与应用都消费同一套：

| 手势 | 触发条件 | 行为 |
|---|---|---|
| 点击 | 位移 < 36 px 且未长按 | 打开应用 / 触发控件 |
| 长按 | 按住 520 ms 未移动 | 图标菜单、编辑 |
| 左右滑 | 水平位移 ≥ 36 px | 主屏翻页（跟手 + 边缘阻尼） |
| 顶部下拉 | 起手在顶部 1/12 区域且下滑 | 打开快捷设置 |
| 底部上滑 | 起手在底部 1/12 区域且上滑 | 打开应用抽屉 |
| 左边缘内滑 | 起手在左侧 1/22 区域右滑 | 应用内返回 / 关闭浮层 |
| 右边缘内滑 | 起手在右侧 1/22 区域左滑 | 同上 |
| 面板内上滑 | 快捷设置打开时向上甩动 | 收起面板回桌面 |
| 抽屉内下滑 | 应用抽屉打开时向下甩动 | 关闭抽屉回桌面 |

实现要点：

- **多指追踪**：`ABS_MT_SLOT` + `ABS_MT_TRACKING_ID`，事件带 `fingers` 计数；
  单指设备（只有 `ABS_X/ABS_Y` + `BTN_TOUCH`）与鼠标自动降级为同一套语义。
- **速度**：`vx/vy` 为一阶平滑后的 px/ms，供惯性滚动与甩动判定，避免瞬时抖动。
- **防误触**：`SYN_REPORT` 才合并移动事件；移动超过 6 px 立即取消长按；
  拖动期间不产生 `TAP`，所以滚动不会误点开应用。
- **按压反馈**：图标/按钮按下时绘制高亮底色，抬起即消失。

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
| `grid` | `cols cell icon items` | 图标网格，`items[].title/icon/tint/to/action` |
| `tiles` | `cols cell items` | 圆角色块（磁贴），适合计算器、快捷入口；`items[].span` 可横向跨列（如计算器的 `=`） |
| `progress` | `pct text` | 进度条 |
| `input` | `key title hint password` | 输入框，点击弹出软键盘 |
| `keyvalue` | `key value` | 键值行（关于页） |
| `stats` | `items[].value/label` | 三栏统计卡 |
| `image` | `icon title sub` | 大图标 + 标题 |
| `divider` / `spacer` | `h` | 分隔线 / 空白 |

尺寸单位是 **dp**，运行时按屏幕密度换算（720×1280 为 2.0，1080×1920 为 3.0）。

文本里可用模板：`{{time}}`、`{{date}}`、`{{battery}}`、`{{wifi}}`、`{{model}}`，
以及任意状态变量名（如计算器的 `{{expr}}`）。

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
src/build/x86_64/properos-jspk build/apps/settings.jspk --render 720x1280 /tmp/a.bmp display

# 把手势事件写进内核日志（串口可读），排查触摸不灵
PROPEROS_JSPK_DEBUG=1 properos-jspk /usr/share/properos/apps/settings.jspk
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
src/build/x86_64/properos-jspk apps/settings --render 720x1280 /tmp/s.png 1
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
| 设置 | `miao.byusistudio.settings` | 网络、显示、声音、电池、个性化、存储、更新、关于 |
| 文件 | `miao.byusistudio.files` | 存储概览、分类、最近文件 |
| 时钟 | `miao.byusistudio.clock` | 大字时间、世界时钟、闹钟、秒表 |
| 计算器 | `miao.byusistudio.calc` | 四则运算，表达式实时显示 |
| 电话 | `miao.byusistudio.phone` | 拨号盘、通话记录 |
| 消息 | `miao.byusistudio.message` | 会话列表与会话页 |
| 相机 | `miao.byusistudio.camera` | 取景占位、拍照/录像入口 |
| 音乐 | `miao.byusistudio.music` | 播放器界面 |
| 应用商店 | `miao.byusistudio.store` | 已安装列表、jspk 说明 |
| 欢迎 | `miao.byusistudio.welcome` | 首次启动引导 |

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
