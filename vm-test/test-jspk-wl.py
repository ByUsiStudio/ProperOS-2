#!/usr/bin/env python3
"""验证修复：Plasma Mobile 里启动 properos-jspk 应作为窗口出现，不再抢 VT。"""
import importlib.util, time
spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec); spec.loader.exec_module(vm)

q = vm.Q(); t = vm.Term()
try:
    print("等待开机", flush=True)
    time.sleep(105)
    t.wait_for(b"login:", 90)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)

    print(t.run("mkdir -p /mnt; mount /dev/vdb /mnt && ls /mnt", 25), flush=True)
    print(t.run("cp /mnt/properos-jspk /usr/bin/properos-jspk && chmod 755 /usr/bin/properos-jspk && "
                "md5sum /usr/bin/properos-jspk /mnt/properos-jspk", 25), flush=True)
    print("=== 会话 ===", flush=True)
    print(t.run("pgrep -a kwin_wayland | head -1; ls /run/user/0/wayland-* 2>/dev/null; "
                "pgrep -a plasmashell | head -1", 20), flush=True)

    for i in range(5):
        time.sleep(20)
        vm.shot(q, "jd%d" % i)

    print("=== 启动应用（模拟桌面点击 .desktop）===", flush=True)
    print(t.run("cat /usr/share/applications/properos-terminal.desktop | grep ^Exec", 15), flush=True)
    t.run("XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 nohup /usr/bin/properos-jspk "
          "/opt/properos/apps/terminal >/tmp/app.log 2>&1 &", 12)
    time.sleep(30)
    vm.shot(q, "japp0")
    print("=== 应用日志 ===", flush=True)
    print(t.run("cat /tmp/app.log; echo ---; pgrep -a properos-jspk | head -2", 20), flush=True)
    print("=== 当前 VT ===", flush=True)
    print(t.run("cat /sys/class/tty/tty0/active", 10), flush=True)
    time.sleep(10)
    vm.shot(q, "japp1")
finally:
    vm.stop()
