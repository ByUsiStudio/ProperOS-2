#!/usr/bin/env python3
"""用修正后的脚本启动会话，每 15 秒采样一次，直到出画面或超时。"""
import base64
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
FIXED = open("/workspace/properos-2.5/overlay/usr/bin/properos-session", "rb").read()

try:
    print("等待开机（100s）", flush=True)
    time.sleep(100)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    b64 = base64.b64encode(FIXED).decode()
    t.run(f"printf '%s' {b64} | base64 -d > /usr/bin/properos-session; chmod 755 /usr/bin/properos-session", 25)
    t.run("systemctl stop properos-shell 2>/dev/null; systemctl stop properos-session 2>/dev/null; "
          "pkill -x kwin_wayland; sleep 2; systemctl start properos-session", 30)

    for i in range(8):
        time.sleep(15)
        vm.shot(q, "f%d" % i, save=True)
        print("t=%ds" % ((i + 1) * 15), flush=True)

    print("=== kwin / egl 日志 ===", flush=True)
    print(t.run("journalctl -b --no-pager | grep -iE 'egl|opengl|gbm|swrast|composit|backend|fatal|failed' | tail -20", 30), flush=True)
    print("=== 进程 ===", flush=True)
    print(t.run("pgrep -a kwin_wayland | head -2; pgrep -a plasmashell | head -2", 15), flush=True)
    print("=== 配置文件 ===", flush=True)
    print(t.run("ls -l /root/.config/plasma* 2>&1 | head", 15), flush=True)
finally:
    vm.stop()
