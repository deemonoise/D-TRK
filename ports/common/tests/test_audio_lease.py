import contextlib
import io
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import unittest
from unittest.mock import call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "brick"))
import audio_lease as lease


class LeaseTests(unittest.TestCase):
    def test_codec_routes_and_restoration(self):
        codec = lease.CODEC
        sink, source = "alsa_output." + codec + ".speaker", "alsa_input." + codec + ".mic"
        for active, output, input_, direct, alsa_env, base in [
                ("on", sink, source, True, {}, "/usr/share/alsa/alsa.conf"),
                ("on", sink, source, True, {"ALSA_CONFIG_DIR": "/tmp/alsa"}, "/tmp/alsa/alsa.conf"),
                ("on", "auto_null", "bluez_input.a", True,
                 {"ALSA_CONFIG_PATH": "/tmp/base.conf:/tmp/custom.conf"}, "/tmp/base.conf:/tmp/custom.conf"),
                ("on", "bluez_output.a", source, False, {"ALSA_CONFIG_PATH": "/tmp/bluetooth.conf"}, None),
                (None, sink, source, False, {}, None)]:
            with self.subTest(output=output, active=active):
                def answer(*args):
                    if args[1] == "-f":
                        import json
                        return json.dumps([{"name": "unrelated", "active_profile": "broken"},
                                           {"name": lease.CARD, "active_profile": active}])
                    return {"get-default-sink": output, "get-default-source": input_}.get(args[1], "")
                original = {"X": "Y", **alsa_env}
                with patch.object(lease, "DirectVolume") as volume, \
                     patch.object(lease, "run", side_effect=answer) as command, \
                     patch.object(lease, "controls", return_value={"adc": "0,0"}) as adc, \
                     patch.object(lease, "wait_for_release") as wait, patch.dict(os.environ, original, clear=True):
                    volume.return_value.recovery.return_value = []
                    volume.return_value.gain_file.return_value = "/tmp/gain"
                    with self.assertRaisesRegex(RuntimeError, "child error"):
                        with lease.codec_environment() as env:
                            expected = original.copy()
                            if direct:
                                self.assertEqual(env["AUDIODEV"], "dtrk_codec")
                                cfg_path, sep, overlay = env["ALSA_CONFIG_PATH"].rpartition(":")
                                self.assertEqual((cfg_path, sep), (base, ":"))
                                config = Path(overlay)
                                self.assertEqual(config.stat().st_mode & 0o777, 0o600)
                                self.assertRegex(config.read_text(),
                                                 r'pcm\.dtrk_codec\s*\{\s*type plug\s*slave\s*\{\s*'
                                                 r'pcm "hw:0,0"\s*rate 48000\s*\}\s*\}\s*\Z')
                                expected.update(SDL_AUDIODRIVER="alsa", AUDIODEV="dtrk_codec",
                                                ALSA_CONFIG_PATH=env["ALSA_CONFIG_PATH"],
                                                DTRK_BRICK_ADC="/dev/snd/controlC0", DTRK_VOLUME_FILE="/tmp/gain")
                            self.assertEqual(env, expected)
                            raise RuntimeError("child error")
                    if direct:
                        self.assertFalse(config.exists())
                    self.assertEqual(os.environ, original)
                expected = [call("pactl", "-f", "json", "list", "cards")]
                if active:
                    expected.append(call("pactl", "get-default-sink"))
                if direct:
                    expected += [call("pactl", "get-default-source"),
                                 call("pactl", "set-card-profile", lease.CARD, "off"),
                                 call("pactl", "set-card-profile", lease.CARD, active)]
                    if output == sink:
                        expected.append(call("pactl", "set-default-sink", sink))
                    if input_ == source:
                        expected.append(call("pactl", "set-default-source", source))
                    expected.append(call("amixer", "-c", "0", "cset", "adc", "0,0"))
                boost = call("knulli-settings-get", "audio.volume.boost")
                self.assertIn(boost, command.call_args_list)
                self.assertEqual([c for c in command.call_args_list if c != boost], expected)
                self.assertEqual(adc.call_count, int(direct))
                self.assertEqual(wait.call_count, int(direct))

    def test_stereo_gain_shared_with_sdl_and_level_restored_to_os(self):
        with patch.object(lease, "sink_level", return_value=((29491, 20000), False)) as read:
            volume = lease.DirectVolume("internal")
            with contextlib.ExitStack() as route:
                path = Path(volume.gain_file(route))
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)
                self.assertEqual(struct.unpack("<II", path.read_bytes()), (5972, 1863))
                self.assertEqual(volume.percent(), 45)
                volume.change(40)
                self.assertEqual(volume.percent(), 40)
                self.assertEqual(struct.unpack("<II", path.read_bytes()), (4194, 4194))
            self.assertFalse(path.exists())
            read.assert_called_once_with("internal")
            self.assertEqual(volume.recovery(), [("pactl", "set-sink-volume", "internal", "26214", "26214"),
                                               ("pactl", "set-sink-mute", "internal", "0")])

    def test_preserves_user_mute_and_channel_balance(self):
        with patch.object(lease, "sink_level", return_value=((30000, 15000), True)):
            volume = lease.DirectVolume("internal")
            self.assertEqual(volume.percent(), 46)
            self.assertEqual(volume.recovery(), [("pactl", "set-sink-volume", "internal", "30000", "15000"),
                                               ("pactl", "set-sink-mute", "internal", "1")])
            with contextlib.ExitStack() as route:
                path = Path(volume.gain_file(route))
                volume.change(30)
                self.assertEqual(path.read_bytes(), bytes(8))
        with patch.object(lease, "sink_level", return_value=((0,), False)):
            with self.assertRaisesRegex(RuntimeError, "stereo codec"):
                lease.DirectVolume("mono")
        self.assertEqual(lease.linear_gain(((0,65536), False)), (0,65536))
        self.assertEqual(lease.linear_gain(((131072,65536), False)), (524288,65536))

    def test_sink_levels_and_mute_use_channel_order_and_exact_values(self):
        with patch.object(lease, "run", return_value='[{"name":"other"},{"name":"a","volume":'
                          '{"front-left":{"value":3},"front-right":{"value":17}},"mute":true}]') as run:
            self.assertEqual(lease.sink_level("a"), ((3, 17), True))
            with self.assertRaisesRegex(RuntimeError, "Cannot find audio sink"):
                lease.sink_level("absent")
            lease.restore(lease.sink_commands("a", ((3, 17), True)))
            self.assertEqual(run.call_args_list[-2:], [call("pactl", "set-sink-volume", "a", "3", "17"),
                                                       call("pactl", "set-sink-mute", "a", "1")])

    def test_unavailable_mixer_releases_codec_before_system_fallback(self):
        sink = "alsa_output." + lease.CODEC + ".speaker"
        cards = '[{"name":"' + lease.CARD + '","active_profile":"on"}]'
        calls = []
        def command(*args):
            calls.append(args)
            return {"-f": cards, "get-default-sink": sink, "get-default-source": "mic"}.get(args[1], "")
        with patch.object(lease, "run", side_effect=command), patch.object(lease, "controls", return_value={}), \
             patch.object(lease, "DirectVolume") as volume, patch.object(lease, "wait_for_release"), \
             contextlib.redirect_stderr(io.StringIO()):
            volume.return_value.recovery.return_value = []
            volume.return_value.gain_file.side_effect = RuntimeError("cannot map gain")
            with lease.codec_environment() as env:
                self.assertEqual(env, dict(os.environ))
                self.assertEqual(calls[-3:-1], [("pactl", "set-card-profile", lease.CARD, "on"),
                                              ("pactl", "set-default-sink", sink)])

    def test_missing_pactl_or_invalid_json_preserves_environment(self):
        for error in [FileNotFoundError(), ValueError("invalid")]:
            with patch.object(lease, "run", side_effect=error), contextlib.redirect_stderr(io.StringIO()):
                with lease.codec_environment() as env:
                    self.assertEqual(env, dict(os.environ))

    def test_query_controls_and_restore_retries(self):
        keys = ["iface=MIXER,name=" + key for key in
                ("ADC volume", "ADCL Input MIC1 Boost Switch", "ADCR Input MIC2 Boost Switch", "MIC1 gain volume")]
        values = ("0,0", "off", "on", "31")
        with patch.object(lease, "run", side_effect=[f"  : values={value}" for value in values]) as run:
            previous = lease.controls()
            self.assertEqual(previous, dict(zip(keys, values)))
            self.assertEqual(run.call_args_list, [call("amixer", "-c", "0", "cget", key) for key in keys])
            self.assertIn(("amixer", "-c", "0", "cset", keys[-1], "31"),
                          lease.recovery_commands("on", "sink", "source", previous))
        with patch.object(lease, "run", return_value="wrong"):
            with self.assertRaisesRegex(RuntimeError, "Cannot read mixer control"):
                lease.controls()
        with patch.object(lease, "run", side_effect=[OSError("one"), "", OSError("two"),
                                                      OSError("two"), OSError("final"), ""]) as run, \
             patch.object(lease.time, "sleep") as sleep:
            with self.assertRaisesRegex(RuntimeError, "final"):
                lease.restore([("first",), ("second",), ("third",)])
            self.assertEqual(run.call_args_list, [call("first")] * 2 + [call("second")] * 3 + [call("third")])
            self.assertEqual(sleep.call_args_list, [call(.2)] * 3)

    def test_wait_both_directions_or_timeout(self):
        for busy in [False, True]:
            with patch.object(Path, "read_text", autospec=True, side_effect=["closed", "RUNNING",
                                                                            "closed", "closed"]) as read, \
                 patch.object(lease.time, "monotonic", side_effect=[1, 3 if busy else 2.999]), \
                 patch.object(lease.time, "sleep") as sleep:
                if busy:
                    with self.assertRaisesRegex(RuntimeError, "busy"):
                        lease.wait_for_release()
                else:
                    lease.wait_for_release()
                paths = [Path(f"/proc/asound/card0/pcm0{d}/sub0/status") for d in "pc"]
                self.assertEqual([args.args[0] for args in read.call_args_list], paths * (1 if busy else 2))
                self.assertEqual(sleep.call_args_list, [] if busy else [call(.025)])

    def test_watchdog_exits_or_terminates_owned_child(self):
        for mode in ["gone", "success", "term", "kill"]:
            with self.subTest(mode=mode), patch("builtins.open", create=True) as opened, \
                 patch.object(lease.fcntl, "flock") as flock, patch.object(lease.select, "select") as select, \
                 patch.object(lease, "codec_environment", return_value=contextlib.nullcontext({"A": "B"})), \
                 patch.object(subprocess, "Popen") as launch:
                app = launch.return_value.__enter__.return_value
                app.poll.side_effect = [27, 27] if mode == "success" else [None, None]
                app.returncode = 27
                if mode == "kill":
                    app.wait.side_effect = [subprocess.TimeoutExpired("app", 3), -9]
                select.side_effect = [([4], [], [])] if mode == "gone" else [([], [], []), ([4], [], [])]
                result = lease.supervise(4, ["d-trk", "--test"])
                opened.assert_called_once_with("/var/run/d-trk-audio.lock", "a")
                flock.assert_called_once_with(opened.return_value.__enter__.return_value,
                                              lease.fcntl.LOCK_EX | lease.fcntl.LOCK_NB)
                self.assertEqual(result, 1 if mode == "gone" else 27)
                if mode == "gone":
                    launch.assert_not_called()
                else:
                    launch.assert_called_once_with(["d-trk", "--test"], env={"A": "B"}, start_new_session=True)
                self.assertEqual(app.terminate.call_count, int(mode in ["term", "kill"]))
                self.assertEqual(app.kill.call_count, int(mode == "kill"))
                waits = [call(timeout=3)] if mode in ["term", "kill"] else []
                self.assertEqual(app.wait.call_args_list, waits + ([call()] if mode == "kill" else []))
                self.assertEqual(select.call_args_list, [call([4], [], [], 0)] +
                                 ([call([4], [], [], .1)] if mode in ["term", "kill"] else []))

    def test_watchdog_fds_and_signals(self):
        for child, writer in [(0, 43), (131, 43), (131, 0)]:
            with patch.object(os, "pipe", return_value=(42, writer)), patch.object(os, "fork", return_value=child), \
                 patch.object(os, "close") as close, patch.object(os, "setsid") as setsid, \
                 patch.object(signal, "signal") as handler, patch.object(os, "waitpid") as wait, \
                 patch.object(lease, "supervise", return_value=11) as supervise:
                def waitpid(pid, options):
                    self.assertEqual((pid, options), (131, 0))
                    for registered in handler.call_args_list:
                        registered.args[1]()
                    return pid, 17 << 8
                wait.side_effect = waitpid
                self.assertEqual(lease.main(), 11 if child == 0 else 17)
                self.assertEqual(close.call_args_list, [call(writer), call(42)] if not child else [call(42), call(writer)])
                self.assertEqual(setsid.call_count, int(not child))
                self.assertEqual([c.args[0] for c in handler.call_args_list], list(lease.SIGNALS))
                if not child:
                    for registered in handler.call_args_list:
                        self.assertIsNone(registered.args[1]())
                    supervise.assert_called_once_with(42, sys.argv[1:])
                else:
                    supervise.assert_not_called()

    def test_command_timeout_and_output(self):
        with patch.object(subprocess, "check_output", return_value="  on\n") as command:
            self.assertEqual(lease.run("pactl", "get-default-source"), "on")
            command.assert_called_once_with(("pactl", "get-default-source"), text=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
