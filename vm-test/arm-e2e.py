#!/usr/bin/env python3
"""ARM64 端到端验证：系统状态 / 网络 / 启动器 / 应用沙箱 / 容器"""
import socket, time

CMDS = [
    "systemctl is-system-running; systemctl --failed --no-legend | head -5",
    "ip -4 addr show eth0 | grep inet",
    "ping -c1 -W3 10.0.2.2 >/dev/null 2>&1 && echo NET_OK || echo NET_FAIL",
    "ls -l /dev/fb0 /dev/dri/card0 2>&1 | head -3",
    "properos-shell --info 2>&1",
    "properos-bootctl status | sed -n '1,9p'",
    "cat /run/properos/services.json | head -c 300; echo",
    "mkdir -p /tmp/demo/root/app && printf 'id=miao.byusistudio.hello\\nname=你好\\nversion=1.0.0\\nentry=/app/run.sh\\nnetwork=0\\n' > /tmp/demo/manifest.conf && printf '#!/bin/sh\\necho APP_OK uid=$(id -u) home=$HOME\\n' > /tmp/demo/root/app/run.sh && chmod +x /tmp/demo/root/app/run.sh && papp pack /tmp/demo /tmp/hello.papp >/dev/null && papp install /tmp/hello.papp | tail -1 && papp run miao.byusistudio.hello 2>&1 | tail -2",
    "mkdir -p /tmp/mr && cp /bin/busybox /tmp/mr/ && tar -C /tmp/mr -cf /tmp/mini.tar . && podman import /tmp/mini.tar local/mini:1 >/dev/null 2>&1; podman run --rm local/mini:1 /busybox echo ARM_CONTAINER_OK 2>&1 | tail -2",
    "podman run --rm -p 8080:80 local/mini:1 /busybox echo ARM_CONTAINER_NET_OK 2>&1 | tail -2",
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

for _ in range(60):
    try:
        s = socket.create_connection(("127.0.0.1", 4444), timeout=5)
        break
    except OSError:
        time.sleep(2)

s.settimeout(1)
b = read_until(s, [b"login:", b"# "], 240)
if b"login:" in b:
    s.sendall(b"root\n")
    b = read_until(s, [b"# "], 40)
    if b"Password" in b: s.sendall(b"\n")
    time.sleep(1)
s.sendall(b"stty -echo 2>/dev/null; export TERM=dumb\n")

for c in CMDS:
    print(f"\n===== {c}", flush=True)
    s.sendall(f"echo @@B@@; {c}; echo @@E@@\n".encode())
    out = read_until(s, [b"@@E@@"], 240).decode("utf-8", "replace").replace("\r", "")
    if "@@B@@" in out: out = out.split("@@B@@", 1)[1]
    print(out.split("@@E@@")[0].strip(), flush=True)
