#!/usr/bin/env python3
"""给 Plasma Mobile 桌面预置收藏应用，验证后把配置取回做默认值。"""
import base64
import importlib.util, re, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()

DESKTOPS = [
    "org.kde.phone.dialer.desktop",
    "properos-terminal.desktop",
    "properos-files.desktop",
    "properos-message.desktop",
    "properos-camera.desktop",
    "properos-store.desktop",
    "properos-settings.desktop",
    "properos-clock.desktop",
    "properos-music.desktop",
    "properos-calc.desktop",
]

CFG = "/root/.config/plasma-org.kde.plasma.phoneshell-appletsrc"


def grab(path):
    out = t.run("base64 -w0 %s 2>/dev/null | head -c 20000" % path, 25)
    for line in out.splitlines():
        line = line.strip()
        if len(line) > 20 and re.fullmatch(r"[A-Za-z0-9+/=]+", line):
            try:
                return base64.b64decode(line).decode("utf-8", "replace")
            except Exception:
                pass
    return ""


try:
    print("等待开机（100s）", flush=True)
    time.sleep(100)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 桌面项 ===", flush=True)
    print(t.run("ls /usr/share/applications/ | head -30", 20), flush=True)

    print("=== 原 appletsrc ===", flush=True)
    cfg = grab(CFG)
    print(cfg, flush=True)

    # 在 homescreen applet 的 [Configuration][General] 下写 favorites
    m = re.search(r"\[Containments\]\[(\d+)\]\[Applets\]\[(\d+)\]\[\w+\]\nplugin=org\.kde\.phone\.homescreen", cfg)
    if not m:
        m = re.search(r"plugin=org\.kde\.phone\.homescreen", cfg)
        print("!! 没找到 homescreen applet 段，cfg 如上", flush=True)
    else:
        c, a = m.group(1), m.group(2)
        fav = ",".join("applications:" + d for d in DESKTOPS)
        sect = "[Containments][%s][Applets][%s][Configuration][General]\nfavorites=%s\n" % (c, a, fav)
        new = cfg
        if "[Containments][%s][Applets][%s][Configuration][General]" % (c, a) in cfg:
            # 已有段：替换/追加 favorites
            head, rest = cfg.split("[Containments][%s][Applets][%s][Configuration][General]" % (c, a), 1)
            lines = rest.splitlines()
            if lines and lines[0].strip() == "":
                lines = lines[1:]
            body, tail = [], []
            for ln in lines:
                if ln.startswith("[") and body:
                    tail.append(ln)
                elif not body:
                    body.append(ln)
                else:
                    tail.append(ln)
            body = [ln for ln in body if not ln.startswith("favorites=")]
            body.append("favorites=" + fav)
            new = head + "[Containments][%s][Applets][%s][Configuration][General]\n" % (c, a) + "\n".join(body).strip("\n") + "\n" + "\n".join(tail) + "\n"
        else:
            new = cfg.rstrip("\n") + "\n\n" + sect
        b64 = base64.b64encode(new.encode()).decode()
        t.run("printf '%s' " + b64 + " | base64 -d > " + CFG, 25)
        print("=== 写入后 ===", flush=True)
        print(grab(CFG), flush=True)

    t.run("systemctl stop properos-session 2>/dev/null; pkill -x kwin_wayland; pkill -x plasmashell; sleep 3; systemctl start properos-session", 25)
    for i in range(5):
        time.sleep(15)
        vm.shot(q, "fav%d" % i, save=True)
        print("t=%ds" % ((i + 1) * 15), flush=True)
finally:
    vm.stop()
