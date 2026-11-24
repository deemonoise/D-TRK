"""Give D-TRK exclusive use of the Brick's built-in codec for the lifetime of its launcher."""
import contextlib
import ctypes
import fcntl
import json
import mmap
import os
from pathlib import Path
import re
import select
import signal
import struct
import subprocess
import sys
import time
import tempfile

import volume_keys

CODEC = "_sys_devices_platform_soc_sndcodec_sound_card0"
CARD = "alsa_card." + CODEC
SIGNALS = (signal.SIGHUP, signal.SIGINT, signal.SIGTERM)


def run(*args):
    return subprocess.check_output(args, text=True, timeout=5).strip()


def mixer_value(name):
    key = "iface=MIXER,name=" + name
    info = run("amixer", "-c", "0", "cget", key)
    match = re.search(r"^\s*: values=([^\n]+)$", info, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Cannot read mixer control {key}")
    return match[1]


def controls():
    names = ("ADC volume", "ADCL Input MIC1 Boost Switch", "ADCR Input MIC2 Boost Switch", "MIC1 gain volume")
    return {"iface=MIXER,name=" + name: mixer_value(name) for name in names}


def sink_level(sink):
    sinks = json.loads(run("pactl", "-f", "json", "list", "sinks"))
    for item in sinks:
        if item["name"] == sink:
            return tuple(v["value"] for v in item["volume"].values()), item["mute"]
    raise RuntimeError(f"Cannot find audio sink {sink}")


def sink_commands(sink, level):
    channels, muted = level
    return [("pactl", "set-sink-volume", sink, *map(str, channels)),
            ("pactl", "set-sink-mute", sink, str(int(muted)))]


def volume_percent(sink):
    match = re.search(r"(\d+)%", run("pactl", "get-sink-volume", sink))
    if not match:
        raise RuntimeError(f"Cannot read level for {sink}")
    return int(match[1])


def key_monitor(get, write):
    try:
        limit = int(run("knulli-settings-get", "audio.volume.boost"))
    except (OSError, ValueError, subprocess.SubprocessError):
        limit = 100
    return volume_keys.watch(get, write, limit)


def linear_gain(level):
    # PulseAudio volume is cubic. Convert its per-channel values to linear Q16 for SDL.
    channels, muted = level
    if muted:
        return (0, 0)
    return tuple(min(0xffffffff, round(value ** 3 / 65536 ** 2)) for value in channels)


class DirectVolume:
    def __init__(self, sink):
        self.sink = sink
        self.level = sink_level(sink)
        if len(self.level[0]) != 2:
            raise RuntimeError("Built-in audio volume requires a stereo codec")

    def gain_file(self, route):
        file = route.enter_context(tempfile.NamedTemporaryFile(dir="/tmp", prefix="d-trk-volume-"))
        os.ftruncate(file.fileno(), 8)
        self.mapping = route.enter_context(mmap.mmap(file.fileno(), 8))
        self.apply()
        return file.name

    def apply(self):
        # The AArch64 reader loads the same aligned pair with a single 64-bit atomic operation.
        packed = struct.pack("<II", *linear_gain(self.level))
        ctypes.c_uint64.from_buffer(self.mapping).value = int.from_bytes(packed, sys.byteorder)

    def percent(self):
        return round(self.level[0][0] * 100 / 65536)

    def change(self, percent):
        value = round(percent * 65536 / 100)
        self.level = ((value, value), self.level[1])
        self.apply()

    def recovery(self):
        return sink_commands(self.sink, self.level)


def wait_for_release():
    paths = [Path(f"/proc/asound/card0/pcm0{direction}/sub0/status") for direction in ("p", "c")]
    deadline = time.monotonic() + 2
    while any(path.read_text().strip() != "closed" for path in paths):
        if time.monotonic() >= deadline:
            raise RuntimeError("ALSA hw:0,0 still busy after releasing the KNULLI profile")
        time.sleep(0.025)


def restore(commands):
    # Profile recreation in PipeWire can be asynchronous. Retry failed controls/routes individually.
    errors = []
    for args in commands:
        for attempt in range(3):
            try:
                run(*args)
                break
            except (OSError, subprocess.SubprocessError) as error:
                if attempt == 2:
                    errors.append(str(error))
                else:
                    time.sleep(0.2)
    if errors:
        raise RuntimeError("Restoring audio: " + ", ".join(errors))


def recovery_commands(profile, sink, source, previous):
    recovery = [("pactl", "set-card-profile", CARD, profile)]
    if sink.startswith("alsa_output." + CODEC + "."):
        recovery.append(("pactl", "set-default-sink", sink))
    if source.startswith("alsa_input." + CODEC + "."):
        recovery.append(("pactl", "set-default-source", source))
    recovery.extend(("amixer", "-c", "0", "cset", key, value) for key, value in previous.items())
    return recovery


def use_builtin_codec(route):
    cards = json.loads(run("pactl", "-f", "json", "list", "cards"))
    profile = next((c["active_profile"] for c in cards if c["name"] == CARD), None)
    if not profile:
        return False
    sink = run("pactl", "get-default-sink")
    if not sink.startswith("alsa_output." + CODEC + ".") and sink != "auto_null":
        return False
    source = run("pactl", "get-default-source")
    recovery = recovery_commands(profile, sink, source, controls())
    volume = DirectVolume(sink)
    route.callback(lambda: restore(recovery + volume.recovery()))
    run("pactl", "set-card-profile", CARD, "off")
    wait_for_release()
    path = volume.gain_file(route)
    route.enter_context(key_monitor(volume.percent, volume.change))
    return path


@contextlib.contextmanager
def codec_environment():
    env = os.environ.copy()
    with contextlib.ExitStack() as route:
        try:
            path = use_builtin_codec(route)
            if path:
                # The Brick's codec clocks PCM at about 48 kHz even when hw:0,0
                # advertises 44.1 kHz. Let ALSA convert both playback and capture.
                overlay = route.enter_context(tempfile.NamedTemporaryFile(dir="/tmp", prefix="d-trk-alsa-"))
                overlay.write(b'pcm.dtrk_codec { type plug slave { pcm "hw:0,0" rate 48000 } }\n')
                overlay.flush()
                config_dir = env.get("ALSA_CONFIG_DIR") or "/usr/share/alsa"
                config = env.get("ALSA_CONFIG_PATH") or f"{config_dir}/alsa.conf"
                env.update(SDL_AUDIODRIVER="alsa", AUDIODEV="dtrk_codec", DTRK_BRICK_ADC="/dev/snd/controlC0",
                           DTRK_VOLUME_FILE=path, ALSA_CONFIG_PATH=f"{config}:{overlay.name}")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            route.close()
            print(f"Using the system SDL audio route: {error}", file=sys.stderr)
        if "DTRK_BRICK_ADC" not in env:
            route.enter_context(key_monitor(lambda: volume_percent("@DEFAULT_SINK@"),
                                           lambda pct: run("pactl", "set-sink-volume", "@DEFAULT_SINK@", f"{pct}%")))
        yield env


def supervise(owner_pipe, command):
    with open("/var/run/d-trk-audio.lock", "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if select.select([owner_pipe], [], [], 0)[0]:
            return 1
        with codec_environment() as env:
            # The frontend may kill its entire process group. The watchdog and app use
            # separate sessions so it can stop just this child before restoring the card.
            with subprocess.Popen(command, env=env, start_new_session=True) as app:
                try:
                    while app.poll() is None:
                        if select.select([owner_pipe], [], [], 0.1)[0]:
                            break
                finally:
                    if app.poll() is None:
                        app.terminate()
                        try:
                            app.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            app.kill()
                            app.wait()
                return app.returncode


def main():
    # EOF also arrives on an uncatchable SIGKILL to the frontend launcher.
    read_fd, write_fd = os.pipe()
    child = os.fork()
    if child == 0:
        os.close(write_fd)
        os.setsid()
        for sig in SIGNALS:
            signal.signal(sig, lambda *_: None)  # caught signals become default on app exec
        try:
            return supervise(read_fd, sys.argv[1:])
        finally:
            os.close(read_fd)
    os.close(read_fd)

    def stop(*_):
        nonlocal write_fd
        if write_fd >= 0:
            os.close(write_fd)
            write_fd = -1

    for sig in SIGNALS:
        signal.signal(sig, stop)
    try:
        return os.waitstatus_to_exitcode(os.waitpid(child, 0)[1])
    finally:
        stop()


if __name__ == "__main__":
    try:
        result = main()
        sys.exit(128 - result if result < 0 else result)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"D-TRK audio: {error}", file=sys.stderr)
        sys.exit(1)
