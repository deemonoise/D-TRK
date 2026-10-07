#!/usr/bin/env python3
"""Recovers D-TRK projects and their samples from a raw image of the SD card.

When the card's directories are damaged, the files' contents usually remain on the card. This
script finds them by their headers and checks each against its own checksum:

  * projects (.mtp): the "MTRK" header, chunks up to the "CRC " chunk, whose crc32 must match:
    a match means the file was found whole;
  * WAV files: the RIFF / WAVE header. A WAV whose audio data has the crc32 and length that a
    recovered project lists for one of its samples gets that sample's name and goes into the
    project's folder, as the firmware keeps them: projects/<PROJECT>/<SAMPLE>.wav.

Read-only on the image. Usage:

  1. Make an image of the card and work on it (do not write to the card):
       macOS:  diskutil list                      # find the card, e.g. /dev/disk4
               diskutil unmountDisk /dev/disk4
               sudo dd if=/dev/rdisk4 of=sd.img bs=4m
       Linux:  sudo dd if=/dev/sdX of=sd.img bs=4M status=progress
  2. python3 sd_recover.py sd.img recovered [GB] [--projects]
     --projects: projects only, no WAV files (faster).
     GB: scan only the first GB gigabytes. FAT fills a card from the start, so on a card that
     was far from full the files are near the beginning: e.g. 8 for a card with a few GB used.
     The whole card is read once; Ctrl+C stops the scan early and keeps what was found.
  3. Copy recovered/projects/* to /projects on a working card.

Several versions of a project may be found (older saves, .bak, .auto): they are written as
NAME.mtp, NAME~2.mtp, ... in the order found; the report lists their sizes and sample counts.
Files that were stored in pieces (fragmented) fail the checksum and are reported, not written.
"""
import os
import struct
import sys
import time
import zlib

SECTOR = 512
WINDOW = 64 << 20  # bytes scanned per read
MAX_MTP = 4 << 20
MAX_WAV = 64 << 20


def device_size(f):
    """Size of a disk device in bytes (a raw device reports 0 to seek), 0 if unknown."""
    try:
        import fcntl
    except ImportError:
        return 0
    try:  # macOS: DKIOCGETBLOCKSIZE, DKIOCGETBLOCKCOUNT
        bs = struct.unpack("<I", fcntl.ioctl(f.fileno(), 0x40046418, b"\0" * 4))[0]
        n = struct.unpack("<Q", fcntl.ioctl(f.fileno(), 0x40086419, b"\0" * 8))[0]
        return bs * n
    except OSError:
        pass
    try:  # Linux: BLKGETSIZE64
        return struct.unpack("<Q", fcntl.ioctl(f.fileno(), 0x80081272, b"\0" * 8))[0]
    except OSError:
        return 0


class Image:
    """Sector-aligned reads (raw devices need them), from a file or a device."""

    def __init__(self, path):
        self.f = open(path, "rb", buffering=0)
        self.f.seek(0, os.SEEK_END)
        self.size = self.f.tell() or device_size(self.f)
        if not self.size:
            sys.exit("Cannot tell the size of %s" % path)

    def read(self, off, n):
        if off >= self.size:
            return b""
        start = off - off % SECTOR
        end = min(self.size, -(-(off + n) // SECTOR) * SECTOR)
        self.f.seek(start)
        data = self.f.read(end - start)
        return data[off - start:off - start + n]


def scan(img, limit, wav=True):
    """Sector-aligned offsets of "MTRK" and "RIFF" in the first limit bytes, in one pass.
    Ctrl+C ends the scan early; what was found so far is still recovered."""
    hits = {b"MTRK": [], b"RIFF": []} if wav else {b"MTRK": []}
    pos, t0 = 0, time.time()
    try:
        while pos < limit:
            buf = img.read(pos, WINDOW + SECTOR)
            if not buf:
                break
            for magic, out in hits.items():
                i = buf.find(magic)
                while 0 <= i < WINDOW:
                    if (pos + i) % SECTOR == 0:
                        out.append(pos + i)
                    i = buf.find(magic, i + 1)
            pos += WINDOW
            done = min(pos, limit)
            rate = done / max(time.time() - t0, 1e-3)
            sys.stderr.write("\r  %d / %d MB, %.0f MB/s, %d min left, found %d projects / %d wav headers   " % (
                done >> 20, limit >> 20, rate / 2**20, (limit - done) / rate / 60, len(hits[b"MTRK"]),
                len(hits.get(b"RIFF", []))))
    except KeyboardInterrupt:
        sys.stderr.write("\n  stopped at %d MB" % (pos >> 20))
    sys.stderr.write("\n")
    return hits[b"MTRK"], hits.get(b"RIFF", [])


def parse_mtp(img, off):
    """(bytes, name, samples) of a whole project at off, or None. samples: [(name, crc, frames)]."""
    head = img.read(off, 8)
    if head[:4] != b"MTRK" or struct.unpack_from("<H", head, 4)[0] == 0:
        return None
    pos, name, samples = 8, None, []
    data = bytearray(head)
    while pos < MAX_MTP:
        ch = img.read(off + pos, 8)
        if len(ch) < 8:
            return None
        cid, size = ch[:4], struct.unpack_from("<I", ch, 4)[0]
        if not all(0x20 <= c < 0x7F for c in cid) or size > MAX_MTP:
            return None
        if cid == b"CRC ":
            if size != 4:
                return None
            crc = struct.unpack("<I", img.read(off + pos + 8, 4))[0]
            if zlib.crc32(bytes(data)) & 0xFFFFFFFF != crc:
                return "badcrc"
            data += ch + struct.pack("<I", crc)
            return bytes(data), name, samples
        payload = img.read(off + pos + 8, size)
        if len(payload) < size:
            return None
        data += ch + payload
        if cid == b"PROJ":
            name = payload[:17].split(b"\0")[0].decode("ascii", "replace")
        elif cid == b"SMPL" and size >= 1:
            for k in range(payload[0]):
                rec = payload[1 + k * 24:1 + (k + 1) * 24]
                if len(rec) == 24:
                    samples.append((rec[:16].split(b"\0")[0].decode("ascii", "replace"),
                                    struct.unpack_from("<I", rec, 16)[0], struct.unpack_from("<I", rec, 20)[0]))
        pos += 8 + size
    return None


def parse_wav(img, off):
    """(bytes, data crc, frames) of a mono 16-bit WAV at off, or None."""
    head = img.read(off, 12)
    if head[:4] != b"RIFF" or head[8:12] != b"WAVE":
        return None
    total = struct.unpack_from("<I", head, 4)[0] + 8
    if total < 44 or total > MAX_WAV:
        return None
    raw = img.read(off, total)
    if len(raw) < total:
        return None
    pos, fmt, data = 12, None, None
    while pos + 8 <= total:
        cid, size = raw[pos:pos + 4], struct.unpack_from("<I", raw, pos + 4)[0]
        if cid == b"fmt ":
            fmt = raw[pos + 8:pos + 8 + size]
        elif cid == b"data":
            data = raw[pos + 8:pos + 8 + size]
            break
        pos += 8 + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        return None
    channels, bits = struct.unpack_from("<H", fmt, 2)[0], struct.unpack_from("<H", fmt, 14)[0]
    frames = len(data) // max(1, channels * bits // 8)
    return raw, zlib.crc32(data) & 0xFFFFFFFF, frames


def main():
    args = [a for a in sys.argv[1:] if a != "--projects"]
    wav = "--projects" not in sys.argv
    if len(args) not in (2, 3):
        sys.exit(__doc__)
    img, out = Image(args[0]), args[1]
    limit = min(img.size, int(float(args[2]) * 2**30)) if len(args) == 3 else img.size
    print("Image: %s, %.1f GB, scanning the first %.1f GB" % (args[0], img.size / 2**30, limit / 2**30))
    mtrk, riff = scan(img, limit, wav)

    print("Projects:")
    projects, seen, bad = [], set(), 0
    for off in mtrk:
        r = parse_mtp(img, off)
        if r == "badcrc":
            bad += 1
            continue
        if not r or r[0] in seen:
            continue
        seen.add(r[0])
        projects.append(r)
    counts = {}
    os.makedirs(os.path.join(out, "projects"), exist_ok=True)
    wanted = {}  # (crc, frames) -> [(project file stem, sample name)]
    for data, name, samples in projects:
        name = name or "RECOVERED"
        counts[name] = counts.get(name, 0) + 1
        stem = name if counts[name] == 1 else "%s~%d" % (name, counts[name])
        with open(os.path.join(out, "projects", stem + ".mtp"), "wb") as f:
            f.write(data)
        print("  %-20s %7d bytes, %d samples" % (stem + ".mtp", len(data), len(samples)))
        for s, crc, frames in samples:
            wanted.setdefault((crc, frames), []).append((stem, s))
    if bad:
        print("  %d more with a bad checksum (stored in pieces): not written" % bad)
    if not projects:
        print("  none found")

    if not wav:
        return
    print("Samples:")
    found, other = set(), 0
    for off in riff:
        r = parse_wav(img, off)
        if not r:
            continue
        raw, crc, frames = r
        owners = wanted.get((crc, frames))
        if owners:
            for stem, s in owners:
                d = os.path.join(out, "projects", stem)
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, s + ".wav"), "wb") as f:
                    f.write(raw)
                found.add((stem, s))
        else:
            d = os.path.join(out, "other_wav")
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, "wav_%012x.wav" % off), "wb") as f:
                f.write(raw)
            other += 1
    need = sum(len(v) for v in wanted.values())
    print("  %d of %d project samples found; %d other WAV files in other_wav/" % (len(found), need, other))
    missing = sorted({(stem, s) for v in wanted.values() for stem, s in v} - found)
    for stem, s in missing:
        print("  missing: %s / %s (the firmware may still have it cached in flash)" % (stem, s))


if __name__ == "__main__":
    main()
