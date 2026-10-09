#include "link_server.h"
#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "clock.h"
#include "fs_server.h"
#include "fw_update.h"
#include "link_fs.h"
#include "link_msg.h"
#include "preview_stream.h"
#include "render_server.h"
#include "sample_tools.h"
#include "synth.h"
#include "synth_model.h"
#include "model.h"
#include "time_sync.h"

using namespace mt::link;

namespace link {

namespace {

#ifdef LINK_DESK
usb_serial_class& port = Serial;
#else
HardwareSerialIMXRT& port = Serial1;
DMAMEM uint8_t rxMem[8192];
DMAMEM uint8_t txMem[8192];
#endif

mt::SynthModel* model = nullptr;
mt::Synth* synth = nullptr;
SynthStream* out = nullptr;

Decoder dec;
TimeSync clock;
uint8_t txSeq = 0;
uint32_t bootId = 0;
uint32_t lastStatus = 0;
uint32_t lostTotal = 0;  // events lost on this side since boot (queue full)

// Preview track: the sounding preview note and when it ends (Teensy time), 0 = none.
uint8_t previewNote = 0;
uint64_t previewOffUs = 0;
constexpr uint32_t kPreviewMs = 300;

// StateSet pieces of the chunk being received; applied on the last piece.
uint8_t scratch[6144];
int scratchChunk = -1;
uint32_t scratchNext = 0;

template <class M>
void send(Msg t, const M& m) {
  uint8_t buf[kMaxEncoded];
  const int n = frame(t, txSeq++, m, buf);
  if (n > 0) port.write(buf, n);
}

void sendHello() {
  Hello h;
#ifdef DTRK_REV
  strncpy(h.fw, DTRK_REV, sizeof h.fw - 1);
#else
  strncpy(h.fw, "dev", sizeof h.fw - 1);
#endif
  h.bootId = bootId;
  h.modelSize = sizeof(mt::SynthModel);
  send(Msg::Hello, h);
}

void onHello(Reader& r) {
  Hello h;
  if (!decode(r, h)) return;
  clock.reset();  // the ESP may have rebooted: its clock restarted
  card::reset();  // and with it the files it had open
  preview::stop();
  render::reset();  // a render it was feeding is over
  bank::reset();
  fw::reset();
  if (h.modelSize && h.modelSize != sizeof(mt::SynthModel))
    log("model size mismatch: esp %lu, synth %u", static_cast<unsigned long>(h.modelSize), sizeof(mt::SynthModel));
  sendHello();
}

void onTime(Reader& r) {
  Time t;
  if (decode(r, t)) clock.sample(t.tUs, micros64());
}

void onEv(Reader& r) {
  EvBatch b;
  if (!decode(r, b) || !clock.valid()) return;
  for (int i = 0; i < b.n; ++i) {
    const Ev& e = b.ev[i];
    QEv q{clock.toLocal(e.tUs) + kPlayLatencyUs, e.track, e.len, {e.b[0], e.b[1], e.b[2]}};
    if (!out->queue().push(q)) lostTotal++;
  }
}

void onStateSet(Reader& r) {
  static StateSet s;
  if (!decode(r, s)) return;
  uint32_t off, len;
  if (!mt::chunkRange(s.chunk, off, len) || s.total != len || len > sizeof scratch ||
      static_cast<uint32_t>(s.offset) + s.len > len) {
    send(Msg::Nack, Nack{s.chunk, -1});
    return;
  }
  if (s.offset == 0) {
    scratchChunk = s.chunk;
    scratchNext = 0;
  }
  if (scratchChunk != s.chunk || s.offset != scratchNext) {  // a piece went missing
    scratchChunk = -1;
    send(Msg::Nack, Nack{s.chunk, -2});
    return;
  }
  memcpy(scratch + s.offset, s.data, s.len);
  scratchNext += s.len;
  if (scratchNext < len) return;
#ifndef AUDIO_BENCH_POOL  // the pool bench keeps its own sounds (bench.cpp)
  __disable_irq();
  memcpy(reinterpret_cast<uint8_t*>(model) + off, scratch, len);
  __enable_irq();
#endif
  scratchChunk = -1;
  send(Msg::Ack, Ack{s.chunk});
}

bool pushNow(uint64_t at, uint8_t len, uint8_t b0, uint8_t b1, uint8_t b2) {
  QEv q{at, mt::kPreviewTrack, len, {b0, b1, b2}};
  if (out->queue().push(q)) return true;
  lostTotal++;
  return false;
}

// Like the old audio task: a new preview ends the previous one, then program, (slice), note-on.
void startPreview(uint8_t instr, int slice, uint8_t note, uint32_t holdMs) {
  const uint64_t at = micros64() + kPlayLatencyUs;
  if (previewOffUs) pushNow(at, 3, 0x80, previewNote, 0);
  pushNow(at, 2, 0xC0, instr, 0);
  if (slice >= 0) pushNow(at, 3, 0xF5, static_cast<uint8_t>(mt::Fx::SLC), static_cast<uint8_t>(slice));
  pushNow(at, 3, 0x90, static_cast<uint8_t>(note & 127), 100);
  previewNote = note & 127;
  previewOffUs = at + 1000ull * (holdMs ? holdMs : kPreviewMs);
}

void onPreviewNote(Reader& r) {
  PreviewNote m;
  if (decode(r, m)) startPreview(m.instr, -1, m.note, m.holdMs);
}

void onPreviewSlice(Reader& r) {
  PreviewSlice m;
  if (decode(r, m)) startPreview(m.instr, m.slice, m.root, m.holdMs > 25000 ? 25000 : m.holdMs);
}

void onMeters(Reader& r) {
  Byte m;
  if (!decode(r, m)) return;
  synth->setMeters(m.v != 0);
  out->setScope(m.v != 0);
}

void onPhones(Reader& r) {
  Byte m;
  if (decode(r, m)) out->setPhones(m.v);
}

static_assert(ProfileRep::kStages == mt::Synth::kProfStages, "profile stages");

uint32_t cycleClock() { return ARM_DWT_CYCCNT; }

// The synth's 32-bit sums wrap within seconds at 600 MHz: loop() folds them in here while it runs.
bool profiling = false;
uint64_t profCycles[mt::Synth::kProfStages] = {};
uint32_t profBlocks = 0;

void foldProfile() {
  uint32_t c[mt::Synth::kProfStages], blocks;
  __disable_irq();  // the audio interrupt adds to the sums
  synth->takeProfile(c, blocks);
  __enable_irq();
  for (int i = 0; i < mt::Synth::kProfStages; ++i) profCycles[i] += c[i];
  profBlocks += blocks;
}

// Profile: Byte 1 starts (sums cleared), 0 stops; the reply carries the sums either way.
void onProfile(Reader& r) {
  Byte m;
  if (!decode(r, m)) return;
  foldProfile();
  ProfileRep rep;
  for (int i = 0; i < mt::Synth::kProfStages; ++i) rep.cycles[i] = profCycles[i];
  rep.blocks = profBlocks;
  rep.running = m.v ? 1 : 0;
  rep.cyclesPerUs = F_CPU_ACTUAL / 1000000;
  memset(profCycles, 0, sizeof profCycles);
  profBlocks = 0;
  profiling = m.v != 0;
  synth->setProfiler(profiling ? cycleClock : nullptr);
  reply(Msg::Profile, dec.seq(), rep);
}

void dispatch() {
  Reader r(dec.payload(), dec.size());
  switch (static_cast<Msg>(dec.type())) {
    case Msg::Hello: onHello(r); break;
    case Msg::Time: onTime(r); break;
    case Msg::Ev: onEv(r); break;
    case Msg::StateSet: onStateSet(r); break;
    case Msg::PreviewNote: onPreviewNote(r); break;
    case Msg::PreviewSlice: onPreviewSlice(r); break;
    case Msg::PreviewStop: preview::stop(); break;
    case Msg::Meters: onMeters(r); break;
    case Msg::Phones: onPhones(r); break;
    case Msg::Profile: onProfile(r); break;
    default: {
      static uint8_t out[kMaxPayload];
      const Msg t = static_cast<Msg>(dec.type());
      int n = -1;
      if (isFsRequest(t)) n = card::handle(t, dec.seq(), dec.payload(), dec.size(), out);
      else if (bank::isRequest(t)) n = bank::handle(t, dec.seq(), dec.payload(), dec.size(), out);
      else if (tools::isRequest(t)) n = tools::handle(t, dec.payload(), dec.size(), out);
      else if (preview::isRequest(t)) n = preview::handle(t, dec.payload(), dec.size(), out);
      else if (render::isRequest(t)) n = render::handle(t, dec.seq(), dec.payload(), dec.size(), out);
      else if (fw::isRequest(t)) n = fw::handle(t, dec.seq(), dec.payload(), dec.size(), out);
      if (n >= 0) reply(t, dec.seq(), out, n);  // a bank job replies when it is done
      break;
    }
  }
}

void sendStatus() {
  if (profiling) foldProfile();
  const SynthStream::Stats st = out->take();
  const uint32_t kBlockCycles = SynthStream::blockCycles();
  Status s;
  s.cpuPct = st.blocks ? static_cast<uint8_t>(min<uint32_t>(255, 100ull * st.cycles / (static_cast<uint64_t>(st.blocks) * kBlockCycles))) : 0;
  s.stalls = st.stalls;
  s.late = st.late;
  s.voices = static_cast<uint8_t>(synth->activeVoices());
  s.outPeak = st.outPeak;
  float pk[mt::kTracks];
  synth->takeTrackPeaks(pk);
  for (int t = 0; t < mt::kTracks; ++t) s.trackPeak[t] = static_cast<uint8_t>(min(255.f, pk[t] * 100.f));
  s.lost = static_cast<uint16_t>(st.lost + lostTotal);
  lostTotal = 0;
  s.scopeN = static_cast<uint16_t>(out->scopeRead(s.scope, Status::kScopeMax));
  send(Msg::Status, s);
}

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& o) {
  model = &m;
  synth = &s;
  out = &o;
  bootId = ARM_DWT_CYCCNT ^ (micros() << 12);
#ifndef LINK_DESK
  port.addMemoryForRead(rxMem, sizeof rxMem);
  port.addMemoryForWrite(txMem, sizeof txMem);
  port.begin(kBaud);
#endif
  sendHello();
}

// A reply carries the seq of its request (the ESP matches them by it).
void reply(Msg t, uint8_t seq, const uint8_t* p, int n) {
  uint8_t buf[kMaxEncoded];
  const int k = encode(static_cast<uint8_t>(t), seq, p, n, buf);
  if (k > 0) port.write(buf, k);
}

void flushOut() { port.flush(); }

void statusIfDue() {
  const uint32_t now = millis();
  if (now - lastStatus >= kStatusMs) {
    lastStatus = now;
    sendStatus();
  }
}

void poll() {
  for (int n = port.available(); n > 0; --n) {
    if (dec.feed(static_cast<uint8_t>(port.read()))) dispatch();
  }
  if (previewOffUs && micros64() + kPlayLatencyUs >= previewOffUs) {
    pushNow(previewOffUs, 3, 0x80, previewNote, 0);
    previewOffUs = 0;
  }
  statusIfDue();
}

void log(const char* fmt, ...) {
  Log m;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m.text, sizeof m.text, fmt, ap);
  va_end(ap);
  send(Msg::Log, m);
#ifndef LINK_DESK
  Serial.println(m.text);
#endif
}

}  // namespace link
