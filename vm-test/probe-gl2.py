#!/usr/bin/env python3
"""在同一个会话脚本上轮换环境组合，找出 KWin 能真正 scanout 的一组。

A 组（kms_swrast + 不设 LIBGL_ALWAYS_SOFTWARE + 不设 KWIN_COMPOSE）已确认
只有黑屏，日志里是 DRM_IOCTL_MODE_CREATE_DUMB 被拒。这里测 B/C/D。
"""
import base64
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()

BASE = open("/workspace/properos-2.5/overlay/usr/bin/properos-session", "rb").read().decode()

VARIANTS = [
    ("B_kwinQ", "export MESA_LOADER_DRIVER_OVERRIDE=kms_swrast\nunset LIBGL_ALWAYS_SOFTWARE\nexport KWIN_COMPOSE=Q\n"),
    ("C_stock", ""),
    ("D_libgl", "unset MESA_LOADER_DRIVER_OVERRIDE\nexport LIBGL_ALWAYS_SOFTWARE=1\n"),
]


def write_script(t, inject):
    s = "#!/bin/sh\n" + inject + "exec /bin/sh /tmp/sess.base\n"
    b64 = base64.b64encode(s.encode()).decode()
    t.run("printf '%s' " + b64 + " | base64 -d > /usr/bin/properos-session; chmod 755 /usr/bin/properos-session", 20)


def restart(t, inject):
    t.run("systemctl stop properos-session 2>/dev/null; pkill -x kwin_wayland; pkill -x plasmashell; sleep 3", 25)
    write_script(t, inject)
    t.run("systemctl start properos-session", 20)


try:
    print("等待开机（100s）", flush=True)
    time.sleep(100)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    b64 = base64.b64encode(BASE.encode()).decode()
    t.run("printf '%s' {b} | base64 -d > /tmp/sess.base; chmod 755 /tmp/sess.base".format(b=b64), 20)
    t.run("systemctl stop properos-shell 2>/dev/null; systemctl stop properos-session 2>/dev/null; pkill -x kwin_wayland; sleep 2", 25)

    for name, inject in VARIANTS:
        print("=== %s ===" % name, flush=True)
        restart(t, inject)
        for k in range(4):
            time.sleep(12)
            vm.shot(q, "%s_%d" % (name, k), save=True)
            print("  %s t=%ds" % (name, (k + 1) * 12), flush=True)
        print(t.run("journalctl -b --no-pager | grep -iE 'CREATE_DUMB|egl|opengl|gbm|swrast|composit|fatal' | tail -5", 25), flush=True)
        t.run("cp -f /tmp/sess.base /usr/bin/properos-session", 10)
finally:
    t.run("cp -f /tmp/sess.base /usr/bin/properos-session 2>/dev/null; true", 10)
    vm.stop()
