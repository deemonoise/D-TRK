"""Bridge Brick volume key events when KNULLI's game session skips system hotkeys."""
import contextlib
from pathlib import Path
import select
import struct
import subprocess
import sys
import threading
import time

EVENT = struct.Struct("@llHHi")
DOWN, UP, MODE = 114, 115, 0x13c
DEVICES = ("sunxi-keyboard", "TRIMUI Brick Controller")


class Keys:
    def __init__(self, get, set_level, limit):
        self.get = get
        self.set_level = set_level
        self.limit = limit
        self.held = set()
        self.mode = False
        self.unpaired = {DOWN: [], UP: []}
        self.base = self.target = self.step = None
        self.due = self.repeat = None

    def adjust(self):
        self.target = min(self.limit, max(0, self.target + self.step))

    def mirrored(self, source, code, stamp):
        # One physical press can arrive through both evdev nodes, even after the first node's
        # release. Match each down once and retain rapid successive presses on the same node.
        recent = self.unpaired[code]
        recent[:] = [(other, when) for other, when in recent if abs(stamp - when) <= .05]
        for entry in recent:
            if entry[0] != source:
                recent.remove(entry)
                return True
        if any(k == code for _, k in self.held):
            return True
        recent.append((source, stamp))
        return False

    def begin_press(self, source, code, now, stamp):
        duplicate = self.mirrored(source, code, stamp)
        self.held.add((source, code))
        if duplicate or self.mode:
            return
        self.step = 5 if code == UP else -5
        if self.due is None:
            self.base = self.target = self.get()
        self.adjust()
        self.due, self.repeat = now + .18, now + .5

    def key(self, source, code, value, now, stamp=None):
        if code == MODE:
            self.mode = bool(value)
            if self.mode:
                self.due = self.repeat = None
            return
        if code not in (UP, DOWN):
            return
        key = (source, code)
        if value == 0:
            self.held.discard(key)
            if not self.held:
                self.repeat = None
            return
        if value != 1 or key in self.held:
            return
        self.begin_press(source, code, now, now if stamp is None else stamp)

    def tick(self, now):
        if self.due is not None and now >= self.due:
            current = self.get()
            if current != self.base:
                self.repeat = None  # KNULLI handled the press and owns its repeat.
            elif current != self.target:
                self.set_level(self.target)
            self.due = None
        if self.repeat is not None and now >= self.repeat:
            self.adjust()
            self.set_level(self.target)
            self.repeat = now + .1

    def timeout(self, now):
        return max(0., min(t for t in (self.due, self.repeat, now + .25) if t is not None) - now)


def input_files(stack):
    inputs = []
    for path in Path("/sys/class/input").glob("event*"):
        try:
            if (path / "device/name").read_text().strip() in DEVICES:
                inputs.append(stack.enter_context(open("/dev/input/" + path.name, "rb", buffering=0)))
        except OSError:
            continue  # Device disappeared during enumeration or is unavailable to this session.
    return inputs


def read_events(readers, inputs):
    events = []
    for reader in readers:
        data = reader.read(EVENT.size * 64)
        if not data:
            inputs.remove(reader)
            continue
        for sec, usec, kind, code, value in EVENT.iter_unpack(data):
            if kind == 1:
                events.append((sec + usec / 1_000_000, reader.fileno(), code, value))
    return sorted(events, key=lambda event: event[0])


def follow(inputs, keys, stopped):
    try:
        while not stopped.is_set():
            readers = select.select(inputs, [], [], keys.timeout(time.monotonic()))[0]
            for stamp, source, code, value in read_events(readers, inputs):
                keys.key(source, code, value, time.monotonic(), stamp)
            keys.tick(time.monotonic())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Brick volume keys: {error}", file=sys.stderr)


@contextlib.contextmanager
def watch(get, set_level, limit):
    with contextlib.ExitStack() as stack:
        inputs = input_files(stack)
        if inputs:
            stopped = threading.Event()
            thread = threading.Thread(target=follow, args=(inputs, Keys(get, set_level, limit), stopped))
            thread.start()
            stack.callback(thread.join)
            stack.callback(stopped.set)
        yield
