#include "audio.h"
#include <Arduino.h>
#include <string.h>
#include "link/link.h"
#include "link_msg.h"
#include "model.h"
#include "state_mirror.h"
#include "synth_model.h"

using namespace mt::link;

namespace audio {

namespace {

constexpr uint32_t kReplyMs = 500;  // no Ack / Nack for a StateSet this long: send the chunk again
constexpr uint32_t kBlockUs = kBlock * 1000000u / kRate;

mt::Project* project = nullptr;
StateMirror mirror;
uint32_t seenBoot = 0;

// The chunk in flight (one at a time) and its bytes as sent, for StateMirror::acked.
int pending = -1;
uint32_t pendingAt = 0;
uint8_t sent[6144];  // the synth board buffers a chunk in as much

// Device state the synth board forgets on a reboot: sent again with the sound state.
bool meters = false;
uint8_t phones = 100;

// Status figures, split between the readers (CPU readout, scope peak, meters) so each takes its own.
struct Acc {
  uint32_t frames = 0, cpuSum = 0, cpuPeak = 0, stalls = 0;
  uint16_t outPeak = 0;
  uint8_t trackPeak[16] = {};
} load, peak, tracks;

void absorb() {
  const slink::StatusAgg a = slink::takeStatus();
  if (!a.frames) return;
  load.frames += a.frames;
  load.cpuSum += a.cpuSum;
  if (a.cpuPeak > load.cpuPeak) load.cpuPeak = a.cpuPeak;
  load.stalls += a.stalls;
  if (a.outPeak > peak.outPeak) peak.outPeak = a.outPeak;
  for (int t = 0; t < 16; ++t)
    if (a.trackPeak[t] > tracks.trackPeak[t]) tracks.trackPeak[t] = a.trackPeak[t];
}

void sendDevice() {
  slink::send(Msg::Meters, Byte{static_cast<uint8_t>(meters ? 1 : 0)});
  slink::send(Msg::Phones, Byte{phones});
}

// Sends chunk id as StateSet pieces; false when the bulk queue stayed full.
bool sendChunk(uint16_t id) {
  uint32_t off, len;
  if (!mt::chunkRange(id, off, len) || len > sizeof sent) return false;
  memcpy(sent, reinterpret_cast<const uint8_t*>(static_cast<const mt::SynthModel*>(project)) + off, len);
  static StateSet s;
  s.chunk = id;
  s.total = static_cast<uint16_t>(len);
  for (uint32_t at = 0; at < len; at += StateSet::kMaxData) {
    s.offset = static_cast<uint16_t>(at);
    s.len = static_cast<uint16_t>(len - at < StateSet::kMaxData ? len - at : StateSet::kMaxData);
    memcpy(s.data, sent + at, s.len);
    if (!slink::send(Msg::StateSet, s, pdMS_TO_TICKS(50))) return false;
  }
  return true;
}

}  // namespace

void begin(mt::Project* p) {
  project = p;
  slink::begin();
}

void pump() {
  if (!project) return;
  const uint32_t boot = slink::bootId();
  if (boot != seenBoot) {
    seenBoot = boot;
    mirror.invalidate();
    pending = -1;
    if (boot) sendDevice();
  }
  slink::Reply r;
  while (slink::takeReply(r)) {
    if (pending < 0 || r.id != pending) continue;  // a reply from before a reboot / timeout
    if (r.ok) mirror.acked(r.id, sent);
    else mirror.failed(r.id);
    pending = -1;
  }
  const uint32_t now = millis();
  if (pending >= 0 && now - pendingAt >= kReplyMs) {
    mirror.failed(static_cast<uint16_t>(pending));
    pending = -1;
  }
  if (pending >= 0 || !slink::synthUp() || !slink::versionOk()) return;
  const int id = mirror.nextDirty(*project);
  if (id < 0) return;
  if (!sendChunk(static_cast<uint16_t>(id))) {
    mirror.failed(static_cast<uint16_t>(id));
    return;
  }
  pending = id;
  pendingAt = now;
}

bool synced() { return project && pending < 0 && mirror.synced(*project); }

const mt::Project* mirrorProject() { return project; }

bool synthUp() { return slink::synthUp(); }

bool versionOk() { return slink::versionOk(); }

const char* synthFw() { return slink::synthFw(); }

uint16_t synthProtocol() { return slink::synthProtocol(); }

LinkCounters linkCounters() {
  const slink::Counters c = slink::counters();
  return {c.lost, c.late, c.crcErrors, c.retries, c.dropped};
}

static_assert(kProfStages == ProfileRep::kStages, "profile stages");

namespace {
bool profiling = false;

// One Profile request (on: start, else stop); false without a reply.
bool profileCall(uint8_t on, ProfileRep& rep) {
  uint8_t p[4];
  Writer w(p, sizeof p);
  encode(Byte{on}, w);
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!slink::request(Msg::Profile, p, w.size(), Msg::Profile, reply, len)) return false;
  Reader r(reply, len);
  return decode(r, rep);
}
}  // namespace

void profileStart() {
  static ProfileRep rep;  // UI task only
  profiling = slink::synthUp() && profileCall(1, rep);
}

bool profileRunning() { return profiling; }

bool profileStop(Profile& out) {
  if (!profiling) return false;
  profiling = false;
  static ProfileRep rep;  // UI task only
  if (!profileCall(0, rep) || rep.blocks == 0 || rep.cyclesPerUs == 0) return false;
  out.blocks = rep.blocks;
  const double div = static_cast<double>(rep.cyclesPerUs) * rep.blocks;
  for (int i = 0; i < kProfStages; ++i) out.us[i] = static_cast<float>(rep.cycles[i] / div);
  return true;
}

void post(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
  if (len == 0 || len > 3) return;
  slink::postEvent(t, track, b, len);
}

void preview(uint8_t instr, uint8_t note) {
  pump();  // an edit just made goes out ahead of the preview
  slink::send(Msg::PreviewNote, PreviewNote{static_cast<uint8_t>(instr & 15), static_cast<uint8_t>(note & 127), 0});
}

void previewSlice(uint8_t instr, uint8_t slice, uint8_t root, uint32_t holdMs) {
  pump();
  slink::send(Msg::PreviewSlice, PreviewSlice{static_cast<uint8_t>(instr & 15), slice, static_cast<uint8_t>(root & 127),
                                              holdMs > 25000 ? 25000 : holdMs});
}

Load takeLoad() {
  absorb();
  Load l{0, 0, 0, 0};
  if (load.frames) {
    // One Status per window: its average load stands for that window's blocks.
    l.blocks = load.frames;
    l.sumUs = load.cpuSum * kBlockUs / 100;
    l.peakUs = load.cpuPeak * kBlockUs / 100;
    l.stalls = load.stalls;
  }
  load = Acc();
  return l;
}

void scopeRead(int16_t* out, int n) {
  if (n > kScopeLen) n = kScopeLen;
  int8_t s[Status::kScopeMax];
  const int m = slink::scope(s, (n + 1) / 2);
  // Newest last; each received sample stands for two output samples.
  for (int i = 0; i < n; ++i) {
    const int j = m - 1 - (n - 1 - i) / 2;
    out[i] = j >= 0 ? static_cast<int16_t>(s[j] * 256) : 0;
  }
}

int16_t scopePeak() {
  absorb();
  const uint16_t pk = peak.outPeak;
  peak.outPeak = 0;
  return static_cast<int16_t>(pk > 32767 ? 32767 : pk);
}

void trackPeaks(float out[16]) {
  absorb();
  for (int t = 0; t < 16; ++t) {
    out[t] = tracks.trackPeak[t] * 0.01f;
    tracks.trackPeak[t] = 0;
  }
}

void setMeters(bool on) {
  meters = on;
  slink::send(Msg::Meters, Byte{static_cast<uint8_t>(on ? 1 : 0)});
}

void setPhones(uint8_t pct) {
  phones = pct > 100 ? 100 : pct;
  slink::send(Msg::Phones, Byte{phones});
}

void pollLog() { slink::pollLog(); }

}  // namespace audio
