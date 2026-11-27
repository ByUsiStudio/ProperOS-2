#!/usr/bin/env python3
"""KWin 在跑但黑屏：查后端、DRM 输出、GL 与 fbcon 状态。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待开机（100s）", flush=True)
    time.sleep(100)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    t.run("systemctl stop properos-shell 2>/dev/null; sleep 1; "
          "systemctl start properos-session; sleep 25", 45)

    print("=== 进程 ===", flush=True)
    print(t.run("ps -eo pid,args | grep -E 'kwin|plasmashell|startplasma' | grep -v grep", 20), flush=True)

    print("=== DRM 输出 ===", flush=True)
    print(t.run("for c in /sys/class/drm/card0-*; do echo -n \"$c \"; cat $c/status 2>/dev/null; done; "
                "echo ---; ls -l /dev/dri", 20), flush=True)

    print("=== KWin 日志 ===", flush=True)
    print(t.run("journalctl -b --no-pager | grep -iE 'kwin|opengl|egl|gbm|drm|compos' | tail -40", 40), flush=True)

    print("=== 会话完整日志 ===", flush=True)
    print(t.run("journalctl -u properos-session -b --no-pager | tail -30", 30), flush=True)

    print("=== KWin 打开的设备 ===", flush=True)
    print(t.run("P=$(pgrep -x kwin_wayland | head -1); echo pid=$P; "
                "ls -l /proc/$P/fd 2>/dev/null | grep -E 'dri|tty' | head", 20), flush=True)
finally:
    vm.stop()
