import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, call, patch

sys.path.insert(0, str(Path(__file__).absolute().parents[1]))
import audio_lease
import volume_keys as volume


class VolumeKeysTests(unittest.TestCase):
    def test_echo_or_fallback_and_double_press(self):
        for handled in [True, False]:
            get = Mock(side_effect=[45, 35 if handled else 45])
            set_level = Mock()
            keys = volume.Keys(get, set_level, 100)
            for t in [0., .1]:
                keys.key(3, volume.DOWN, 1, t)
                keys.key(3, volume.DOWN, 0, t + .025)
            keys.tick(.27)
            set_level.assert_not_called()
            keys.tick(.28)
            self.assertEqual(set_level.call_args_list, [] if handled else [call(35)])
            keys.tick(1)
            self.assertEqual(get.call_count, 2)

    def test_bounds_repeat_release_and_duplicate_sources(self):
        get, write = Mock(return_value=95), Mock()
        keys = volume.Keys(get, write, 100)
        keys.key(0, volume.UP, 1, 0.)
        keys.key(3, volume.UP, 1, .01)
        keys.key(3, volume.UP, 1, .02)
        keys.key(0, volume.UP, 2, .03)
        keys.tick(.18)
        self.assertEqual(write.call_args_list, [call(100)])
        keys.key(0, volume.UP, 0, .3)
        keys.tick(.49)
        keys.tick(.5)
        self.assertEqual(write.call_args_list, [call(100)] * 2)
        keys.tick(.6)
        keys.key(3, volume.UP, 0, .61)
        keys.tick(1)
        self.assertEqual(write.call_args_list, [call(100)] * 3)
        self.assertEqual(keys.timeout(1), .25)
        keys = volume.Keys(lambda: 2, write, 100)
        keys.key(3, volume.DOWN, 1, 0.)
        self.assertEqual(keys.timeout(.19), 0)
        keys.tick(.2)
        self.assertEqual(write.call_args.args, (0,))

    def test_brightness_chord_and_native_repeat(self):
        get, write = Mock(side_effect=[45, 40]), Mock()
        keys = volume.Keys(get, write, 100)
        keys.key(3, 304, 1, 0)
        keys.key(3, volume.MODE, 1, 0)
        keys.key(0, volume.UP, 1, .01)
        keys.key(0, volume.UP, 0, .05)
        keys.key(3, volume.MODE, 0, .055)
        keys.tick(.2)
        get.assert_not_called()
        keys.key(0, volume.DOWN, 1, .3)
        keys.tick(.49)
        keys.tick(.81)
        write.assert_not_called()
        self.assertEqual(get.call_count, 2)

    def test_native_rounding_owns_repeat(self):
        get, write = Mock(side_effect=[40, 44]), Mock()
        keys = volume.Keys(get, write, 100)
        keys.key(1, volume.UP, 1, 0)
        keys.tick(.181)
        keys.tick(.7)
        write.assert_not_called()

    def test_mirrored_batches_and_fast_primary_clicks(self):
        for streams, expected in [
            ([(1, 0.), (2, .007)], 55),
            ([(1, 0.), (2, .05)], 55),
            ([(1, 0.), (1, .03)], 60),
            ([(1, 0.), (1, .03), (2, .006), (2, .036)], 60),
            ([(1, 0.), (2, .13)], 60),
        ]:
            with self.subTest(streams=streams):
                write = Mock()
                keys = volume.Keys(lambda: 50, write, 100)
                for source, timestamp in streams:
                    keys.key(source, volume.UP, 1, timestamp)
                    keys.key(source, volume.UP, 0, timestamp + .002)
                keys.tick(.4)
                self.assertEqual(write.call_args_list, [call(expected)])

    def test_long_overlap_then_fresh_click_and_stray_kernel_repeat(self):
        write = Mock()
        keys = volume.Keys(lambda: 50, write, 100)
        keys.key(1, volume.UP, 2, 0)
        keys.tick(.8)
        write.assert_not_called()
        keys.key(1, volume.UP, 1, 2)
        keys.key(2, volume.UP, 1, 2.09)
        keys.key(1, volume.UP, 0, 2.1)
        keys.key(2, volume.UP, 0, 2.11)
        keys.key(1, volume.UP, 1, 2.12)
        keys.tick(2.4)
        self.assertEqual(write.call_args_list, [call(60)])
        keys.tick(2.63)
        keys.tick(2.65)
        keys.tick(2.74)
        self.assertEqual(write.call_args_list, [call(60), call(65), call(70)])

    def test_timestamp_order_for_two_batches_and_removing_eof(self):
        with tempfile.TemporaryFile() as first, tempfile.TemporaryFile() as second:
            first.write(volume.EVENT.pack(20, 6000, 1, volume.UP, 1))
            first.write(volume.EVENT.pack(20, 19000, 1, volume.UP, 0))
            second.write(volume.EVENT.pack(20, 1000, 1, volume.UP, 1))
            second.write(volume.EVENT.pack(20, 21000, 1, volume.UP, 0))
            for stream in [first, second]:
                stream.seek(0)
            inputs = [first, second]
            self.assertEqual(volume.read_events(inputs.copy(), inputs), [
                (20.001, second.fileno(), volume.UP, 1), (20.006, first.fileno(), volume.UP, 1),
                (20.019, first.fileno(), volume.UP, 0), (20.021, second.fileno(), volume.UP, 0)])
            self.assertEqual(volume.read_events([first], inputs), [])
            self.assertEqual(inputs, [second])

    def test_press_then_brightness_modifier_cancels_volume(self):
        write = Mock()
        keys = volume.Keys(lambda: 50, write, 100)
        keys.key(1, volume.UP, 1, 0)
        keys.key(2, volume.MODE, 1, .03)
        keys.key(2, volume.MODE, 0, .06)
        keys.tick(.7)
        write.assert_not_called()

    def test_worker_stops_before_descriptors_close_on_exception(self):
        observed = []
        def register(stack):
            stack.callback(observed.append, "closed")
            return [object()]
        worker = Mock()
        def make_worker(*, target, args):
            self.assertIs(target, volume.follow)
            stopped = args[2]
            worker.join.side_effect = lambda: observed.append("stopped" if stopped.is_set() else "running")
            return worker
        with patch.object(volume, "input_files", side_effect=register), patch.object(volume.threading, "Thread", side_effect=make_worker):
            with self.assertRaisesRegex(RuntimeError, "tracker"):
                with volume.watch(None, None, 100):
                    worker.start.assert_called_once_with()
                    raise RuntimeError("tracker")
        self.assertEqual(observed, ["stopped", "closed"])

    def test_only_brick_input_devices_are_opened(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.ExitStack() as stack:
            root = Path(tmp)
            for i, name in enumerate((*volume.DEVICES, "USB keyboard")):
                path = root / f"event{i}" / "device"
                path.mkdir(parents=True)
                (path / "name").write_text(name)
            with patch.object(volume, "Path", return_value=root), patch("builtins.open") as opened:
                self.assertEqual(len(volume.input_files(stack)), 2)
                self.assertCountEqual(opened.call_args_list, [call("/dev/input/event0", "rb", buffering=0),
                                                              call("/dev/input/event1", "rb", buffering=0)])

    def test_record_stream(self):
        with tempfile.TemporaryFile() as stream:
            stream.write(volume.EVENT.pack(0, 0, 1, 114, 1) + volume.EVENT.pack(0, 0, 0, 0, 0))
            stream.seek(0)
            keys = Mock()
            keys.timeout.return_value = 0.2
            stopped = threading.Event()
            def poll(*_):
                stopped.set()
                return [stream], [], []
            with patch.object(volume.select, "select", side_effect=poll), patch.object(volume.time, "monotonic", return_value=1):
                volume.follow([stream], keys, stopped)
            keys.key.assert_called_once_with(stream.fileno(), 114, 1, 1, 0.)
            keys.tick.assert_called_once_with(1)
        with patch.object(volume, "input_files", return_value=[]), patch.object(volume.threading, "Thread") as worker:
            with volume.watch(None, None, 100):
                pass
            worker.assert_not_called()

    def test_route_volume_and_codec_readiness(self):
        with patch.object(audio_lease, "run", return_value="145"), patch.object(volume, "watch") as watch:
            get, write = Mock(), Mock()
            audio_lease.key_monitor(get, write)
            watch.assert_called_once_with(get, write, 145)
        with patch.object(audio_lease, "run", return_value="Volume: 40%"):
            self.assertEqual(audio_lease.volume_percent("system"), 40)
        with patch.object(audio_lease, "run", return_value="?"):
            with self.assertRaisesRegex(RuntimeError, "Cannot read level"):
                audio_lease.volume_percent("bad")
