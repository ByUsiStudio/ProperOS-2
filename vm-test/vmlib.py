#!/usr/bin/env python3
"""QEMU arm64 测试小工具：QMP 截图 + 串口 shell"""
import json, os, shutil, socket, subprocess, time
from collections import Counter
from PIL import Image

ISO = os.environ.get("VM_ISO", "/workspace/properos-2.5/dist/ProperOS-25.0.0-arm64.iso")
PAYLOAD = os.environ.get("VM_PAYLOAD", "/root/build/payload.img")
DISK = os.environ.get("VM_DISK", "/root/build/vmrun.qcow2")
QMP = os.environ.get("VM_QMP", "/tmp/qmp-vm.sock")
PORT = int(os.environ.get("VM_PORT", "4475"))
OUT = os.environ.get("VM_OUT", "/tmp/shots")
SCREEN = os.environ.get("VM_SCREEN", "1280x800")
NIC = os.environ.get("VM_NIC", "none")
EXTRA = os.environ.get("VM_EXTRA", "")

os.makedirs(OUT, exist_ok=True)
if not os.environ.get("VM_KEEP_DISK"):
    subprocess.run(["qemu-img", "create", "-f", "qcow2", DISK, "8G"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

varsf = os.environ.get("VM_VARS", "/tmp/AAVMF_VARS-vmrun.fd")
if not os.path.exists(varsf):
    shutil.copy("/usr/share/AAVMF/AAVMF_VARS.fd", varsf)
if os.path.exists(QMP):
    os.unlink(QMP)

w, h = SCREEN.split("x")
args = [
    "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72",
    "-m", "3072", "-smp", "2",
    "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,file={varsf}",
]
if not os.environ.get("VM_NO_DISK"):
    args += ["-drive", f"if=none,id=hd0,file={DISK},format=qcow2",
             "-device", "virtio-blk-pci,drive=hd0"]
if PAYLOAD and os.path.exists(PAYLOAD):
    args += ["-drive", f"if=none,id=hd1,file={PAYLOAD},format=raw,readonly=on",
             "-device", "virtio-blk-pci,drive=hd1"]
args += [
    "-device", f"virtio-gpu-pci,xres={w},yres={h}",
    "-device", "virtio-keyboard-pci",
    "-device", "virtio-tablet-pci",
]
if not os.environ.get("VM_NO_CD"):
    args += ["-drive", f"if=none,id=cd0,media=cdrom,file={ISO},format=raw,readonly=on",
             "-device", "virtio-scsi-pci,id=scsi0",
             "-device", "scsi-cd,drive=cd0,bus=scsi0.0"]
args += [
    "-serial", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-qmp", f"unix:{QMP},server,nowait",
    "-vnc", "127.0.0.1:7",
    "-nic", NIC,
]
if EXTRA:
    args += EXTRA.split()

logpath = os.environ.get("VM_LOG", "/root/build/vmrun-serial.log")
log = open(logpath, "wb")
p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def stop():
    p.terminate()
    try:
        p.wait(10)
    except Exception:
        p.kill()
    log.close()


class Q:
    def __init__(self):
        self.buf = b""
        for _ in range(60):
            if os.path.exists(QMP):
                sk = socket.socket(socket.AF_UNIX)
                try:
                    sk.connect(QMP)
                    sk.settimeout(8)
                    self.s = sk
                    self._msg(); self.cmd("qmp_capabilities"); return
                except OSError:
                    sk.close()
            time.sleep(0.2)
        raise SystemExit("qmp fail")

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


def shot(q, name, save=True):
    path = f"{OUT}/{name}.ppm"
    q.cmd("screendump", filename=path)
    try:
        im = Image.open(path).convert("RGB")
    except Exception as e:
        print(f"  {name}: 失败 {e}"); return None
    small = im.resize((64, 40))
    px = list(small.getdata())
    nz = sum(1 for c in px if c != (0, 0, 0))
    print(f"  {name}: {im.size} non-black={nz}/{len(px)} top={Counter(px).most_common(3)}")
    if save:
        im.save(f"{OUT}/{name}.png")
    else:
        os.unlink(path)
    return im


class Term:
    def __init__(self):
        self.s = None
        for _ in range(20):
            try:
                self.s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
                break
            except OSError:
                time.sleep(1)
        self.s.settimeout(1)

    def drain(self, t=25):
        buf = b""
        end = time.time() + t
        while time.time() < end:
            try:
                d = self.s.recv(8192)
                if not d: break
                buf += d
            except socket.timeout:
                continue
        log.write(buf); log.flush()
        return buf.decode("utf-8", "replace")

    def wait_for(self, needle, t=180):
        buf = b""
        end = time.time() + t
        while time.time() < end:
            try:
                d = self.s.recv(8192)
                if not d: break
                buf += d
                if needle in buf: break
            except socket.timeout:
                continue
        log.write(buf); log.flush()
        return needle in buf

    def send(self, s):
        self.s.sendall(s.encode() if isinstance(s, str) else s)

    def run(self, cmd, t=20):
        self.send(f"printf '\\n<<B>>\\n'; {cmd}; printf '\\n<<EOC>>\\n'\n")
        out = self.drain(t).replace("\r", "")
        return out.split("<<B>>")[-1].split("<<EOC>>")[0]


def login(root="root", timeout=180, stop_setup=True):
    t = Term()
    ok = t.wait_for(b"login:", timeout)
    t.send(root + "\n"); time.sleep(2); t.drain(8)
    t.send("stty -echo; export TERM=linux\n"); time.sleep(0.5); t.drain(2)
    if stop_setup:
        t.run("systemctl stop properos-live-installer 2>/dev/null; mkdir -p /mnt; "
              "mount /dev/vdb /mnt 2>/dev/null; ls /mnt | head -5", 45)
    else:
        t.run("mkdir -p /mnt; mount /dev/vdb /mnt 2>/dev/null; pgrep -a properos", 20)
    return t


def click(q, x, y, w=1280, h=800):
    ax = int(x * 32767 / w)
    ay = int(y * 32767 / h)
    ev = [
        {"type": "abs", "data": {"axis": "x", "value": ax}},
        {"type": "abs", "data": {"axis": "y", "value": ay}},
    ]
    q.cmd("input-send-event", events=ev)
    time.sleep(0.12)
    q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
    time.sleep(0.12)
    q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
    time.sleep(0.45)
