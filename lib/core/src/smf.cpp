#include "smf.h"
#include <algorithm>
#include <string.h>

namespace mt {

namespace {

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

enum class Trk : uint8_t { Eot, RanOut, Bad };

// Notes sounding in the current track, oldest first (FIFO for same-pitch overlaps).
constexpr int kMaxOpen = 128;
struct OpenNotes {
  uint32_t idx[kMaxOpen];
  uint16_t key[kMaxOpen];  // channel << 7 | pitch
  int n = 0;
};

// Stable in-place merge of the sorted runs [a, m) and [m, e) by tick. No buffer; the left
// half recurses, the right half loops, so recursion depth is O(log n).
void mergeInPlace(SmfNote* a, SmfNote* m, SmfNote* e) {
  auto lessTick = [](const SmfNote& x, uint32_t t) { return x.tick < t; };
  auto tickLess = [](uint32_t t, const SmfNote& x) { return t < x.tick; };
  while (true) {
    const size_t n1 = static_cast<size_t>(m - a), n2 = static_cast<size_t>(e - m);
    if (n1 == 0 || n2 == 0 || (m - 1)->tick <= m->tick) return;
    if (n1 + n2 == 2) {
      std::swap(*a, *m);
      return;
    }
    SmfNote *c1, *c2;
    if (n1 >= n2) {
      c1 = a + n1 / 2;
      c2 = std::lower_bound(m, e, c1->tick, lessTick);
    } else {
      c2 = m + n2 / 2;
      c1 = std::upper_bound(a, m, c2->tick, tickLess);
    }
    SmfNote* nm = std::rotate(c1, m, c2);
    mergeInPlace(a, c1, nm);
    a = nm;
    m = c2;
  }
}

struct Parser {
  SmfInfo& info;
  SmfNote* notes;
  uint32_t cap;
  bool tooMany = false;
  OpenNotes open;
  uint8_t trackIdx = 0;
  char trackName[17] = {0};

  Parser(SmfInfo& i, SmfNote* n, uint32_t c) : info(i), notes(n), cap(c) {}

  int source(uint8_t ch) {
    for (int i = 0; i < info.sourceCount; ++i)
      if (info.src[i].track == trackIdx && info.src[i].channel == ch) return i;
    if (info.sourceCount >= kSmfMaxSources) return -1;
    SmfSource& s = info.src[info.sourceCount];
    s.track = trackIdx;
    s.channel = ch;
    memcpy(s.name, trackName, sizeof(s.name));
    s.count = 0;
    s.lo = 127;
    s.hi = 0;
    return info.sourceCount++;
  }

  void setName(const uint8_t* p, uint32_t len) {
    if (trackName[0]) return;  // first name wins
    const uint32_t n = len < 16 ? len : 16;
    memcpy(trackName, p, n);
    trackName[n] = 0;
    for (int i = 0; i < info.sourceCount; ++i)
      if (info.src[i].track == trackIdx) memcpy(info.src[i].name, trackName, sizeof(trackName));
  }

  void close(int i, uint32_t tick) {
    SmfNote& n = notes[open.idx[i]];
    n.len = tick - n.tick;
    for (int j = i + 1; j < open.n; ++j) {
      open.idx[j - 1] = open.idx[j];
      open.key[j - 1] = open.key[j];
    }
    --open.n;
  }

  void noteOn(uint32_t tick, uint8_t ch, uint8_t pitch, uint8_t vel) {
    if (info.noteCount >= cap) {
      tooMany = true;
      return;
    }
    const int s = source(ch);
    if (s < 0) return;
    if (open.n == kMaxOpen) close(0, tick);  // polyphony overflow: cut the oldest
    const uint32_t idx = info.noteCount++;
    notes[idx] = SmfNote{tick, 0, pitch, vel, static_cast<uint8_t>(s)};
    open.idx[open.n] = idx;
    open.key[open.n++] = static_cast<uint16_t>(ch << 7 | pitch);
    SmfSource& src = info.src[s];
    if (src.count < 0xFFFF) ++src.count;
    if (pitch < src.lo) src.lo = pitch;
    if (pitch > src.hi) src.hi = pitch;
  }

  void noteOff(uint32_t tick, uint8_t ch, uint8_t pitch) {
    const uint16_t key = static_cast<uint16_t>(ch << 7 | pitch);
    for (int i = 0; i < open.n; ++i)
      if (open.key[i] == key) {
        close(i, tick);
        return;
      }
  }

  // Parses one MTrk body. endTick receives the last tick reached.
  Trk track(const uint8_t* p, const uint8_t* end, uint32_t& endTick) {
    uint32_t tick = 0;
    uint8_t rs = 0;
    Trk res = Trk::RanOut;
    auto vlq = [&](uint32_t& v) -> int {  // 0 ok, 1 ran out, 2 bad
      v = 0;
      for (int i = 0; i < 4; ++i) {
        if (p >= end) return 1;
        const uint8_t b = *p++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return 0;
      }
      return 2;
    };
    while (true) {
      uint32_t delta, len;
      int r = vlq(delta);
      if (r) { res = r == 1 ? Trk::RanOut : Trk::Bad; break; }
      if (delta > UINT32_MAX - tick) { res = Trk::Bad; break; }  // tick would wrap
      tick += delta;
      if (p >= end) break;
      uint8_t st = *p;
      if (st < 0x80) {
        if (!rs) { res = Trk::Bad; break; }
        st = rs;
      } else {
        ++p;
        if (st < 0xF0) rs = st;
      }
      // Meta, sysex and real-time events leave running status alone (lenient: some writers
      // continue running status after them).
      if (st >= 0xF8 && st != 0xFF) {
        continue;  // real-time: single byte
      } else if (st == 0xFF) {
        if (p >= end) break;
        const uint8_t type = *p++;
        r = vlq(len);
        if (r) { res = r == 1 ? Trk::RanOut : Trk::Bad; break; }
        if (len > static_cast<uint32_t>(end - p)) break;
        if (type == 0x2F) { res = Trk::Eot; break; }
        if (type == 0x03) setName(p, len);
        if (type == 0x51 && len == 3 && !tempoSeen) {
          tempoSeen = true;
          info.firstTempoUsPerQ = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }
        p += len;
      } else if (st == 0xF0 || st == 0xF7) {
        r = vlq(len);
        if (r) { res = r == 1 ? Trk::RanOut : Trk::Bad; break; }
        if (len > static_cast<uint32_t>(end - p)) break;
        p += len;
      } else if (st > 0xF0) {
        res = Trk::Bad;
        break;
      } else {
        const uint8_t kind = st & 0xF0, ch = st & 0x0F;
        const int nData = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
        if (end - p < nData) break;
        const uint8_t a = p[0], b = nData == 2 ? p[1] : 0;
        if ((a | b) & 0x80) { res = Trk::Bad; break; }
        p += nData;
        if (kind == 0x90 && b > 0) noteOn(tick, ch, a, b);
        else if (kind == 0x80 || kind == 0x90) noteOff(tick, ch, a);
      }
    }
    while (open.n) close(0, tick);
    endTick = tick;
    return res;
  }

  bool tempoSeen = false;
};

}  // namespace

SmfErr parseSmf(const uint8_t* data, size_t size, SmfInfo& info, SmfNote* notes, uint32_t cap) {
  info = SmfInfo();
  info.firstTempoUsPerQ = 500000;
  if (!data || size < 4 || memcmp(data, "MThd", 4) != 0) return SmfErr::NotMidi;
  if (size < 14) return SmfErr::Truncated;
  const uint32_t hdrLen = be32(data + 4);
  if (hdrLen < 6) return SmfErr::NotMidi;
  const uint16_t format = be16(data + 8), ntrks = be16(data + 10), division = be16(data + 12);
  if (format > 1) return SmfErr::Unsupported;
  if (division & 0x8000) return SmfErr::Smpte;
  if (division == 0) return SmfErr::NotMidi;
  info.ppq = division;
  if (hdrLen > size - 8) return SmfErr::Truncated;

  Parser ps(info, notes, cap);
  size_t pos = 8 + hdrLen;
  uint32_t tracks = 0;
  bool truncated = false;
  while (size - pos >= 8) {
    const uint32_t len = be32(data + pos + 4);
    const size_t body = pos + 8;
    const bool cut = len > size - body;
    if (memcmp(data + pos, "MTrk", 4) == 0) {
      ps.trackIdx = tracks < 255 ? static_cast<uint8_t>(tracks) : 255;
      ps.trackName[0] = 0;
      const uint32_t first = info.noteCount;
      uint32_t endTick = 0;
      const Trk r = ps.track(data + body, cut ? data + size : data + body + len, endTick);
      ++tracks;
      if (endTick > info.lastTick) info.lastTick = endTick;
      mergeInPlace(notes, notes + first, notes + info.noteCount);
      if (r == Trk::Bad) {
        // Keep what was parsed so far (this track included) and stop; nothing usable -> error.
        if (info.noteCount == 0) return SmfErr::NotMidi;
        truncated = true;
        break;
      }
      if (r == Trk::RanOut && cut) truncated = true;
    } else if (cut) {
      truncated = true;
    }
    if (cut) break;
    pos = body + len;
  }
  if (tracks < ntrks) truncated = true;  // also after a bad track
  if (truncated) return SmfErr::Truncated;
  return ps.tooMany ? SmfErr::TooManyNotes : SmfErr::Ok;
}

}  // namespace mt
