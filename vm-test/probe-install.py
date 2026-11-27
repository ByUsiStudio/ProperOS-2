#!/usr/bin/env python3
"""用图形安装器把系统装到一块空磁盘上，全程截图。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
try:
    print("等待安装器（165s）", flush=True)
    time.sleep(165)
    vm.shot(q, "s0")

    print("点击 开始安装", flush=True)
    vm.click(q, 520, 999, 720, 1280)
    time.sleep(4)
    vm.shot(q, "s1")

    print("选择磁盘 + 继续", flush=True)
    vm.click(q, 330, 453, 720, 1280)
    time.sleep(1.5)
    vm.click(q, 520, 999, 720, 1280)
    time.sleep(4)
    vm.shot(q, "s2")

    print("勾选不设密码 + 继续", flush=True)
    vm.click(q, 47, 624, 720, 1280)
    time.sleep(1)
    vm.click(q, 520, 999, 720, 1280)
    time.sleep(4)
    vm.shot(q, "s3")

    print("现在安装", flush=True)
    vm.click(q, 520, 999, 720, 1280)
    for i in range(12):
        time.sleep(40)
        vm.shot(q, "p%d" % i)
finally:
    vm.stop()
