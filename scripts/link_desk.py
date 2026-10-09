#!/usr/bin/env python3
"""Desk mode: drives a bare Teensy (env teensy41-desk) over USB as the ESP would.

Speaks the link protocol of lib/core/src/link_frame.* / link_msg.*: COBS frames with crc16,
Hello, Time every 50 ms, EvBatch. Log frames from the Teensy are printed as they come.

  python3 -I scripts/link_desk.py hello
  python3 -I scripts/link_desk.py notes          # C-major arpeggio on track 0
  python3 -I scripts/link_desk.py status [secs]
  python3 -I scripts/link_desk.py fs [dir]       # card: list dir, write / read / remove a test file
  python3 -I scripts/link_desk.py update X.hex   # .hex onto the card, FwFromFile: the board reflashes itself
  python3 -I scripts/link_desk.py selftest       # codec only, no board

Needs pyserial: ~/.platformio/penv/bin/python has it (or pip install pyserial). --port /dev/cu.usbmodemXXXX if autodetect picks wrong.
"""
import argparse
import queue
import struct
import sys
import threading
import time

PROTOCOL = 1
MAX_PAYLOAD = 512

# lib/core/src/link_msg.h, enum class Msg (append only).
MSG = {name: i + 1 for i, name in enumerate(
    "Hello Time Ev StateSet Ack Nack "
    "FsOpen FsRead FsWrite FsClose FsStat FsList FsRemove FsRename FsMkdir "
    "BankSync AssetsSave BankImport SampleInfo BankIndex BankClear "
    "WavePeaks Onsets "
    "PreviewNote PreviewSlice PreviewFile PreviewStop "
    "RenderStart RenderBlocks RenderEnd "
    "Meters Phones Profile "
    "Status Progress FwFromFile Log "
    "FsRmdir WtFrame".split())}
MSG_NAME = {v: k for k, v in MSG.items()}

# lib/core/src/link_msg.h, kErr*.
FS_ERR = {0: "ok", -1: "no card", -2: "not found", -3: "exists", -4: "io", -5: "bad handle",
          -6: "too many open", -7: "bad argument", -8: "is a folder", -9: "not empty", -10: "no reply"}
FS_CHUNK = 480


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def cobs_encode(data):
    out = bytearray([0])
    code_pos, code = 0, 1
    for b in data:
        if b == 0:
            out[code_pos] = code
            code_pos, code = len(out), 1
            out.append(0)
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[code_pos] = code
                code_pos, code = len(out), 1
                out.append(0)
    out[code_pos] = code
    return bytes(out)


def cobs_decode(data):
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        i += 1
        if code == 0 or i + code - 1 > len(data):
            return None
        out += data[i:i + code - 1]
        i += code - 1
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def encode_frame(mtype, seq, payload):
    assert len(payload) <= MAX_PAYLOAD
    body = bytes([mtype, seq]) + payload
    return cobs_encode(body + struct.pack("<H", crc16(body))) + b"\x00"


def decode_frame(raw):
    """COBS bytes without the 0x00 -> (type, seq, payload) or None."""
    f = cobs_decode(raw)
    if f is None or len(f) < 4 or struct.unpack("<H", f[-2:])[0] != crc16(f[:-2]):
        return None
    return f[0], f[1], f[2:-2]


def now_us():
    return time.monotonic_ns() // 1000


def hello_payload():
    return struct.pack("<H16sII", PROTOCOL, b"desk", 0, 0)  # modelSize 0: no mirror


def parse_hello(p):
    proto, fw, boot, size = struct.unpack("<H16sII", p[:26])
    return proto, fw.split(b"\x00")[0].decode(errors="replace"), boot, size


def ev_batch(events):
    """events: [(t_us, track, bytes)] -> EvBatch payload (<= 40 events)."""
    base = min(t for t, _, _ in events)
    p = struct.pack("<QB", base, len(events))
    for t, track, b in events:
        p += struct.pack("<IBB", t - base, track, len(b)) + bytes(b)
    return p


def fs_str(s):
    b = s.encode()[:255]
    return bytes([len(b)]) + b


def parse_list(p):
    """FsList reply body (after err) -> (more, [(is_dir, size, name)])."""
    more, count = p[0], p[1]
    i, out = 2, []
    for _ in range(count):
        is_dir, size, n = struct.unpack("<BIB", p[i:i + 6])
        out.append((bool(is_dir), size, p[i + 6:i + 6 + n].decode(errors="replace")))
        i += 6 + n
    return bool(more), out


def parse_status(p):
    cpu, stalls, late, voices, out_peak = struct.unpack("<BHHBH", p[:8])
    peaks = list(p[8:24])
    lost, scope_n = struct.unpack("<HH", p[24:28])
    return dict(cpu=cpu, stalls=stalls, late=late, voices=voices, out=out_peak, lost=lost,
                peaks=peaks, scope=scope_n)


class Link:
    def __init__(self, port):
        import serial  # only with a board
        self.ser = serial.Serial(port, 115200, timeout=0.05)
        self.seq = 0
        self.frames = queue.Queue()
        self.errors = 0
        self.alive = True
        self.tx_lock = threading.Lock()
        threading.Thread(target=self._rx, daemon=True).start()
        threading.Thread(target=self._clock, daemon=True).start()

    def send(self, name, payload=b""):
        with self.tx_lock:
            seq = self.seq & 0xFF
            self.ser.write(encode_frame(MSG[name], seq, payload))
            self.seq += 1
        return seq

    def request(self, name, payload, timeout=2.0):
        """Fs* request -> (err, body): the reply has the same type and seq."""
        seq = self.send(name, payload)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                t, s, p = self.frames.get(timeout=end - time.monotonic())
            except queue.Empty:
                break
            if t == MSG[name] and s == seq:
                return struct.unpack("<h", p[:2])[0], p[2:]
        return -10, b""

    def _rx(self):
        buf = bytearray()
        while self.alive:
            try:
                data = self.ser.read(self.ser.in_waiting or 1)  # whatever is there: no wait for a full 4 KB
            except Exception:
                if self.alive:
                    raise
                return  # closed under us (update: the board reboots)
            for b in data:
                if b:
                    buf.append(b)
                    continue
                if buf:
                    f = decode_frame(bytes(buf))
                    if f is None:
                        self.errors += 1
                    elif f[0] == MSG["Log"]:
                        n = f[2][0]
                        print("[synth]", f[2][1:1 + n].decode(errors="replace"), flush=True)
                    else:
                        self.frames.put(f)
                buf.clear()

    def _clock(self):
        while self.alive:
            self.send("Time", struct.pack("<Q", now_us()))
            time.sleep(0.05)

    def wait(self, name, timeout=1.0):
        """Next frame of that type (others dropped) or None."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                t, _, p = self.frames.get(timeout=end - time.monotonic())
            except queue.Empty:
                return None
            if t == MSG[name]:
                return p
        return None

    def hello(self):
        for _ in range(3):
            self.send("Hello", hello_payload())
            p = self.wait("Hello", 1.0)
            if p:
                return parse_hello(p)
        return None


def find_port():
    """The one Teensy (VID 16C0) that is not a Dirtywave M8 (also a Teensy 4.1)."""
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports()
             if p.vid == 0x16C0 and "M8" not in (p.description or "") + (p.product or "")]
    if len(ports) != 1:
        found = ", ".join("%s (%s)" % (p.device, p.description) for p in ports) or "none"
        sys.exit("pass --port: Teensy ports found: " + found)
    return ports[0].device


def cmd_hello(link, args):
    h = link.hello()
    if not h:
        sys.exit("no Hello from the board")
    proto, fw, boot, size = h
    ok = "ok" if proto == PROTOCOL else "MISMATCH (script %d)" % PROTOCOL
    print("protocol %d %s, fw %s, boot %08x, SynthModel %d B" % (proto, ok, fw, boot, size))
    return h


def cmd_notes(link, args):
    cmd_hello(link, args)
    time.sleep(0.3)  # a few Time samples for the offset
    notes = [60, 64, 67, 72, 67, 64] * 2 + [60]
    for n in notes:
        t = now_us()
        link.send("Ev", ev_batch([(t, 0, [0x90, n, 100]), (t + 160000, 0, [0x80, n, 0])]))
        time.sleep(0.2)
    time.sleep(0.5)
    s = link.wait("Status", 0.5)
    if s:
        print_status(parse_status(s))


def print_status(s):
    peaks = " ".join("%3d" % v for v in s["peaks"])
    print("cpu %3d%%  voices %2d  out %5d  late %d  stalls %d  lost %d  | %s" %
          (s["cpu"], s["voices"], s["out"], s["late"], s["stalls"], s["lost"], peaks), flush=True)


def cmd_status(link, args):
    cmd_hello(link, args)
    end = time.monotonic() + args.secs
    last = 0
    while time.monotonic() < end:
        p = link.wait("Status", 1.0)
        if p is None:
            print("no Status for 1 s")
            continue
        if time.monotonic() - last >= 0.5:  # 25 Hz is too much to read
            last = time.monotonic()
            print_status(parse_status(p))
    print("frames with bad CRC / COBS: %d" % link.errors)


def fs_check(err, what):
    if err:
        sys.exit("%s: %s" % (what, FS_ERR.get(err, err)))


def cmd_fs(link, args):
    cmd_hello(link, args)
    names, start = [], 0
    while True:
        err, body = link.request("FsList", struct.pack("<HB", start, 32) + fs_str(args.dir))
        fs_check(err, "list " + args.dir)
        more, entries = parse_list(body)
        names += entries
        start += len(entries)
        if not more or not entries:
            break
    for is_dir, size, name in names:
        print("  %-40s %s" % (name + ("/" if is_dir else ""), "" if is_dir else size))
    print("%d entries" % len(names))
    path = "/desk_test.bin"
    data = bytes((i * 37) & 0xFF for i in range(10000))
    t0 = time.monotonic()
    err, body = link.request("FsOpen", bytes([1]) + fs_str(path))
    fs_check(err, "open for writing")
    h = body[0]
    for at in range(0, len(data), FS_CHUNK):
        err, body = link.request("FsWrite", struct.pack("<BI", h, at) + data[at:at + FS_CHUNK])
        fs_check(err, "write")
    fs_check(link.request("FsClose", bytes([h]))[0], "close")
    t1 = time.monotonic()
    err, body = link.request("FsOpen", bytes([0]) + fs_str(path))
    fs_check(err, "open for reading")
    h, size = body[0], struct.unpack("<I", body[2:6])[0]
    back = b""
    while len(back) < size:
        err, body = link.request("FsRead", struct.pack("<BIH", h, len(back), FS_CHUNK))
        fs_check(err, "read")
        if not body:
            break
        back += body
    fs_check(link.request("FsClose", bytes([h]))[0], "close")
    t2 = time.monotonic()
    fs_check(link.request("FsRemove", fs_str(path))[0], "remove")
    print("%d B written in %.0f ms, read in %.0f ms, %s" %
          (len(data), (t1 - t0) * 1000, (t2 - t1) * 1000, "same" if back == data else "DIFFERENT"))


FW_RESULT = ["Ok", "NoSd", "OpenFail", "ReadFail", "BadHex", "TooBig", "BadImage", "FlashFail", "Busy"]
FW_PATH = "/firmware/teensy.hex"


def cmd_update(link, args):
    """A .hex onto the board's card (/firmware/teensy.hex), then FwFromFile: staged, checked, the board
    flashes itself and reboots. Prints what the new firmware says on USB Serial for a few seconds."""
    cmd_hello(link, args)
    data = open(args.hex, "rb").read()
    err, body = link.request("FsStat", fs_str(FW_PATH))
    if err == 0 and not args.force:
        sys.exit("%s is on the card already (%d B): --force to overwrite" % (FW_PATH, struct.unpack("<I", body[1:5])[0]))
    t0 = time.monotonic()
    err, body = link.request("FsOpen", bytes([1]) + fs_str(FW_PATH))
    fs_check(err, "open " + FW_PATH)
    h = body[0]
    for at in range(0, len(data), FS_CHUNK):
        fs_check(link.request("FsWrite", struct.pack("<BI", h, at) + data[at:at + FS_CHUNK])[0], "write")
    fs_check(link.request("FsClose", bytes([h]))[0], "close")
    print("%s: %d B in %.1f s" % (FW_PATH, len(data), time.monotonic() - t0), flush=True)
    t0 = time.monotonic()
    seq = link.send("FwFromFile", fs_str(FW_PATH))
    last, shown = time.monotonic(), 0
    while True:
        try:
            t, s, p = link.frames.get(timeout=max(0.1, last + 5 - time.monotonic()))
        except queue.Empty:
            sys.exit("no reply / Progress for 5 s")
        if t == MSG["Progress"]:
            last = time.monotonic()
            op, done, total = struct.unpack("<BII", p[:9])
            if time.monotonic() - shown > 1:
                shown = time.monotonic()
                print("  %5.1f s  %d / %d" % (last - t0, done, total), flush=True)
        elif t == MSG["FwFromFile"] and s == seq:
            res, size = struct.unpack("<BI", p[:5])
            print("FwFromFile: %s, image %d B, %.1f s" % (FW_RESULT[res] if res < len(FW_RESULT) else res, size,
                                                          time.monotonic() - t0), flush=True)
            if res:
                sys.exit(1)
            break
    link.alive = False
    link.ser.close()
    from serial.tools import list_ports
    end, ser, out = time.monotonic() + args.secs, None, b""
    while time.monotonic() < end:
        if ser is None:
            ports = [q.device for q in list_ports.comports() if q.vid == 0x16C0 and "M8" not in (q.description or "")]
            try:
                ser = __import__("serial").Serial(ports[0], 115200, timeout=0.2) if ports else None
                if ser:
                    print("board back after %.1f s on %s" % (time.monotonic() - t0, ports[0]), flush=True)
            except Exception:
                ser = None
            time.sleep(0.2)
            continue
        try:
            out += ser.read(ser.in_waiting or 1)
        except Exception:
            ser = None
    text = bytes(b for b in out if 32 <= b < 127 or b == 10).decode()
    print(text if text.strip() else "(no text from the board)")


BANK_RESULT = ["Ok", "Busy", "NoSd", "NoBank", "OpenFail", "ReadFail", "NotWav", "Unsupported", "Truncated",
               "Full", "WriteFail", "NoMemory", "Link", "Running"]


def parse_bank_index(p):
    """BankIndex reply -> dict (result, count, capacity, free, unused, gen, builtins, rates)."""
    res, count, cap, free, unused, gen = struct.unpack("<BBIIII", p[:18])
    builtins, n = p[18 + 16 + 4], p[18 + 16 + 4 + 1]
    rates = list(struct.unpack("<%dH" % n, p[18 + 16 + 4 + 2:18 + 16 + 4 + 2 + 2 * n]))
    return {"result": BANK_RESULT[res], "count": count, "capacity": cap, "free": free, "unused": unused,
            "gen": gen, "builtins": builtins, "rates": rates}


def cmd_bank(link, args):
    """The bank's state; with a path, a WAV on the board's card imported into it first."""
    cmd_hello(link, args)
    if args.path:
        seq = link.send("BankImport", bytes([1 if args.wt else 0, 0]) + struct.pack("<I", 0) + fs_str(args.path))
        end = time.monotonic() + 60
        while time.monotonic() < end:
            try:
                t, s, p = link.frames.get(timeout=end - time.monotonic())
            except queue.Empty:
                break
            if s != seq:
                continue
            if t == MSG["Progress"]:
                _, done, total, _ = struct.unpack("<BIIB", p[:10])
                print("  %d / %d" % (done, total), flush=True)
            elif t == MSG["BankImport"]:
                res, crc, frames, rate, root = struct.unpack("<BIIIB", p[:14])
                print("import: %s crc %08x, %d frames at %d Hz, root %d" % (BANK_RESULT[res], crc, frames, rate, root))
                break
        else:
            print("import: no reply")
    link.send("BankIndex")
    p = link.wait("BankIndex", 2.0)
    if not p:
        sys.exit("no BankIndex reply")
    b = parse_bank_index(p)
    print("bank %s: %d entries, %d KB free of %d KB, %d KB unused, gen %d, built-ins %02x" %
          (b["result"], b["count"], b["free"] // 1024, b["capacity"] // 1024, b["unused"] // 1024, b["gen"],
           b["builtins"]))


def cmd_selftest(args):
    assert crc16(b"123456789") == 0x29B1
    for n in (0, 1, 253, 254, 255, 256, 512):
        for p in (bytes(n), b"\xff" * n, bytes((i * 7) % 256 for i in range(n))):
            enc = encode_frame(5, 9, p)
            assert enc[-1] == 0 and 0 not in enc[:-1]
            assert decode_frame(enc[:-1]) == (5, 9, p), n
    batch = ev_batch([(1000 + i, i % 17, [0x90, 60, 127]) for i in range(40)])
    assert len(batch) <= MAX_PAYLOAD
    assert parse_hello(hello_payload())[0] == PROTOCOL
    assert MSG["FsOpen"] == 7 and MSG["Log"] == 37 and MSG["FsRmdir"] == 38 and MSG["WtFrame"] == 39
    assert MSG["BankSync"] == 16 and MSG["BankIndex"] == 20 and MSG["Progress"] == 35
    idx = struct.pack("<BBIIII", 0, 3, 5000, 4000, 100, 7) + bytes(20) + bytes([0xFF, 2]) + struct.pack("<HH", 44100, 0)
    b = parse_bank_index(idx)
    assert b["count"] == 3 and b["free"] == 4000 and b["builtins"] == 0xFF and b["rates"] == [44100, 0]
    page = bytes([1, 2]) + struct.pack("<BI", 1, 0) + fs_str("wt") + struct.pack("<BI", 0, 1234) + fs_str("kick.wav")
    assert parse_list(page) == (True, [(True, 0, "wt"), (False, 1234, "kick.wav")])
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("hello")
    sub.add_parser("notes")
    st = sub.add_parser("status")
    st.add_argument("secs", type=float, nargs="?", default=5.0)
    fs = sub.add_parser("fs")
    fs.add_argument("dir", nargs="?", default="/")
    bk = sub.add_parser("bank")
    bk.add_argument("path", nargs="?", help="a WAV on the board's card to import first")
    bk.add_argument("--wt", action="store_true", help="import it as a wavetable")
    up = sub.add_parser("update")
    up.add_argument("hex")
    up.add_argument("--force", action="store_true")
    up.add_argument("--secs", type=float, default=15.0)
    sub.add_parser("selftest")
    args = ap.parse_args()
    if args.cmd == "selftest":
        return cmd_selftest(args)
    link = Link(args.port or find_port())
    {"hello": cmd_hello, "notes": cmd_notes, "status": cmd_status, "fs": cmd_fs, "bank": cmd_bank, "update": cmd_update}[args.cmd](link, args)
    link.alive = False


if __name__ == "__main__":
    main()
