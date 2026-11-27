#!/usr/bin/env python3
"""分离诊断：键盘事件(QMP sendkey) 与 指针事件(VNC) 是否到达 guest evdev。"""
import importlib.util, time
from vncdotool import api

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
c = None
try:
    time.sleep(150)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 安装器打开的文件 ===", flush=True)
    print(t.run("p=$(pgrep -x properos-setup | head -1); echo pid=$p; "
                "ls -l /proc/$p/fd 2>/dev/null | grep -c input; "
                "ls -l /proc/$p/fd 2>/dev/null | grep input", 30), flush=True)

    print("=== 键盘：QMP sendkey ===", flush=True)
    t.run("( timeout 8 cat /dev/input/event1 > /tmp/k1.bin 2>/dev/null & ); sleep 1", 10)
    q.cmd("human-monitor-command", **{"command-line": "sendkey a"})
    time.sleep(2)
    q.cmd("human-monitor-command", **{"command-line": "sendkey b"})
    time.sleep(4)

    print("=== 指针：VNC 点击 ===", flush=True)
    t.run("( timeout 10 cat /dev/input/event2 > /tmp/m2.bin 2>/dev/null & ); sleep 1", 12)
    c = api.connect("127.0.0.1::5907")
    c.mouseMove(520, 999); time.sleep(0.3)
    c.mouseDown(1); time.sleep(0.15); c.mouseUp(1)
    time.sleep(5)

    print(t.run("ls -l /tmp/k1.bin /tmp/m2.bin; echo '--- k1 ---'; od -An -tx1 /tmp/k1.bin | head -6; "
                "echo '--- m2 ---'; od -An -tx1 /tmp/m2.bin | head -8", 30), flush=True)
    vm.shot(q, "d1")
finally:
    try:
        if c: c.disconnect()
    except Exception:
        pass
    vm.stop()
