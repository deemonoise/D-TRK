"""Host-only tests for the SD Dice library compiler (no third-party packages)."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import prepare_dice_library as lib


def varint(value):
    result = [value & 127]
    value >>= 7
    while value:
        result.insert(0, (value & 127) | 128)
        value >>= 7
    return bytes(result)


def midi(events, ppq=96, name=""):
    track = bytearray()
    if name:
        text = name.encode()
        track.extend(b"\0\xff\x03" + varint(len(text)) + text)
    for delta, message in events:
        track.extend(varint(delta) + message)
    track.extend(b"\0\xff\x2f\0")
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, ppq) + b"MTrk" + struct.pack(">I", len(track)) + track


class LibraryTests(unittest.TestCase):
    def test_note_lengths_running_status_and_velocity_zero(self):
        source = midi([(12, b"\x90\x3c\x60"), (24, b"\x40\x50"),
                       (24, b"\x3c\0"), (24, b"\x40\0")])
        parsed = lib.parse_smf(source)
        self.assertEqual([(n.tick, n.duration, n.note, n.velocity) for n in parsed.notes],
                         [(12, 48, 60, 96), (36, 48, 64, 80)])

    def test_overlapping_same_note_closes_fifo(self):
        parsed = lib.parse_smf(midi([(0, b"\x90\x3c\x64"), (10, b"\x3c\x50"),
                                    (10, b"\x80\x3c\0"), (10, b"\x3c\0")]))
        self.assertEqual([(n.tick, n.duration) for n in parsed.notes], [(0, 20), (10, 20)])

    def test_bad_header_track_or_vlq_rejected(self):
        for raw in (b"garbage", midi([(0, b"\x90\x3c")]),
                    midi([(0, b"\x90\x3c\x64")])[:-2],
                    midi([(0, b"\x90\x3c\x64")]).replace(b"\0\x90", b"\x80\x80\x80\x80\0\x90", 1)):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                lib.parse_smf(raw)

    def test_realtime_byte_does_not_lose_running_status(self):
        parsed = lib.parse_smf(midi([(0, b"\x90\x3c\x64"), (0, b"\xf8"), (12, b"\x3c\0")]))
        self.assertEqual(parsed.notes[0].duration, 12)

    def test_metadata_tempo_key_and_name(self):
        parsed = lib.parse_smf(midi([(0, b"\xff\x51\x03\x07\xa1\x20"),
                                    (0, b"\xff\x59\x02\xfc\x01"),
                                    (0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")], name="Kick"))
        self.assertEqual(parsed.bpm, 120)
        self.assertEqual(parsed.keys, [(5, 2)])  # four flats -> F minor
        self.assertEqual(parsed.track_names[0], "Kick")

    def test_filename_key_over_profile_with_flat_minor(self):
        parsed = lib.parse_smf(midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")]))
        self.assertEqual(lib.infer_key("Darkwave - Lead 150 Abm.mid", parsed)[:2], (8, 2))
        self.assertEqual(lib.infer_key("Chord_Loop_120_Cm.mid", parsed)[:2], (0, 2))

    def test_explicit_filename_overrides_default_metadata_key(self):
        parsed = lib.parse_smf(midi([(0, b"\xff\x59\x02\0\0"),
                                    (0, b"\x90\x41\x64"), (96, b"\x80\x41\0")]))
        self.assertEqual(lib.infer_key("Classic Deep House - Bassloop_122_Fm.mid", parsed)[:2], (5, 2))

    def test_dedup_manifest_paths_survive_library_relocation(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        def aliases(root):
            return {"root": "/old/midi", "duplicates": [
                {"keep": "/old/midi/House/Bass/Keep Cm.mid",
                 "remove": "/old/midi/Trap/Lead/Removed Cm.mid"}]}
        _, binary, _ = self.compile_tree({"House/Bass/Keep Cm.mid": raw}, aliases)
        self.assertEqual(lib.HEADER.unpack_from(binary)[3], 2)

    def test_track_names_map_non_gm_single_pitch_drums(self):
        self.assertEqual(lib.drum_lane(60, "AH1_Kick 03", "Full_Loop.mid")[0], 0)
        self.assertEqual(lib.drum_lane(60, "OH Hat", "")[0], 3)
        self.assertEqual(lib.drum_lane(60, "CD Hat", "")[0], 2)
        self.assertEqual(lib.drum_lane(60, "Snare", "")[0], 1)
        self.assertEqual(lib.drum_lane(60, "Clap", "")[0], 4)

    def test_gm_drum_mapping(self):
        for note, lane in [(35, 0), (36, 0), (38, 1), (40, 1), (42, 2), (44, 2), (46, 3), (39, 4)]:
            self.assertEqual(lib.drum_lane(note, "", "")[0], lane)

    def test_every_known_folder_has_genre_mapping(self):
        for name in ("Analog Magic", "Chillwave", "Darkwave", "Deep House", "Disco", "Drill",
                     "Drum & Bass", "EDM", "Electric Dreams", "Electronica", "House", "Indie Dance",
                     "Latin", "Lo-Fi Hip Hop", "Melodic Techno", "Minimal", "Pop", "Reggaeton",
                     "Slap House", "Soul & R&B", "Tech House", "Trap", "ethereal2080 DRE4MSCAPE"):
            self.assertGreater(lib.genre_mask(name), 0, name)

    def compile_tree(self, files, duplicate_manifest=None):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for name, raw in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        if duplicate_manifest is not None:
            (root / "dice-duplicates.json").write_text(json.dumps(duplicate_manifest(root)))
        return root, *lib.build_library(root)

    def test_binary_offsets_timing_and_trailing_eot_ignored(self):
        # One bar leading silence, then notes at a microtimed position. EOT far beyond the phrase.
        raw = midi([(389, b"\x90\x24\x58"), (47, b"\x80\x24\0"),
                    (10000, b"\xff\x01\x01x")])
        root, binary, manifest = self.compile_tree({"House/Bass/Phrase Cm.mid": raw})
        self.assertEqual(lib.HEADER.unpack_from(binary)[:4], (b"DML1", 1, 64, 1))
        entry = lib.ENTRY.unpack_from(binary, lib.HEADER.size)
        self.assertEqual(entry[1:6], (1, 0, 2, 1, 0))
        self.assertEqual(entry[6], 80)
        event = lib.EVENT.unpack_from(binary, entry[6])
        self.assertEqual(event[:6], (5, 47, 36, 88, 255, 0))
        self.assertEqual(manifest["entries"][0]["trimmed_leading_bars"], 1)

    def test_tail_just_past_four_bars_is_clipped_without_adding_silence(self):
        raw = midi([(0, b"\x90\x3c\x64"), (24, b"\x80\x3c\0"),
                    (1476, b"\x90\x40\x58"), (44, b"\x80\x40\0")])
        _, binary, manifest = self.compile_tree({"House/Chords/Tail Cm.mid": raw})
        row = lib.ENTRY.unpack_from(binary, 16)
        self.assertEqual(row[4], 4)
        events = [lib.EVENT.unpack_from(binary, row[6] + i * 8) for i in range(row[7] // 8)]
        self.assertEqual(events[1][:4], (1500, 36, 64, 88))
        self.assertEqual(len(events), 2)
        self.assertIn("note tails clipped to standard phrase boundary", manifest["entries"][0]["warnings"])

    def test_real_attack_after_four_bars_keeps_eight_bar_phrase(self):
        raw = midi([(0, b"\x90\x3c\x64"), (24, b"\x80\x3c\0"),
                    (1517, b"\x90\x40\x58"), (8, b"\x80\x40\0")])
        _, binary, _ = self.compile_tree({"House/Chords/Attack Cm.mid": raw})
        row = lib.ENTRY.unpack_from(binary, 16)
        self.assertEqual(row[4], 8)
        event = lib.EVENT.unpack_from(binary, row[6] + 8)
        self.assertEqual(event[:4], (1541, 8, 64, 88))

    def test_unquantized_attack_before_boundary_preserves_timing_and_velocity(self):
        raw = midi([(0, b"\x90\x3c\x64"), (24, b"\x80\x3c\0"),
                    (1507, b"\x90\x40\x58"), (13, b"\x80\x40\0")])
        _, binary, _ = self.compile_tree({"House/Lead/Timing Cm.mid": raw})
        row = lib.ENTRY.unpack_from(binary, 16)
        self.assertEqual(row[4], 4)
        event = lib.EVENT.unpack_from(binary, row[6] + 8)
        self.assertEqual(event[:4], (1531, 5, 64, 88))

    def test_preserve_chord_events_for_core_conversion(self):
        raw = midi([(0, b"\x90\x3c\x64"), (0, b"\x40\x5a"),
                    (0, b"\x43\x50"), (96, b"\x80\x3c\0"),
                    (0, b"\x40\0"), (0, b"\x43\0")])
        _, binary, manifest = self.compile_tree({"House/Chords/Cm.mid": raw})
        entry = lib.ENTRY.unpack_from(binary, 16)
        self.assertEqual(entry[7], 24)
        self.assertEqual([lib.EVENT.unpack_from(binary, entry[6] + i * 8)[2] for i in range(3)], [60, 64, 67])

    def test_split_long_phrase_into_bounded_events(self):
        events = []
        for i in range(600):
            events.extend([(1 if i else 0, b"\x90\x3c\x64"), (1, b"\x80\x3c\0")])
        _, binary, manifest = self.compile_tree({"House/Arp/Busy Cm.mid": midi(events)})
        self.assertGreater(len(manifest["entries"]), 1)
        for i in range(len(manifest["entries"])):
            row = lib.ENTRY.unpack_from(binary, 16 + i * 64)
            self.assertLessEqual(row[7] // 8, 512)
            self.assertIn(row[4], (1, 2, 4, 8))

    def test_invalid_file_reported_without_losing_good_file(self):
        _, binary, manifest = self.compile_tree({
            "House/Bass/Good Cm.mid": midi([(0, b"\x90\x24\x64"), (96, b"\x80\x24\0")]),
            "House/Bass/Bad.mid": b"not a midi"})
        self.assertEqual(len(manifest["rejected"]), 1)
        self.assertEqual(lib.HEADER.unpack_from(binary)[3], 1)

    def test_deduplicated_alias_keeps_other_role_and_genre(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        def aliases(root):
            return {"duplicates": [{"keep": str(root / "House/Bass/Keep Cm.mid"),
                                    "remove": str(root / "Trap/Lead/Removed Cm.mid")}]}
        _, binary, manifest = self.compile_tree({"House/Bass/Keep Cm.mid": raw}, aliases)
        rows = [lib.ENTRY.unpack_from(binary, 16 + i * 64) for i in range(lib.HEADER.unpack_from(binary)[3])]
        self.assertEqual({r[1] for r in rows}, {1, 3})
        self.assertEqual(len(manifest["entries"]), 2)

    def test_same_role_duplicate_alias_does_not_bias_selection(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        def aliases(root):
            return {"duplicates": [{"keep": str(root / "House/Bass/Keep Cm.mid"),
                                    "remove": str(root / "House/Bass/Other Cm.mid")}]}
        _, binary, manifest = self.compile_tree({"House/Bass/Keep Cm.mid": raw}, aliases)
        self.assertEqual(lib.HEADER.unpack_from(binary)[3], 1)
        self.assertIn("House/Bass/Other Cm.mid", manifest["entries"][0]["aliases"])

    def test_cli_dry_run_and_original_output_collision(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        root, _, _ = self.compile_tree({"House/Bass/A Cm.mid": raw})
        self.assertEqual(lib.main([str(root), "--dry-run"]), 0)
        self.assertFalse((root / "dice-library.dml").exists())
        with self.assertRaises(SystemExit):
            lib.main([str(root), "--output", str(root / "House/Bass/A Cm.mid")])
        self.assertEqual((root / "House/Bass/A Cm.mid").read_bytes(), raw)

    def test_duplicate_payloads_share_storage(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        _, binary, _ = self.compile_tree({"House/Bass/A Cm.mid": raw, "House/Lead/B Cm.mid": raw})
        a, b = lib.ENTRY.unpack_from(binary, 16), lib.ENTRY.unpack_from(binary, 80)
        self.assertEqual(a[6:8], b[6:8])

    def test_long_unicode_names_are_valid_and_distinct(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        _, binary, _ = self.compile_tree({
            "House/Bass/" + "Мелодия" * 8 + "A Cm.mid": raw,
            "House/Lead/" + "Мелодия" * 8 + "B Cm.mid": raw})
        a = lib.ENTRY.unpack_from(binary, 16)[8].rstrip(b"\0")
        b = lib.ENTRY.unpack_from(binary, 80)[8].rstrip(b"\0")
        a.decode("utf8")
        b.decode("utf8")
        self.assertNotEqual(a, b)

    def test_compilation_is_deterministic_and_does_not_modify_sources(self):
        raw = midi([(0, b"\x90\x3c\x64"), (96, b"\x80\x3c\0")])
        root, binary, manifest = self.compile_tree({"House/Bass/A Cm.mid": raw})
        second, second_manifest = lib.build_library(root)
        self.assertEqual(binary, second)
        self.assertEqual(manifest, second_manifest)
        self.assertEqual((root / "House/Bass/A Cm.mid").read_bytes(), raw)


if __name__ == "__main__":
    unittest.main()
