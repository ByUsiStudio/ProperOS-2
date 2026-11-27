import importlib.util, time
spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec); spec.loader.exec_module(vm)
q = vm.Q(); t = vm.Term()
try:
    time.sleep(85)
    t.wait_for(b"login:", 60)
    t.send("root\n"); time.sleep(3); t.drain(6)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)
    print(t.run("mount /dev/vdb /mnt && cp /mnt/properos-jspk /usr/bin/ && chmod 755 /usr/bin/properos-jspk && "
                "mkdir -p /opt/properos && cp -a /mnt/apps /opt/properos/apps && ls /opt/properos/apps | tr '\\n' ' '", 40), flush=True)
    print(t.run("cat /sys/class/tty/tty0/active; ls /run/user/0/wayland-*", 12), flush=True)
    print("=== 启动 ===", flush=True)
    print(t.run("XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 setsid /usr/bin/properos-jspk "
                "/opt/properos/apps/terminal >/tmp/app.log 2>&1 < /dev/null & sleep 15; echo done", 35), flush=True)
    print("=== 日志与状态 ===", flush=True)
    print(t.run("cat /tmp/app.log; echo ---PGREP---; pgrep -a properos-jspk | head -3; echo ---VT---; cat /sys/class/tty/tty0/active", 20), flush=True)
    vm.shot(q, "win0")
    time.sleep(18)
    vm.shot(q, "win1")
    print(t.run("pgrep -c properos-jspk; cat /sys/class/tty/tty0/active", 12), flush=True)
finally:
    vm.stop()
