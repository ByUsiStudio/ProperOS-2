#!/usr/bin/env python3
"""同一台虚拟机里轮换 Mesa/KWin 环境组合，找出能上屏的一组。"""
import base64
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()

VARIANTS = [
    ("A_kms_swrast", 'export MESA_LOADER_DRIVER_OVERRIDE=kms_swrast\nunset LIBGL_ALWAYS_SOFTWARE\n'),
    ("B_libgl_only",  'unset MESA_LOADER_DRIVER_OVERRIDE\nexport LIBGL_ALWAYS_SOFTWARE=1\n'),
    ("C_swrast",      'export MESA_LOADER_DRIVER_OVERRIDE=swrast\nunset LIBGL_ALWAYS_SOFTWARE\n'),
    ("D_quick_soft",  'export MESA_LOADER_DRIVER_OVERRIDE=kms_swrast\nunset LIBGL_ALWAYS_SOFTWARE\n'
                      'export QT_QUICK_BACKEND=software\n'),
]


def write_wrapper(t, inject):
    s = "#!/bin/sh\n" + inject + "exec /bin/sh /tmp/sess.orig\n"
    b64 = base64.b64encode(s.encode()).decode()
    t.run(f"printf '%s' {b64} | base64 -d > /usr/bin/properos-session; chmod 755 /usr/bin/properos-session", 20)


def restart(t, inject):
    t.run("systemctl stop properos-session 2>/dev/null; pkill -x kwin_wayland; pkill -x plasmashell; sleep 2", 20)
    write_wrapper(t, inject)
    t.run("systemctl start properos-session; sleep 32", 50)


try:
    print("等待开机（100s）", flush=True)
    time.sleep(100)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)
    t.run("systemctl stop properos-shell 2>/dev/null; cp -f /usr/bin/properos-session /tmp/sess.orig", 20)

    for name, inject in VARIANTS:
        print("=== %s ===" % name, flush=True)
        restart(t, inject)
        time.sleep(10)
        vm.shot(q, name, save=True)
        info = t.run("pgrep -x kwin_wayland >/dev/null && echo KWIN || echo NONE", 12)
        log = t.run("journalctl -b --no-pager | grep -iE 'egl|opengl|gbm|swrast|composit|failed' | tail -6", 20)
        print(name, info.strip(), flush=True)
        print(log.strip()[:800], flush=True)
        t.run("cp -f /tmp/sess.orig /usr/bin/properos-session", 10)
finally:
    t.run("cp -f /tmp/sess.orig /usr/bin/properos-session 2>/dev/null; true", 10)
    vm.stop()
