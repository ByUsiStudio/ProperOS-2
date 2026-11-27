#!/usr/bin/env python3
"""启动指定 ISO 并间隔截图，看引导链走到哪一步。"""
import importlib.util, os, sys, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

stamps = [int(x) for x in os.environ.get("STAMPS", "20,40,70,100,140").split(",")]
q = vm.Q()
try:
    last = 0
    for i, s in enumerate(stamps):
        time.sleep(max(0, s - last)); last = s
        vm.shot(q, "b%d" % i)
finally:
    vm.stop()
