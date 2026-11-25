#!/usr/bin/env python3
"""ProperOS 2.5 · .jspk 打包工具

把一个应用目录打成 .jspk（ZIP + deflate）。包内结构：
  manifest.json   应用元信息
  ui/*.json       声明式界面
  res/*           资源（可选）

用法：
  tools/jspk-pack.py apps/settings                # 产出 apps/settings.jspk
  tools/jspk-pack.py --all                       # 打包 apps/* 到 build/apps/
  tools/jspk-pack.py apps/settings -o out.jspk

开发者：ByUsi Studio · 北啊呢
"""
import json
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def validate(src):
    man_path = os.path.join(src, "manifest.json")
    if not os.path.isfile(man_path):
        raise SystemExit(f"{src}: 缺少 manifest.json")
    with open(man_path, encoding="utf-8") as f:
        man = json.load(f)
    for k in ("id", "name", "entry"):
        if not man.get(k):
            raise SystemExit(f"{man_path}: 缺少字段 {k}")
    entry = os.path.join(src, man["entry"])
    if not os.path.isfile(entry):
        raise SystemExit(f"{src}: entry 指向的文件不存在：{man['entry']}")
    with open(entry, encoding="utf-8") as f:
        ui = json.load(f)
    if "pages" not in ui:
        raise SystemExit(f"{entry}: 缺少 pages")
    start = ui.get("start", "root")
    if start not in ui["pages"]:
        raise SystemExit(f"{entry}: start 指向的页面不存在：{start}")
    # 检查所有 to / action.to 是否指向存在的页面
    pages = ui["pages"]

    def check_action(a, where):
        if isinstance(a, dict):
            to = a.get("to") or (a.get("page") if a.get("type") in ("nav", "navigate") else None)
            if to and to not in pages:
                print(f"  ! {where}: 跳转到不存在的页面 {to}")

    def walk(node, where):
        if isinstance(node, dict):
            if "to" in node:
                check_action({"to": node["to"]}, where)
            if "action" in node:
                check_action(node["action"], where)
            for k, v in node.items():
                walk(v, where)
        elif isinstance(node, list):
            for i, v in enumerate(node):
                walk(v, f"{where}[{i}]")

    for name, page in pages.items():
        walk(page, f"{os.path.basename(entry)}:{name}")
    return man


def pack(src, out):
    man = validate(src)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(src):
            dirs[:] = [d for d in sorted(dirs) if not d.startswith(".")]
            for fn in sorted(files):
                if fn.startswith(".") or fn.endswith(".jspk"):
                    continue
                p = os.path.join(root, fn)
                rel = os.path.relpath(p, src).replace(os.sep, "/")
                z.write(p, rel)
    size = os.path.getsize(out)
    print(f"  {man['id']:<34} {man['name']:<8} {size/1024:6.1f} KiB  {out}")
    return out


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    if args[0] == "--all":
        src_dir = os.path.join(ROOT, "apps")
        out_dir = os.path.join(ROOT, "build", "apps")
        os.makedirs(out_dir, exist_ok=True)
        n = 0
        for name in sorted(os.listdir(src_dir)):
            d = os.path.join(src_dir, name)
            if not os.path.isdir(d):
                continue
            pack(d, os.path.join(out_dir, name + ".jspk"))
            n += 1
        print(f"共 {n} 个应用")
        return 0

    src = args[0]
    out = os.path.join(ROOT, "apps", os.path.basename(src.rstrip("/")) + ".jspk")
    if "-o" in args:
        out = args[args.index("-o") + 1]
    pack(src, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
