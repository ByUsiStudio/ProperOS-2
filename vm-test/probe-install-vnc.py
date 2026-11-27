#!/usr/bin/env python3
"""用真实 VNC 指针事件操作安装器（QMP input-send-event 在 8.2 里不进 evdev）。"""
import importlib.util, time
from vncdotool import api

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
c = None
try:
    print("等待安装器（165s）", flush=True)
    time.sleep(165)
    vm.shot(q, "s0")

    c = api.connect("127.0.0.1::5907")

    def tap(x, y):
        c.mouseMove(x, y)
        time.sleep(0.2)
        c.mouseDown(1)
        time.sleep(0.12)
        c.mouseUp(1)
        time.sleep(0.8)

    print("点击 开始安装", flush=True)
    tap(520, 999)
    time.sleep(3)
    vm.shot(q, "s1")

    print("选磁盘 + 继续", flush=True)
    tap(330, 453)
    time.sleep(1.2)
    tap(520, 999)
    time.sleep(3)
    vm.shot(q, "s2")

    print("勾选不设密码 + 继续", flush=True)
    tap(47, 624)
    time.sleep(1)
    tap(520, 999)
    time.sleep(3)
    vm.shot(q, "s3")

    print("现在安装", flush=True)
    tap(520, 999)
    for i in range(14):
        time.sleep(30)
        vm.shot(q, "p%d" % i)
finally:
    try:
        if c: c.disconnect()
    except Exception:
        pass
    vm.stop()
