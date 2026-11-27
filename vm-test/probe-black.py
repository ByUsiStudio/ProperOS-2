#!/usr/bin/env python3
"""黑屏诊断：横屏对比 + plasmashell 日志。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待 150 秒", flush=True)
    for i in range(3):
        time.sleep(50)
        vm.shot(q, "h%d" % i, save=True)

    t.wait_for(b"login:", 60)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 会话 ===", flush=True)
    print(t.run("systemctl is-active properos-session; "
                "ps -ef | grep -E 'plasmashell|kwin_wayland|startplasma' | grep -v grep | head -5", 30), flush=True)

    print("=== plasmashell 日志 ===", flush=True)
    print(t.run("journalctl -b _COMM=plasmashell --no-pager | tail -25", 40), flush=True)

    print("=== 图形 ===", flush=True)
    print(t.run("ls /sys/class/drm/ 2>/dev/null; "
                "cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null; "
                "cat /sys/class/drm/card0-Virtual-1/status 2>/dev/null; "
                "ls -l /dev/dri 2>/dev/null", 20), flush=True)

    print("=== 会话日志 ===", flush=True)
    print(t.run("journalctl -b -u properos-session --no-pager | tail -20", 30), flush=True)
finally:
    vm.stop()
