#!/usr/bin/env python3
"""新镜像首启验证：Plasma Mobile 上屏、ppkg 应用、终端窗口。

顺序：开机 → 等桌面 → 截图 → 查 ppkg/应用目录 → 在会话里开终端并输入命令 → 截图。
"""
import base64
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()

try:
    print("等待开机（110s）", flush=True)
    time.sleep(110)
    vm.shot(q, "d0", save=True)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 服务 ===", flush=True)
    print(t.run("systemctl is-active properos-session graphical.target", 20), flush=True)
    print("=== 应用目录 ===", flush=True)
    print(t.run("ls /opt/properos/apps | tr '\\n' ' '; echo; ls /usr/share/applications/properos-*.desktop | wc -l", 20), flush=True)
    print("=== ppkg 数据库 ===", flush=True)
    print(t.run("ppkg list 2>&1 | tail -6", 25), flush=True)
    print("=== 包内文件 ===", flush=True)
    print(t.run("ls /opt/properos/apps/terminal; head -6 /usr/share/applications/properos-terminal.desktop", 20), flush=True)

    # 桌面就绪需要 60-120 秒（TCG 软件渲染）
    for i in range(6):
        time.sleep(20)
        vm.shot(q, "desk%d" % i, save=True)
        print("desk t=%ds" % ((i + 1) * 20), flush=True)

    print("=== 会话进程 ===", flush=True)
    print(t.run("pgrep -a kwin_wayland | head -1; pgrep -a plasmashell | head -1; ls /run/user/0/wayland-* 2>/dev/null", 20), flush=True)

    # 在 Plasma 会话里开应用（ppkg 目录形态）
    t.run("XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 nohup /usr/bin/properos-jspk /opt/properos/apps/terminal "
          ">/tmp/app.log 2>&1 &", 15)
    time.sleep(35)
    vm.shot(q, "app0", save=True)
    print("=== 应用日志 ===", flush=True)
    print(t.run("cat /tmp/app.log | tail -5; pgrep -a properos-jspk | head -2", 20), flush=True)

    # 点终端区域唤起键盘，再敲命令
    vm.click(q, 360, 400, 720, 1280)
    time.sleep(12)
    vm.shot(q, "app1", save=True)
    for k in ("u", "n", "a", "m", "e"):
        vm.key(q, k)
        time.sleep(0.6)
    vm.key(q, "ret")
    time.sleep(8)
    vm.shot(q, "app2", save=True)
finally:
    vm.stop()
