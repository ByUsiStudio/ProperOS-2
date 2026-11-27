#!/usr/bin/env python3
"""看 ISO 安装器实际画面。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
try:
    for i in range(4):
        time.sleep(30)
        vm.shot(q, "i%d" % i)
finally:
    vm.stop()
