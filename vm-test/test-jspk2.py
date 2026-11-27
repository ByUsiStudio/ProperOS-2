import importlib.util, time
spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec); spec.loader.exec_module(vm)
q = vm.Q(); t = vm.Term()
try:
    time.sleep(85)
    t.wait_for(b"login:", 60)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)
    print(t.run("cat /sys/class/tty/tty0/active; pgrep -a kwin_wayland | head -1", 15), flush=True)
    print(t.run("mkdir -p /mnt; mount /dev/vdb /mnt && cp /mnt/properos-jspk /usr/bin/properos-jspk && "
                "chmod 755 /usr/bin/properos-jspk && md5sum /usr/bin/properos-jspk", 25), flush=True)
    print("=== 旧行为对照：当前 VT ===", flush=True)
    print(t.run("cat /sys/class/tty/tty0/active", 8), flush=True)
    print("=== 启动 terminal 应用 ===", flush=True)
    print(t.run("XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 setsid /usr/bin/properos-jspk "
                "/opt/properos/apps/terminal >/tmp/app.log 2>&1 < /dev/null & sleep 12; echo start-ok", 30), flush=True)
    print("=== 日志 ===", flush=True)
    print(t.run("cat /tmp/app.log; echo ---; pgrep -a properos-jspk | head -3; echo ---; cat /sys/class/tty/tty0/active", 20), flush=True)
    time.sleep(15)
    vm.shot(q, "jspk-win")
    print(t.run("cat /sys/class/tty/tty0/active; pgrep -c properos-jspk", 12), flush=True)
finally:
    vm.stop()
