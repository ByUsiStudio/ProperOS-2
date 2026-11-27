#!/usr/bin/env python3
"""手动停掉 framebuffer 桌面，直接起 Plasma 会话，确认能否上屏。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待开机（105s）", flush=True)
    time.sleep(105)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 停掉 shell，启动会话 ===", flush=True)
    print(t.run("systemctl stop properos-shell; sleep 2; "
                "systemctl start properos-session; sleep 3; "
                "systemctl is-active properos-session", 40), flush=True)

    for i in range(7):
        time.sleep(30)
        vm.shot(q, "k%d" % i, save=True)
        info = t.run("pgrep -x kwin_wayland >/dev/null && echo KWIN_OK || echo no_kwin; "
                     "pgrep -x plasmashell >/dev/null && echo PLASMA_OK || echo no_plasma", 15)
        print("t=%ds %s" % ((i + 1) * 30, " ".join(info.split())), flush=True)

    print("=== 会话日志 ===", flush=True)
    print(t.run("journalctl -u properos-session -b --no-pager | tail -25", 30), flush=True)
    print("=== startplasma 输出 ===", flush=True)
    print(t.run("ls -l /run/user/0/ 2>&1 | head; echo ---; "
                "journalctl -b _COMM=kwin_wayland --no-pager 2>/dev/null | tail -10; echo ---; "
                "journalctl -b _COMM=plasmashell --no-pager 2>/dev/null | tail -12", 35), flush=True)
    vm.shot(q, "final", save=True)
finally:
    vm.stop()
