#!/usr/bin/env python3
"""确认镜像里的桌面到底是 Plasma phoneshell 还是 framebuffer 兜底。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待开机（110s）", flush=True)
    time.sleep(110)
    vm.shot(q, "p0", save=True)

    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 桌面进程 ===", flush=True)
    print(t.run("pgrep -a plasmashell | head -3; echo ---; pgrep -a kwin | head -3; "
                "echo ---; pgrep -a properos-shell | head -3", 30), flush=True)

    print("=== 会话与 socket ===", flush=True)
    print(t.run("systemctl is-active properos-session; echo ---; "
                "ls -l /run/user/0/wayland-0 2>&1; echo ---; "
                "cat /root/.config/plasmashellrc 2>&1", 30), flush=True)

    print("=== plasmashell 日志 ===", flush=True)
    print(t.run("journalctl -b _COMM=plasmashell --no-pager 2>/dev/null | tail -12; "
                "echo '=== session log ==='; journalctl -b -u properos-session --no-pager | tail -20", 40), flush=True)

    print("=== 设备 ===", flush=True)
    print(t.run("ls -l /dev/dri 2>&1 | head; echo ---; cat /sys/class/graphics/fb0/name 2>&1; "
                "echo ---; fbset 2>/dev/null | head -3", 25), flush=True)

    time.sleep(15)
    vm.shot(q, "p1", save=True)
finally:
    vm.stop()
