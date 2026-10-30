#!/usr/bin/env python3
"""Compile a directory of MIDI phrases into the bounded DML1 library used by Dice.

No third-party packages. Source MIDI files are read only. The JSON manifest records
every conversion, rejected file, drum-map approximation and duplicate-path alias.
Run with --output /tmp/dice-library.dml to inspect the result before copying it to SD.
DML1 stores the original polyphonic note events; the firmware performs CHD fitting.
"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
from dataclasses import dataclass, field
import hashlib
import json
import math
from pathlib import Path
import re
import struct

HEADER = struct.Struct("<4sHHII")
ENTRY = struct.Struct("<HBBBBHII48s")
EVENT = struct.Struct("<HHBBBB")
PPQ = 96
BAR = PPQ * 4
MAX_BARS = 8
MAX_EVENTS = 512
GENRES = ("HOUSE", "TECHNO", "TRANCE", "DNB", "BREAKS", "HIPHOP",
          "IDM", "AMBIENT", "SYNTHWAVE")
ROLES = ("DRUMS", "BASS", "CHORDS", "LEAD", "ARP", "FX")
# Firmware ScaleType values, deliberately independent of Python enum ordering.
MAJOR, MINOR = 1, 2


@dataclass(frozen=True)
class Note:
    tick: int
    duration: int
    note: int
    velocity: int
    channel: int = 0
    track: int = 0


@dataclass
class Midi:
    ppq: int
    notes: list[Note] = field(default_factory=list)
    bpm: int = 0
    track_names: dict[int, str] = field(default_factory=dict)
    keys: list[tuple[int, int]] = field(default_factory=list)
    signatures: list[tuple[int, int]] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


def _vlq(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    for _ in range(4):
        if pos >= len(data):
            raise ValueError("truncated VLQ")
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 127)
        if byte < 128:
            return value, pos
    raise ValueError("VLQ longer than four bytes")


def _take(data: bytes, pos: int, length: int) -> tuple[bytes, int]:
    end = pos + length
    if end > len(data):
        raise ValueError("truncated MIDI event")
    return data[pos:end], end


def _metadata(result: Midi, track: int, kind: int, payload: bytes) -> None:
    if kind == 3 and track not in result.track_names:
        result.track_names[track] = payload.decode("utf8", "replace").strip("\0")
    elif kind == 81 and len(payload) == 3:
        micros = int.from_bytes(payload, "big")
        if micros == 0:
            raise ValueError("zero tempo")
        if result.bpm == 0:
            result.bpm = round(60_000_000 / micros)
    elif kind == 89 and len(payload) == 2:
        sf = int.from_bytes(payload[:1], "big", signed=True)
        if -7 <= sf <= 7 and payload[1] < 2:
            result.keys.append(((7 * sf + (9 if payload[1] else 0)) % 12,
                                MINOR if payload[1] else MAJOR))
    elif kind == 88 and len(payload) >= 2:
        if payload[0] == 0 or payload[1] > 7:
            raise ValueError("invalid time signature")
        result.signatures.append((payload[0], 1 << payload[1]))


def _channel_event(result: Midi, held: dict, status: int, values: bytes,
                   tick: int, track: int) -> None:
    if any(value > 127 for value in values):
        raise ValueError("invalid MIDI data byte")
    kind, channel = status & 240, status & 15
    if kind not in (128, 144):
        return
    note, velocity = values
    queue = held[(channel, note)]
    if kind == 144 and velocity:
        queue.append((tick, velocity))
    elif queue:
        start, attack = queue.popleft()
        result.notes.append(Note(start, tick - start, note, attack, channel, track))


def _track(data: bytes, result: Midi, track: int) -> None:
    pos = tick = running = 0
    held = defaultdict(deque)
    while pos < len(data):
        delta, pos = _vlq(data, pos)
        tick += delta
        if tick > 0xFFFFFFFF or pos >= len(data):
            raise ValueError("invalid tick or truncated status")
        status = data[pos]
        if status < 128:
            if not running:
                raise ValueError("missing running status")
            status = running
        else:
            pos += 1
            if status < 240:
                running = status
        if status == 255:
            kind_data, pos = _take(data, pos, 1)
            length, pos = _vlq(data, pos)
            payload, pos = _take(data, pos, length)
            _metadata(result, track, kind_data[0], payload)
            if kind_data[0] == 47:
                break
        elif status in (240, 247):
            length, pos = _vlq(data, pos)
            _, pos = _take(data, pos, length)
        elif 248 <= status <= 254:
            continue
        elif 128 <= status < 240:
            count = 1 if (status & 240) in (192, 208) else 2
            values, pos = _take(data, pos, count)
            _channel_event(result, held, status, values, tick, track)
        else:
            raise ValueError("unsupported system message")
    for (channel, note), queue in held.items():
        for start, velocity in queue:
            result.notes.append(Note(start, tick - start, note, velocity, channel, track))
            result.warnings.append("open note closed at end of track")


def parse_smf(data: bytes) -> Midi:
    """Strict SMF 0/1 reader; malformed files are reported rather than partly used."""
    if len(data) < 14 or data[:4] != b"MThd":
        raise ValueError("not a Standard MIDI File")
    length, fmt, tracks, ppq = struct.unpack_from(">IHHH", data, 4)
    if length < 6 or fmt not in (0, 1) or not tracks or (fmt == 0 and tracks != 1):
        raise ValueError("unsupported MIDI header")
    if not ppq or ppq & 0x8000:
        raise ValueError("SMPTE or zero division not supported")
    result = Midi(ppq)
    pos = 8 + length
    for track in range(tracks):
        header, pos = _take(data, pos, 8)
        if header[:4] != b"MTrk":
            raise ValueError("missing track chunk")
        size = int.from_bytes(header[4:], "big")
        payload, pos = _take(data, pos, size)
        _track(payload, result, track)
    result.notes.sort(key=lambda n: (n.tick, n.note, n.track, n.channel))
    return result


def genre_mask(folder: str) -> int:
    name = folder.casefold()
    families = {
        "analog magic": (0, 1), "chillwave": (8, 7), "darkwave": (8,),
        "deep house": (0,), "disco": (0, 8), "drill": (5,), "drum & bass": (3,),
        "edm": (0, 2), "electric dreams": (8,), "electronica": (6, 7),
        "house": (0,), "indie dance": (0, 8), "latin": (0, 4), "lo-fi hip hop": (5,),
        "melodic techno": (1,), "minimal": (1,), "pop": (8, 0),
        "reggaeton": (5, 4), "slap house": (0,), "soul & r&b": (5,),
        "tech house": (0, 1), "trap": (5,), "ethereal2080 dre4mscape": (7, 8),
    }
    indices = families.get(name)
    if indices is None:
        indices = tuple(i for i, genre in enumerate(GENRES) if genre.casefold() == name)
    return sum(1 << i for i in indices) if indices else (1 << len(GENRES)) - 1


def role_of(path: Path) -> int:
    category = path.parts[1].casefold() if len(path.parts) > 2 else ""
    if "drum" in category:
        return 0
    if "bass" in category:
        return 1
    if "chord" in category or "pad" in category or "key" in category:
        return 2
    if "arp" in category or "pluck" in category:
        return 4
    if re.search(r"(?:^|[ _-])(fx|riser|impact|noise|vinyl)(?:[ _-]|$)", path.stem, re.I):
        return 5
    return 3


def _pitch_key(notes: list[Note]) -> tuple[int, int]:
    profile = [0.0] * 12
    for note in notes:
        profile[note.note % 12] += max(1, note.duration) * note.velocity
    best = (-math.inf, 0, MINOR)
    # Correlation profiles provide a fallback, not a claim of a known source key.
    major = (6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88)
    minor = (6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17)
    for scale, weights in ((MAJOR, major), (MINOR, minor)):
        for root in range(12):
            score = sum(profile[i] * weights[(i - root) % 12] for i in range(12))
            if score > best[0]:
                best = (score, root, scale)
    return best[1:]


def infer_key(filename: str, parsed: Midi) -> tuple[int, int, str]:
    # A token boundary prevents matching parts of words such as Bass, Lead or BPM.
    key = re.search(r"(?:^|[\s_-])([A-Ga-g])([#b]?)(min(?:or)?|m|maj(?:or)?)?(?=$|[\s_.-])",
                    Path(filename).stem)
    if key:
        root = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[key[1].upper()]
        root = (root + (1 if key[2] == "#" else -1 if key[2] == "b" else 0)) % 12
        return root, MINOR if key[3] and key[3] in ("m", "min", "minor") else MAJOR, "filename"
    if parsed.keys:
        return *parsed.keys[0], "MIDI metadata"
    root, scale = _pitch_key(parsed.notes)
    return root, scale, "pitch profile (low confidence)"


def _named_lane(name: str) -> int | None:
    name = re.sub(r"[^a-z0-9]+", " ", name.casefold())
    if re.search(r"kick|bass drum|\bbd\b", name):
        return 0
    if re.search(r"snare|\bsd\b|rimshot", name):
        return 1
    if "clap" in name:
        return 4
    if re.search(r"open.?hat|\boh\b", name):
        return 3
    if re.search(r"closed?.?hat|\bch\b|\bcd hat\b|hi.?hat|hihat", name):
        return 2
    if re.search(r"ride|crash|cymbal", name):
        return 7
    if "tom" in name:
        return 6
    if re.search(r"perc|shaker|conga|bongo|clave|cowbell", name):
        return 5
    return None


def drum_lane(note: int, track_name: str, filename: str) -> tuple[int, str]:
    named = _named_lane(track_name)
    if named is not None:
        return named, "track name"
    gm = {35: 0, 36: 0, 38: 1, 40: 1, 37: 5, 39: 4, 42: 2, 44: 2, 46: 3,
          41: 6, 43: 6, 45: 6, 47: 6, 48: 6, 50: 6,
          49: 7, 51: 7, 52: 7, 53: 7, 55: 7, 57: 7, 59: 7}
    if note in gm:
        return gm[note], "GM pitch"
    named = _named_lane(filename)
    if named is not None:
        return named, "filename"
    return 5, "unknown drum pitch mapped to percussion"


def _scaled(value: int, ppq: int) -> int:
    return (value * PPQ + ppq // 2) // ppq


def _source_notes(parsed: Midi, role: int, filename: str) -> tuple[list, int, list[str]]:
    first = min(note.tick for note in parsed.notes)
    leading = first // (parsed.ppq * 4)
    origin = leading * parsed.ppq * 4
    warnings = set(parsed.warnings)
    notes = []
    for note in parsed.notes:
        tick = _scaled(note.tick - origin, parsed.ppq)
        duration = max(1, _scaled(note.duration, parsed.ppq))
        if duration > BAR * MAX_BARS:
            duration = BAR * MAX_BARS
            warnings.add("note duration clipped to eight bars")
        lane = 255
        if role == 0:
            lane, reason = drum_lane(note.note, parsed.track_names.get(note.track, ""), filename)
            if reason == "unknown drum pitch mapped to percussion":
                warnings.add(reason)
        notes.append((tick, duration, note.note, note.velocity, lane, 0))
    return sorted(notes), leading, sorted(warnings)


def _window_events(notes: list, start: int, end: int) -> list:
    events = []
    for tick, duration, note, velocity, lane, reserved in notes:
        if tick < end and tick + duration > start:
            position = max(tick, start)
            events.append((position - start, min(tick + duration, end) - position,
                           note, velocity, lane, reserved))
    return sorted(events)


def _windows(notes: list, start: int, bars: int) -> list[tuple[int, int, list]]:
    events = _window_events(notes, start, start + BAR * bars)
    if not events:
        return []
    if len(events) <= MAX_EVENTS:
        return [(start, bars, events)]
    if bars == 1:
        raise ValueError("more than 512 events within one bar")
    half = bars // 2
    return _windows(notes, start, half) + _windows(notes, start + BAR * half, half)


def _trim_phrase_tail(notes: list) -> tuple[list, bool]:
    """Do not turn a tiny note-off overhang into extra silent bars.

    Only durations are shortened. Every attack must precede the chosen boundary;
    an intentional pickup/next-bar attack keeps the longer phrase.
    """
    end = max(tick + duration for tick, duration, *_ in notes)
    latest_attack = max(note[0] for note in notes)
    block_start = (end // (BAR * MAX_BARS)) * BAR * MAX_BARS
    for bars in (0, 1, 2, 4, 8):
        boundary = block_start + bars * BAR
        if boundary > 0 and latest_attack < boundary < end <= boundary + PPQ:
            clipped = [(tick, min(duration, boundary - tick), *rest)
                       for tick, duration, *rest in notes]
            return clipped, True
    return notes, False


def _phrases(notes: list) -> list[tuple[int, int, list]]:
    end = max(tick + duration for tick, duration, *_ in notes)
    phrases = []
    for start in range(0, end, BAR * MAX_BARS):
        needed = min(MAX_BARS, (end - start + BAR - 1) // BAR)
        bars = next(size for size in (1, 2, 4, 8) if size >= needed)
        phrases.extend(_windows(notes, start, bars))
    return phrases


def _aliases(root: Path) -> tuple[dict[str, list[Path]], list[str]]:
    result = defaultdict(list)
    path = root / "dice-duplicates.json"
    if not path.exists():
        return result, []
    try:
        manifest = json.loads(path.read_text())
        records = manifest["duplicates"]
        origin = Path(manifest.get("root", str(root))).resolve()
        for item in records:
            # Manifest created by the explicit deduplication operation may carry absolute paths.
            keep = Path(item["keep"]).resolve().relative_to(origin) if Path(item["keep"]).is_absolute() else Path(item["keep"])
            alias = Path(item["remove"]).resolve().relative_to(origin) if Path(item["remove"]).is_absolute() else Path(item["remove"])
            if ".." in keep.parts or ".." in alias.parts:
                raise ValueError("parent path in duplicate manifest")
            result[keep.as_posix()].append(alias)
    except (ValueError, KeyError, TypeError) as error:
        raise ValueError(f"invalid duplicate manifest: {error}") from error
    return result, []


def _bpm(filename: str, parsed: Midi) -> int:
    if parsed.bpm:
        return min(65535, parsed.bpm)
    matches = re.finditer(r"(?:^|[ _-])(\d{2,3})(?:\s*bpm|(?=[ _-]))", filename, re.I)
    return next((int(match[1]) for match in matches if 20 <= int(match[1]) <= 300), 0)


def _entry_rows(source: Path, aliases: list[Path], parsed: Midi) -> list[dict]:
    entries = []
    for label in [source, *aliases]:
        role = role_of(label)
        root_note, scale, confidence = infer_key(label.name, parsed)
        notes, leading, warnings = _source_notes(parsed, role, label.name)
        notes, clipped_tail = _trim_phrase_tail(notes)
        if clipped_tail:
            warnings.append("note tails clipped to standard phrase boundary")
        if parsed.keys and confidence == "filename" and parsed.keys[0] != (root_note, scale):
            warnings.append("explicit filename key overrides conflicting MIDI metadata")
        phrases = _phrases(notes)
        if parsed.signatures and any(signature != (4, 4) for signature in parsed.signatures):
            warnings.append("source meter retained in 4/4 phrase windows")
        family = genre_mask(label.parts[0]) if len(label.parts) > 1 else 511
        for start, bars, events in phrases:
            continuations = sum(tick < start < tick + duration for tick, duration, *_ in notes)
            entry_warnings = warnings + (["sustained notes reattacked at phrase boundary"] if continuations else [])
            display = f"{label.stem} [{start // BAR + 1}]"
            entries.append(dict(source=source.as_posix(), alias=label.as_posix() if label != source else None,
                                genre_mask=family, role=role, root=root_note, scale=scale,
                                key_source=confidence, bars=bars, bpm=_bpm(label.stem, parsed),
                                trimmed_leading_bars=leading, start_bar=start // BAR,
                                source_notes=len(parsed.notes), event_count=len(events),
                                warnings=entry_warnings, name=display, events=events))
    return entries


def _merge_identical(entries: list[dict]) -> tuple[list[dict], int]:
    unique = {}
    for entry in entries:
        key = (entry["genre_mask"], entry["role"], entry["root"], entry["scale"],
               entry["bars"], entry["bpm"], tuple(entry["events"]))
        label = entry["alias"] or entry["source"]
        if key in unique:
            previous = unique[key]
            if label not in previous["aliases"]:
                previous["aliases"].append(label)
        else:
            entry["aliases"] = [label] if entry["alias"] else []
            unique[key] = entry
    return list(unique.values()), len(entries) - len(unique)


def _encode(entries: list[dict]) -> bytes:
    offset = HEADER.size + ENTRY.size * len(entries)
    payloads = bytearray()
    records = bytearray()
    shared = {}
    for entry in entries:
        payload = b"".join(EVENT.pack(*event) for event in entry["events"])
        if payload not in shared:
            shared[payload] = offset + len(payloads)
            payloads.extend(payload)
        suffix = hashlib.sha256((entry["source"] + str(entry["start_bar"])).encode()).hexdigest()[:6]
        prefix = entry["name"].encode("utf8")[:39].decode("utf8", "ignore")
        name = (prefix + " #" + suffix).encode("utf8").ljust(48, b"\0")
        records.extend(ENTRY.pack(entry["genre_mask"], entry["role"], entry["root"],
                                  entry["scale"], entry["bars"], entry["bpm"],
                                  shared[payload], len(payload), name))
    return HEADER.pack(b"DML1", 1, ENTRY.size, len(entries), 0) + records + payloads


def build_library(root: Path) -> tuple[bytes, dict]:
    root = root.resolve()
    aliases, warnings = _aliases(root)
    files = sorted(path for path in root.rglob("*") if path.is_file()
                   and path.suffix.lower() in (".mid", ".midi") and not path.name.startswith("._"))
    entries, rejected = [], []
    source_hashes = {}
    for path in files:
        relative = path.relative_to(root)
        raw = path.read_bytes()
        try:
            parsed = parse_smf(raw)
            if not parsed.notes:
                raise ValueError("no note events")
            new = _entry_rows(relative, aliases.get(relative.as_posix(), []), parsed)
            entries.extend(new)
            source_hashes[relative.as_posix()] = hashlib.sha256(raw).hexdigest()
        except (ValueError, OverflowError, struct.error) as error:
            rejected.append(dict(source=relative.as_posix(), reason=str(error)))
    entries, merged = _merge_identical(entries)
    binary = _encode(entries)
    manifest_entries = [{key: value for key, value in entry.items() if key != "events"} for entry in entries]
    manifest = dict(format="DML1", version=1, ppq=PPQ, max_events=MAX_EVENTS,
                    source_files=len(files), accepted_files=len(source_hashes),
                    duplicate_aliases=sum(len(value) for value in aliases.values()),
                    merged_identical_phrases=merged,
                    entry_count=len(entries), bytes=len(binary), sha256=hashlib.sha256(binary).hexdigest(),
                    source_hashes=source_hashes, warnings=warnings, entries=manifest_entries, rejected=rejected)
    return binary, manifest


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="MIDI library root, containing genre/role folders")
    parser.add_argument("--output", type=Path, help="DML1 path (default ROOT/dice-library.dml)")
    parser.add_argument("--manifest", type=Path, help="JSON report path (default next to output)")
    parser.add_argument("--dry-run", action="store_true", help="analyse without writing output files")
    args = parser.parse_args(argv)
    if not args.root.is_dir():
        parser.error("root is not a directory")
    output = args.output or args.root / "dice-library.dml"
    report = args.manifest or output.with_suffix(".json")
    if output.suffix.lower() in (".mid", ".midi") or report.suffix.lower() in (".mid", ".midi"):
        parser.error("output paths must not overwrite MIDI files")
    if output.resolve() == report.resolve():
        parser.error("library and manifest must use different paths")
    binary, manifest = build_library(args.root)
    if not args.dry_run:
        output.parent.mkdir(parents=True, exist_ok=True)
        report.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(binary)
        report.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(f"{manifest['accepted_files']}/{manifest['source_files']} MIDI files; "
          f"{manifest['entry_count']} phrases; {len(manifest['rejected'])} rejected; {len(binary)} bytes")
    if not args.dry_run:
        print(f"Library: {output}\nManifest: {report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
