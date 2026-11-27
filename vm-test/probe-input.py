#!/usr/bin/env python3
"""诊断 live 安装器是否能拿到 evdev 输入。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    time.sleep(150)
    vm.shot(q, "e0")

    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 输入设备 ===", flush=True)
    print(t.run("ls -l /dev/input/ 2>&1 | head -20; "
                "cat /proc/bus/input/devices 2>&1 | head -30", 40), flush=True)

    print("=== 安装器进程 ===", flush=True)
    print(t.run("ps -ef | grep -E 'properos-setup|systemd' | head -6; "
                "systemctl is-active properos-live-installer.service", 30), flush=True)

    print("=== 抓一次点击 ===", flush=True)
    t.run("( timeout 6 cat /dev/input/event1 > /tmp/e1.bin 2>/dev/null & ); "
          "( timeout 6 cat /dev/input/event2 > /tmp/e2.bin 2>/dev/null & ); sleep 1", 12)
    vm.click(q, 520, 999, 720, 1280)
    time.sleep(5)
    print(t.run("ls -l /tmp/e*.bin; for f in /tmp/e1.bin /tmp/e2.bin; do echo $f; "
                "od -An -tx1 $f | head -4; done", 30), flush=True)
finally:
    vm.stop()
