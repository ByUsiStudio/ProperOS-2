import importlib.util, time
spec = importlib.util.spec_from_file_location("vm", "/root/build/vmlib.py")
vm = importlib.util.module_from_spec(spec); spec.loader.exec_module(vm)
q = vm.Q(); t = vm.Term()
try:
    time.sleep(75)
    vm.shot(q, "v25b0")
    if t.wait_for(b"login:", 30):
        t.send("root\n"); time.sleep(3); t.drain(6)
        t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)
        print(t.run("cat /etc/os-release | head -3; pgrep -a kwin_wayland | head -1; ls /run/user/0/wayland-* 2>/dev/null; "
                    "systemctl is-active graphical.target", 25), flush=True)
    else:
        print("no login prompt", flush=True)
finally:
    vm.stop()
