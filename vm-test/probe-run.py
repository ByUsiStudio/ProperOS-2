#!/usr/bin/env python3
"""在 QEMU arm64 上运行 fbprobe，逐阶段截屏判定哪种提交手段有效"""
import json, os, shutil, socket, subprocess, sys, time
from collections import Counter
from PIL import Image

ISO = "/workspace/properos-2.5/dist/ProperOS-2.5-arm64.iso"
PAYLOAD = "/root/build/payload.img"
DISK = "/root/build/probe.qcow2"
QMP = "/tmp/qmp-probe.sock"
PORT = 4471
OUT = "/tmp/shots"

os.makedirs(OUT, exist_ok=True)
subprocess.run(["qemu-img", "create", "-f", "qcow2", DISK, "8G"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
varsf = "/tmp/AAVMF_VARS-probe.fd"
if not os.path.exists(varsf):
    shutil.copy("/usr/share/AAVMF/AAVMF_VARS.fd", varsf)

args = [
    "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72",
    "-m", "3072", "-smp", "2",
    "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,file={varsf}",
    "-drive", f"if=none,id=hd0,file={DISK},format=qcow2",
    "-device", "virtio-blk-pci,drive=hd0",
    "-drive", f"if=none,id=hd1,file={PAYLOAD},format=raw,readonly=on",
    "-device", "virtio-blk-pci,drive=hd1",
    "-device", "virtio-gpu-pci",
    "-device", "virtio-keyboard-pci",
    "-drive", f"if=none,id=cd0,media=cdrom,file={ISO},format=raw,readonly=on",
    "-device", "virtio-scsi-pci,id=scsi0",
    "-device", "scsi-cd,drive=cd0,bus=scsi0.0",
    "-serial", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-qmp", f"unix:{QMP},server,nowait",
    "-vnc", "127.0.0.1:7",
    "-nic", "none",
]
p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
serial_log = open("/root/build/probe-serial.log", "wb")


class Q:
    def __init__(self):
        self.buf = b""
        for _ in range(50):
            if os.path.exists(QMP):
                sk = socket.socket(socket.AF_UNIX)
                try:
                    sk.connect(QMP)
                    sk.settimeout(8)
                    self.s = sk
                    self._msg(); self.cmd("qmp_capabilities"); return
                except OSError as e:
                    sk.close()
                    err = e
            time.sleep(0.2)
        raise SystemExit(f"qmp fail: {err}")

    def _msg(self):
        while b"\n" in self.buf:
            l, self.buf = self.buf.split(b"\n", 1)
            if l.strip(): return json.loads(l)
        while True:
            d = self.s.recv(65536)
            self.buf += d
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                if l.strip(): return json.loads(l)

    def cmd(self, n, **a):
        r = {"execute": n}
        if a: r["arguments"] = a
        self.s.sendall((json.dumps(r) + "\n").encode())
        while True:
            m = self._msg()
            if "return" in m or "error" in m: return m


def shot(q, name):
    path = f"{OUT}/{name}.ppm"
    q.cmd("screendump", filename=path)
    try:
        im = Image.open(path).convert("RGB")
    except Exception as e:
        print(f"  {name}: 截图失败 {e}"); return None
    small = im.resize((64, 40))
    px = list(small.getdata())
    nz = sum(1 for c in px if c != (0, 0, 0))
    top = Counter(px).most_common(2)
    print(f"  {name}: {im.size} non-black={nz}/{len(px)} top={top}")
    im.save(f"{OUT}/{name}.png")
    return im


try:
    q = Q()
    s = None
    for attempt in range(3):
        try:
            s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
            break
        except OSError:
            time.sleep(1)
    s.settimeout(1)

    def drain(t=25):
        buf = b""
        end = time.time() + t
        while time.time() < end:
            try:
                d = s.recv(8192)
                if not d: break
                buf += d
            except socket.timeout:
                continue
        serial_log.write(buf); serial_log.flush()
        return buf.decode("utf-8", "replace")

    def run(cmd, t=20, mark="EOC"):
        s.sendall(f"printf '\\n<<B>>\\n'; {cmd}; printf '\\n<<{mark}>>\\n'\n".encode())
        out = (drain(t)).replace("\r", "")
        return out.split("<<B>>")[-1].split(f"<<{mark}>>")[0]

    print("== 等待登录提示")
    buf = b""
    end = time.time() + 150
    while time.time() < end:
        try:
            d = s.recv(8192)
            buf += d
        except socket.timeout:
            if b"login:" in buf:
                break
    serial_log.write(buf); serial_log.flush()
    print("  登录提示:", b"login:" in buf)

    s.sendall(b"root\n")
    time.sleep(2)
    out = drain(10)
    print("  登录后:", out.replace("\r", "")[-300:])
    s.sendall(b"stty -echo\n"); time.sleep(0.5); drain(2)

    print("== 环境信息")
    print(run("fgconsole; ls /dev/dri/ 2>&1; ls /dev/fb* 2>&1; cat /proc/fb; "
              "cat /sys/class/vtconsole/*/name 2>/dev/null; ls /dev/vd*", 20))
    print("== 停掉安装器")
    print(run("systemctl stop properos-live-installer; sleep 1; pgrep -a properos || echo none", 40))
    print(run("dmesg | grep -iE 'virtio_gpu|virtio-gpu|fb0|drm' | tail -20", 20))

    print("== 运行 fbprobe")
    s.sendall(b"mount -t ext4 /dev/vda /mnt 2>/dev/null || mount -t ext4 /dev/vdb /mnt; "
              b"ls /mnt | head; nohup /mnt/fbprobe > /tmp/probe.out 2>&1 &\n")
    time.sleep(3)
    shot(q, "p00-before")
    for i in range(16):
        time.sleep(1.5)
        shot(q, f"p{i:02d}")
    print("== probe 输出")
    print(run("cat /tmp/probe.out", 15))
finally:
    p.terminate()
    serial_log.close()
