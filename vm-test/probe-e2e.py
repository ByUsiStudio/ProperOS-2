#!/usr/bin/env python3
"""端到端：phoneshell 是否上屏、终端窗口、触摸键盘输入 ls。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待 Plasma 上屏（150s）", flush=True)
    for i in range(3):
        time.sleep(50)
        vm.shot(q, "q%d" % i, save=True)

    t.wait_for(b"login:", 60)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 加载的 shell ===", flush=True)
    print(t.run("grep -E 'Shell|shell' /root/.config/plasmashellrc; "
                "ps -ef | grep plasmashell | grep -v grep | head -2; "
                "journalctl -b _COMM=plasmashell --no-pager | grep -iE 'phoneshell|nano|shellpackage' | head -5", 40),
          flush=True)

    print("=== 打开终端窗口 ===", flush=True)
    print(t.run("export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; "
                "( setsid /usr/bin/properos-jspk /usr/share/properos/apps/terminal.jspk "
                ">/tmp/tj.log 2>&1 & ); sleep 12; pgrep -a properos-jspk; "
                "echo '--- log ---'; head -6 /tmp/tj.log", 50), flush=True)
    vm.shot(q, "win0")
    time.sleep(2)
    vm.shot(q, "win1")

    print("=== 点击终端区域，唤起键盘 ===", flush=True)
    vm.click(q, 360, 300)
    time.sleep(2)
    vm.shot(q, "kb0")

    print("=== 软键盘输入 ls + 回车 ===", flush=True)
    vm.click(q, 612, 1061)     # l
    time.sleep(0.4)
    vm.click(q, 108, 1061)     # s
    time.sleep(0.4)
    vm.click(q, 612, 1233)     # 回车
    time.sleep(3)
    vm.shot(q, "kb1")
    time.sleep(2)
    vm.shot(q, "kb2")
finally:
    vm.stop()
