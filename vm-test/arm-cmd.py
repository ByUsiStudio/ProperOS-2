#!/usr/bin/env python3
"""在已启动的 ARM64 串口连接上执行命令"""
import socket, sys, time

CMDS = [
    "systemctl --failed --no-legend",
    "systemctl list-jobs --no-legend | head -10",
    "systemctl status properos-firstboot.service --no-pager -n 5 2>&1 | tail -12",
    "systemctl status properd.service --no-pager -n 5 2>&1 | tail -10",
    "ls /etc/systemd/network/ 2>&1; ip link show | head -6",
    "ls -l /dev/fb0 /dev/dri 2>&1",
    "properos-bootctl mark-good; properos-bootctl status | head -8",
    "cat /run/properos/services.json 2>&1 | head -3",
    "podman info --format '{{.Host.Arch}} {{.Host.CgroupControllers}}' 2>&1 | tail -2",
]

def read_until(s, needles, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        try:
            d = s.recv(8192)
            if not d: break
            buf += d
            if any(n in buf for n in needles): return buf
        except socket.timeout:
            continue
    return buf

s = socket.create_connection(("127.0.0.1", 4444), timeout=5)
s.settimeout(1)
b = read_until(s, [b"# "], 60)
if b"login:" in b:
    s.sendall(b"root\n"); b = read_until(s, [b"# "], 30); s.sendall(b"\n")

for c in CMDS:
    print(f"\n===== {c}", flush=True)
    s.sendall(f"echo @@B@@; {c}; echo @@E@@\n".encode())
    out = read_until(s, [b"@@E@@"], 90).decode("utf-8", "replace").replace("\r", "")
    if "@@B@@" in out: out = out.split("@@B@@", 1)[1]
    print(out.split("@@E@@")[0].strip(), flush=True)
