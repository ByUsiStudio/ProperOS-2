#!/usr/bin/env python3
"""验证真终端与真实数据：直接引导新磁盘镜像，串口核对 + 屏幕截图。"""
import importlib.util, time

spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)

q = vm.Q()
t = vm.Term()
try:
    print("等待 Plasma 上屏", flush=True)
    for i in range(3):
        time.sleep(50)
        vm.shot(q, "p%d" % i, save=True)

    if not t.wait_for(b"login:", 60):
        print("串口没有 login 提示", flush=True)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print("=== 会话与应用 ===", flush=True)
    print(t.run("systemctl is-active properos-session; "
                "ls /usr/share/properos/apps/ | tr '\\n' ' '; echo", 40), flush=True)

    print("=== 终端：离屏渲染 + 打印网格 ===", flush=True)
    print(t.run("PROPEROS_TERM_FOCUS=1 PROPEROS_TERM_DUMP=1 SHELL=/bin/bash "
                "properos-jspk /usr/share/properos/apps/terminal.jspk "
                "--render 720x1280 /tmp/t.bmp 1 2>&1 | head -8", 90), flush=True)

    print("=== 终端：真跑几条命令 ===", flush=True)
    print(t.run("ls -l /tmp/t.bmp; cd /; echo TERM=$TERM", 20), flush=True)

    print("=== 在 Plasma 里打开终端窗口 ===", flush=True)
    print(t.run("export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; "
                "( setsid /usr/bin/properos-jspk /usr/share/properos/apps/terminal.jspk "
                ">/tmp/tj.log 2>&1 & ); sleep 14; pgrep -a properos-jspk; "
                "echo '--- log ---'; head -5 /tmp/tj.log", 60), flush=True)
    vm.shot(q, "term0")
    time.sleep(3)
    vm.shot(q, "term1")

    print("=== 点击终端区域唤起键盘 ===", flush=True)
    vm.click(q, 640, 300)
    time.sleep(2)
    vm.shot(q, "term_kb")
    vm.click(q, 95, 520)      # 键盘第一行第二格附近（数字 1）
    time.sleep(1)
    vm.shot(q, "term_kb2")

    print("=== 文件应用离屏渲染 ===", flush=True)
    print(t.run("properos-jspk /usr/share/properos/apps/files.jspk "
                "--render 720x1280 /tmp/f.bmp 1; ls -l /tmp/f.bmp", 60), flush=True)
finally:
    vm.stop()
