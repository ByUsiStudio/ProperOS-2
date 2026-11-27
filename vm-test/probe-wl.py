#!/usr/bin/env python3
"""诊断：为什么 properos-jspk 在 Plasma 会话里连不上 Wayland。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待 Plasma 上屏", flush=True)
    time.sleep(140)
    vm.shot(q, "d0")

    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 运行时目录 ===", flush=True)
    print(t.run("ls -la /run/user/ 2>&1; ls -la /run/user/*/wayland* 2>&1; "
                "id -u; echo HOME=$HOME", 40), flush=True)

    print("=== plasmashell 的环境变量 ===", flush=True)
    print(t.run("p=$(pgrep -x plasmashell | head -1); echo pid=$p; "
                "tr '\\0' '\\n' < /proc/$p/environ 2>/dev/null | "
                "grep -iE 'WAYLAND|XDG_RUNTIME|XDG_SESSION' ", 40), flush=True)

    print("=== kwin 的环境变量 ===", flush=True)
    print(t.run("p=$(pgrep -x kwin_wayland | head -1); echo pid=$p; "
                "tr '\\0' '\\n' < /proc/$p/environ 2>/dev/null | "
                "grep -iE 'WAYLAND|XDG_RUNTIME'", 40), flush=True)

    print("=== 用会话真实环境跑 jspk（WAYLAND_DEBUG）===", flush=True)
    print(t.run("p=$(pgrep -x plasmashell | head -1); "
                "WD=$(tr '\\0' '\\n' < /proc/$p/environ | sed -n 's/^WAYLAND_DISPLAY=//p'); "
                "XR=$(tr '\\0' '\\n' < /proc/$p/environ | sed -n 's/^XDG_RUNTIME_DIR=//p'); "
                "echo WD=$WD XR=$XR; "
                "WAYLAND_DEBUG=1 XDG_RUNTIME_DIR=$XR WAYLAND_DISPLAY=$WD "
                "timeout 10 /usr/bin/properos-jspk /usr/share/properos/apps/terminal.jspk "
                ">/tmp/tj2.log 2>&1; echo rc=$?; head -30 /tmp/tj2.log", 60), flush=True)
finally:
    vm.stop()
