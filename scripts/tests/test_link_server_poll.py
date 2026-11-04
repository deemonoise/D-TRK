"""Execute the production poll body with a port that disconnects while dispatching."""
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


ROOT = Path(__file__).absolute().parents[2]
SOURCE = ROOT / "src/teensy/link_server.cpp"

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include "link_frame.h"
using namespace mt::link;
bool connected = true, wasConnected = true, disconnectOnDispatch = false;
int commands = 0;
Decoder dec;
struct Port {
  std::deque<uint8_t> bytes;
  int negativeReads = 0;
  bool disappearAfterSnapshot = false;
  int available() {
    const int size = bytes.size();
    if (disappearAfterSnapshot) { bytes.clear(); disappearAfterSnapshot = false; }
    return size;
  }
  int read() {
    if (bytes.empty()) { ++negativeReads; return -1; }
    const int byte = bytes.front(); bytes.pop_front(); return byte;
  }
  void clear() { bytes.clear(); }
} port;
struct UsbPort { bool connected() { return ::connected; } } usbPort;
struct Writer { bool roomForCritical() { return true; } } writer;
Writer& deskWriter() { return writer; }
void observeDeskConnection() {
  if (wasConnected && !connected) { dec = Decoder{}; port.clear(); }
  wasConnected = connected;
}
void pumpDeskTx() { observeDeskConnection(); }
void dispatch() {
  ++commands;
  if (disconnectOnDispatch) { connected = false; observeDeskConnection(); }
}
uint64_t previewOffUs = 0;
uint8_t previewNote = 0;
constexpr uint32_t kPlayLatencyUs = 4000;
uint64_t micros64() { return 0; }
void pushNow(uint64_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
void statusIfDue() {}
void appendFrame(uint8_t seq) {
  uint8_t data[kMaxEncoded]; const uint8_t payload[] = {1,2,3};
  int n = encode(1,seq,payload,sizeof payload,data);
  for (int i=0;i<n;++i) port.bytes.push_back(data[i]);
}
'''

CASES = r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  if (!strcmp(argv[1], "disconnect")) {
    appendFrame(1); appendFrame(2); disconnectOnDispatch = true;
    poll();
    assert(commands == 1);
    assert(port.negativeReads == 0);
    connected = true; disconnectOnDispatch = false;
    appendFrame(3); poll();
    assert(commands == 2);
    assert(dec.errors() == 0);
  } else if (!strcmp(argv[1], "negative")) {
    appendFrame(1); port.disappearAfterSnapshot = true;
    poll();
    assert(port.negativeReads == 1);
    assert(commands == 0);
    appendFrame(2); poll();
    assert(commands == 1);
    assert(dec.errors() == 0);
  } else {
    assert(!strcmp(argv[1], "uart"));
    connected = false; appendFrame(1); appendFrame(2);
    poll();
    assert(commands == 2);  // normal UART has no USB/DTR admission
  }
}
'''


def production_poll():
    source = SOURCE.read_text()
    start = source.index("void poll() {")
    depth = 0
    for at in range(source.index("{", start), len(source)):
        depth += (source[at] == "{") - (source[at] == "}")
        if not depth:
            return source[start:at + 1]
    raise AssertionError("unterminated production poll")


class LinkServerPollTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        source = folder / "poll.cpp"
        source.write_text(HARNESS + production_poll() + CASES)
        cls.binaries = {}
        for mode, flags in [("desk", ["-DLINK_DESK"]), ("uart", [])]:
            binary = folder / mode
            subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", *flags,
                            "-I" + str(ROOT / "lib/core/src"), str(source),
                            str(ROOT / "lib/core/src/link_frame.cpp"), "-o", str(binary)],
                           capture_output=True, text=True, check=True)
            cls.binaries[mode] = binary

    def run_case(self, mode, case):
        result = subprocess.run([str(self.binaries[mode]), case], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)

    def test_disconnect_during_dispatch_does_not_feed_cleared_rx(self):
        self.run_case("desk", "disconnect")

    def test_negative_read_ends_the_snapshot_without_feeding_ff(self):
        self.run_case("desk", "negative")

    def test_normal_uart_does_not_depend_on_usb_dtr(self):
        self.run_case("uart", "uart")
