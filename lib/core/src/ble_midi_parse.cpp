#include "ble_midi_parse.h"

namespace mt {

int midiDataLen(uint8_t status) {
  const uint8_t k = status & 0xF0;
  return (k == 0xC0 || k == 0xD0) ? 1 : 2;
}

// A high byte right after a timestamp is a status (or real-time / SysEx end); any other high
// byte is a timestamp. So real-time is recognised only with its own timestamp, as the spec
// sends it. It ends the "after timestamp" state: the next high byte is a timestamp again, and
// data after it continues the interrupted message.
//
// A partial message is dropped at the start of each packet: BLE-MIDI never splits non-SysEx
// messages, so leftover data means a lost notify or a non-compliant sender, and carrying it over
// would splice two packets into a wrong message. Running status itself carries over.
void BleMidiParser::feed(const uint8_t* b, int n, Sink out, void* ctx) {
  if (!b || n < 2 || (b[0] & 0xC0) != 0x80) return;
  have_ = 0;
  bool afterTs = false;
  for (int i = 1; i < n; ++i) {
    const uint8_t c = b[i];
    if (c & 0x80) {
      if (!afterTs) {  // timestamp
        afterTs = true;
        continue;
      }
      afterTs = false;
      if (c >= 0xF8) {  // real-time: only with its own timestamp, anywhere in a message or SysEx
        out(MidiMsg{c, 0, 0}, ctx);
      } else if (c == 0xF0) {
        sysex_ = true;
        cancelRunning();
      } else if (c >= 0xF1) {  // SysEx end, system common F1..F6: not emitted, cancel running status
        sysex_ = false;
        cancelRunning();
      } else {
        sysex_ = false;
        status_ = c;
        have_ = 0;
      }
      continue;
    }
    afterTs = false;
    if (sysex_ || !status_) continue;  // SysEx body (also a continuation packet) or no status
    data_[have_++] = c;
    const int len = midiDataLen(status_);
    if (have_ == len) {
      out(MidiMsg{status_, data_[0], static_cast<uint8_t>(len > 1 ? data_[1] : 0)}, ctx);
      have_ = 0;
    }
  }
}

namespace {

bool packable(const TimedMsg& m) {
  const uint8_t s = m.b[0];
  if (s >= 0xF8) return m.len == 1;
  if (s < 0x80 || s >= 0xF0 || m.len != 1 + midiDataLen(s)) return false;
  for (int i = 1; i < m.len; ++i)
    if (m.b[i] & 0x80) return false;
  return true;
}

}  // namespace

int blePack(const TimedMsg* in, int n, uint8_t* out, int cap, int& taken) {
  taken = 0;
  if (!in || !out || n <= 0 || cap < 3) return 0;
  int w = 0;
  uint8_t status = 0;  // last channel status, 0 = none
  uint32_t prevMs = 0;
  for (; taken < n; ++taken) {
    const TimedMsg& m = in[taken];
    if (!packable(m)) continue;
    // Low 7 bits can only say "up to 127 ms later": a bigger gap or an earlier time goes in the
    // next packet with its own header.
    if (w && static_cast<uint32_t>(m.ms - prevMs) > 127) break;
    const bool running = m.b[0] < 0xF0 && m.b[0] == status;
    const int need = (w ? 0 : 1) + 1 + (running ? m.len - 1 : m.len);
    if (w + need > cap) break;
    if (!w) out[w++] = static_cast<uint8_t>(0x80 | ((m.ms >> 7) & 0x3F));
    out[w++] = static_cast<uint8_t>(0x80 | (m.ms & 0x7F));
    for (int i = running ? 1 : 0; i < m.len; ++i) out[w++] = m.b[i];
    status = m.b[0] >= 0xF8 ? 0 : m.b[0];
    prevMs = m.ms;
  }
  return w;
}

}  // namespace mt
