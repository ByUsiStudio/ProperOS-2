#!/usr/bin/env python3
"""注入诊断版 jspk，抓完整 Wayland 失败点，并验证窗口是否真在 Plasma 里。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待 Plasma 上屏", flush=True)
    time.sleep(140)
    vm.shot(q, "w0")

    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 注入新二进制 ===", flush=True)
    print(t.run("mkdir -p /mnt/p; mount -o ro /dev/vdb /mnt/p && cp /mnt/p/properos-jspk "
                "/usr/bin/properos-jspk.new && chmod 755 /usr/bin/properos-jspk.new && "
                "cp /mnt/p/terminal.jspk /tmp/t.jspk && ls -l /usr/bin/properos-jspk.new", 40),
          flush=True)

    print("=== 带调试跑 25 秒（前台）===", flush=True)
    print(t.run("export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; "
                "WAYLAND_DEBUG=1 timeout 25 /usr/bin/properos-jspk.new /tmp/t.jspk "
                ">/tmp/tj3.log 2>&1; echo rc=$?; wc -l /tmp/tj3.log", 60), flush=True)
    vm.shot(q, "w1")

    print("=== 日志尾部（失败点）===", flush=True)
    print(t.run("grep -nE 'properos-wl|properos:|configure|error' /tmp/tj3.log | tail -20; "
                "echo '--- tail ---'; tail -12 /tmp/tj3.log", 50), flush=True)

    print("=== 后台常驻 + 截图 ===", flush=True)
    print(t.run("export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; "
                "( setsid /usr/bin/properos-jspk.new /tmp/t.jspk >/tmp/tj4.log 2>&1 & ); "
                "sleep 20; pgrep -a properos-jspk | head -3; head -4 /tmp/tj4.log", 50),
          flush=True)
    vm.shot(q, "w2")
finally:
    vm.stop()
