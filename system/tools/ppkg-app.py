#!/usr/bin/env python3
"""把 apps/<名字>/ 打成 .ppkg（ProperOS 的系统包格式）

从 25.0.0 起，应用开发以 .ppkg 交付：一个包 = PKGINFO + FILES + payload/，
`ppkg install` 会把 payload/ 铺到 /。运行时的界面描述仍然是 JSON，但开发、
分发、校验、卸载全部走 ppkg 这一条路——.jspk 只作为旧包的兼容读取保留。

包内布局（payload 展开后）：

    /opt/properos/apps/<名字>/manifest.json
    /opt/properos/apps/<名字>/ui/main.json
    /usr/share/applications/properos-<名字>.desktop

用法:
    ppkg-app.py build <app 目录> <输出.ppkg> [--version 25.0.0] [--key 私钥]
    ppkg-app.py all   <apps 目录> <输出目录> [--version 25.0.0]
    ppkg-app.py bundle <apps 目录> <输出.ppkg> [--version 25.0.0]

开发者：ByUsi Studio · 北啊呢
"""
import argparse
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile

APP_PREFIX = "/opt/properos/apps"
DESKTOP_DIR = "/usr/share/applications"

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


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def read_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def desktop_entry(man, name):
    aid = man.get("id") or ("miao.byusistudio." + name)
    title = man.get("name", name)
    summary = man.get("summary", "")
    icon = ICON_MAP.get(man.get("icon", ""), "application-x-executable")
    cats = man.get("categories") or ["application"]
    category = "".join(CAT_MAP.get(c, "") for c in cats) or "Utility;"
    target = "%s/%s" % (APP_PREFIX, name)
    return (
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=%s\n"
        "Name[zh_CN]=%s\n"
        "Comment=%s\n"
        "Comment[zh_CN]=%s\n"
        "Icon=%s\n"
        "Exec=/usr/bin/properos-app %s\n"
        "TryExec=/usr/bin/properos-app\n"
        "Terminal=false\n"
        "Categories=%s\n"
        "StartupNotify=true\n"
        "StartupWMClass=properos-app\n"
        "X-ProperOS-App=%s\n"
        "X-ProperOS-Package=properos-app-%s\n"
    ) % (title, title, summary, summary, icon, target, category, aid, name)


def collect_payload(apps_dir, names):
    """返回 {payload 内路径: bytes}"""
    out = {}
    for name in names:
        root = os.path.join(apps_dir, name)
        man = read_json(os.path.join(root, "manifest.json"))
        for dirpath, _dirs, files in os.walk(root):
            for f in sorted(files):
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, root)
                dst = "%s/%s/%s" % (APP_PREFIX, name, rel)
                with open(full, "rb") as fh:
                    out[dst] = fh.read()
        out["%s/properos-%s.desktop" % (DESKTOP_DIR, name)] = desktop_entry(man, name).encode()
    return out


def add_bytes(tar, path, data, mode=0o644):
    ti = tarfile.TarInfo(path)
    ti.size = len(data)
    ti.mode = mode
    ti.mtime = 0
    ti.uid = ti.gid = 0
    ti.uname = ti.gname = "root"
    tar.addfile(ti, io.BytesIO(data))


def make_package(out, pkg_name, version, description, payload, depends, key=None):
    files = []
    for path in sorted(payload):
        files.append("%s  %s\n" % (sha256_bytes(payload[path]), path.lstrip("/")))
    files_txt = "".join(files).encode()

    pkginfo = (
        "name=%s\n"
        "version=%s\n"
        "arch=any\n"
        "format=payload\n"
        "kind=app\n"
        "description=%s\n"
        "depends=%s\n"
    ) % (pkg_name, version, description, depends)

    sig = None
    if key:
        with tempfile.NamedTemporaryFile(delete=False) as tf:
            tf.write(files_txt)
            fl = tf.name
        sigf = fl + ".sig"
        subprocess.run(["openssl", "dgst", "-sha256", "-sign", key, "-out", sigf, fl], check=True)
        with open(sigf, "rb") as fh:
            sig = fh.read()
        os.unlink(fl)
        os.unlink(sigf)

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with tarfile.open(out, "w:gz", format=tarfile.GNU_FORMAT) as tar:
        add_bytes(tar, "PKGINFO", pkginfo.encode())
        add_bytes(tar, "FILES", files_txt)
        if sig:
            add_bytes(tar, "SIGNATURE", sig)
        for path in sorted(payload):
            mode = 0o755 if path.endswith((".sh", ".py")) else 0o644
            add_bytes(tar, "payload" + path, payload[path], mode)
    return out


def app_names(apps_dir):
    return sorted(
        d for d in os.listdir(apps_dir)
        if os.path.isfile(os.path.join(apps_dir, d, "manifest.json"))
    )


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("mode", choices=["build", "all", "bundle"])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--version", default="25.0.0")
    ap.add_argument("--key", default=None, help="Ed25519/RSA 私钥（openssl dgst -sign）")
    a = ap.parse_args()

    if a.mode == "build":
        name = os.path.basename(os.path.abspath(a.src))
        man = read_json(os.path.join(a.src, "manifest.json"))
        payload = collect_payload(os.path.dirname(os.path.abspath(a.src)), [name])
        out = make_package(a.dst, "properos-app-" + name, a.version,
                           man.get("summary", ""), payload, "properos-app", a.key)
        print("%s  %d 文件  %.1f KB" % (out, len(payload), os.path.getsize(out) / 1024.0))
        return

    if a.mode == "all":
        os.makedirs(a.dst, exist_ok=True)
        for name in app_names(a.src):
            man = read_json(os.path.join(a.src, name, "manifest.json"))
            payload = collect_payload(a.src, [name])
            out = os.path.join(a.dst, "properos-app-%s-%s.ppkg" % (name, a.version))
            make_package(out, "properos-app-" + name, a.version,
                         man.get("summary", ""), payload, "properos-app", a.key)
            print("  %-40s %.1f KB" % (os.path.basename(out), os.path.getsize(out) / 1024.0))
        return

    # bundle：一个包装全部应用，装机用
    names = app_names(a.src)
    payload = collect_payload(a.src, names)
    make_package(a.dst, "properos-apps", a.version,
                 "ProperOS 系统应用集（%d 个）" % len(names), payload, "properos-app", a.key)
    print("%s  %d 个应用  %d 文件  %.1f KB" %
          (a.dst, len(names), len(payload), os.path.getsize(a.dst) / 1024.0))


if __name__ == "__main__":
    sys.exit(main())
