#!/usr/bin/env python3
"""验证 QMP 鼠标注入：input-send-event vs HMP mouse_move。"""
import importlib.util, time, json

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
try:
    time.sleep(25)
    print("input-send-event:", json.dumps(q.cmd(
        "input-send-event",
        events=[{"type": "abs", "data": {"axis": "x", "value": 23000}},
                {"type": "abs", "data": {"axis": "y", "value": 25000}},
                {"type": "btn", "data": {"down": True, "button": "left"}},
                {"type": "btn", "data": {"down": False, "button": "left"}}]), ensure_ascii=False))
    print("hmp mouse_move:", json.dumps(q.cmd(
        "human-monitor-command", **{"command-line": "mouse_move 520 999"}), ensure_ascii=False))
    print("hmp mouse_button:", json.dumps(q.cmd(
        "human-monitor-command", **{"command-line": "mouse_button 1"}), ensure_ascii=False))
finally:
    vm.stop()
