#!/usr/bin/env python3
"""验证修正后的会话脚本能上屏，并读取 Plasma homescreen 配置结构。"""
import base64
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()

FIXED = open("/workspace/properos-2.5/overlay/usr/bin/properos-session", "rb").read()

try:
    print("等待开机（100s）", flush=True)
    time.sleep(100)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    b64 = base64.b64encode(FIXED).decode()
    t.run(f"printf '%s' {b64} | base64 -d > /usr/bin/properos-session; chmod 755 /usr/bin/properos-session; "
          "systemctl stop properos-shell 2>/dev/null; "
          "systemctl stop properos-session 2>/dev/null; pkill -x kwin_wayland; sleep 2", 30)
    t.run("systemctl start properos-session; sleep 30", 45)
    vm.shot(q, "fixed", save=True)

    print("=== homescreen containment ===", flush=True)
    print(t.run("grep -n 'Containments\\|plugin=\\|favorites' /root/.config/plasma-org.kde.plasma.desktop-appletsrc 2>/dev/null | head -40", 25), flush=True)

    print("=== 应用 desktop 文件 ===", flush=True)
    print(t.run("ls /usr/share/applications/*.desktop | head -20", 20), flush=True)

    print("=== 抽屉里的应用 ===", flush=True)
    print(t.run("ls /usr/share/applications/ | wc -l", 15), flush=True)
finally:
    vm.stop()
