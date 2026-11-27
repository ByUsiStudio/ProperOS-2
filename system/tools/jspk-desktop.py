#!/usr/bin/env python3
"""把 apps/*.jspk 应用注册进桌面环境（生成 .desktop）

Plasma Mobile 的应用抽屉按 .desktop 收集应用，.jspk 包本身只说明界面，
所以装机时要为每个包生成一条入口，Exec 指向 properos-jspk。

用法: jspk-desktop.py <输出目录> [apps 目录]
"""
import json
import os
import sys

# manifest.icon（自绘字形 id）→ Breeze 主题图标名
ICON_MAP = {
    "settings": "preferences-system",
    "files": "system-file-manager",
    "clock": "clock",
    "calc": "accessories-calculator",
    "phone": "phone",
    "message": "internet-chat",
    "camera": "camera-photo",
    "music": "media-optical-audio",
    "store": "system-software-install",
    "welcome": "help-about",
    "terminal": "utilities-terminal",
}

CAT_MAP = {
    "system": "Settings;System;",
    "application": "Utility;",
    "media": "AudioVideo;",
    "network": "Network;",
    "utility": "Utility;",
}


def gen(apps_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    for name in sorted(os.listdir(apps_dir)):
        mf = os.path.join(apps_dir, name, "manifest.json")
        if not os.path.isfile(mf):
            continue
        with open(mf, encoding="utf-8") as f:
            m = json.load(f)
        aid = m.get("id") or ("miao.byusistudio." + name)
        title = m.get("name", name)
        summary = m.get("summary", "")
        icon = ICON_MAP.get(m.get("icon", ""), "application-x-executable")
        cats = m.get("categories") or ["application"]
        category = "".join(CAT_MAP.get(c, "") for c in cats) or "Utility;"
        pkg = "/usr/share/properos/apps/%s.jspk" % name

        body = (
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=%s\n"
            "Name[zh_CN]=%s\n"
            "Comment=%s\n"
            "Comment[zh_CN]=%s\n"
            "Exec=/usr/bin/properos-jspk %s\n"
            "TryExec=/usr/bin/properos-jspk\n"
            "Icon=%s\n"
            "Terminal=false\n"
            "Categories=%s\n"
            "StartupNotify=false\n"
            "StartupWMClass=properos-jspk\n"
            "X-ProperOS-Jspk=%s\n"
        ) % (title, title, summary, summary, pkg, icon, category, aid)

        out = os.path.join(out_dir, aid + ".desktop")
        with open(out, "w", encoding="utf-8") as f:
            f.write(body)
        n += 1
    print("生成 %d 个 .desktop 到 %s" % (n, out_dir))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("用法: jspk-desktop.py <输出目录> [apps 目录]")
    here = os.path.dirname(os.path.abspath(__file__))
    gen(sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "apps"),
        sys.argv[1])
