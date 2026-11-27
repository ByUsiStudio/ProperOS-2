#!/usr/bin/env python3
"""ARM64 串口交互测试：登录并执行验证命令"""
import socket, sys, time, re

HOST, PORT = "127.0.0.1", 4444
CMDS = [
    "uname -m; uname -r",
    "cat /proc/cmdline | tr ' ' '\\n' | grep properos",
    "systemctl is-system-running",
    "ls -l /usr/lib/properos/ /usr/bin/papp /usr/sbin/properd 2>&1 | head -8",
    "properos-bootctl status 2>&1 | head -12",
    "properos-shell --list 2>&1 | head -6",
    "papp --version; ppkg --version",
    "ip -4 addr show | grep inet | head -3",
    "podman --version; podman info --format '{{.Host.Arch}} {{.Host.CgroupVersion}}' 2>&1 | tail -2",
    "free -h | head -2",
]

def connect():
    for _ in range(120):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(1)
    raise SystemExit("无法连接串口")

def read_until(s, needles, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        try:
            d = s.recv(8192)
            if not d:
                break
            buf += d
            for n in needles:
                if n in buf:
                    return buf
        except socket.timeout:
            continue
    return buf

def main():
    s = connect()
    print("=== 等待登录提示 ===", flush=True)
    buf = read_until(s, [b"login:", b"# "], 300)
    text = buf.decode("utf-8", "replace")
    print(text[-1200:], flush=True)
    if b"login:" in buf:
        s.sendall(b"root\n")
        buf = read_until(s, [b"# ", b"Password:"], 60)
        print(buf.decode("utf-8", "replace")[-500:], flush=True)
        s.sendall(b"\n")
        time.sleep(1)
        s.sendall(b"stty -echo 2>/dev/null; export TERM=dumb\n")
        time.sleep(1)

    for c in CMDS:
        print(f"\n===== {c}", flush=True)
        s.sendall(f"echo @@BEGIN@@; {c}; echo @@END@@\n".encode())
        out = read_until(s, [b"@@END@@"], 180).decode("utf-8", "replace")
        out = out.replace("\r", "")
        if "@@BEGIN@@" in out:
            out = out.split("@@BEGIN@@", 1)[1]
        out = out.split("@@END@@")[0]
        print(out.strip(), flush=True)

if __name__ == "__main__":
    main()
