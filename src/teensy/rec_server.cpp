#include "rec_server.h"
#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "codec.h"
#include "fs_server.h"
#include "hot.h"
#include "link_server.h"
#include "psram.h"
#include "rec.h"
#include "render_server.h"
#include "wav.h"

using namespace mt::link;

namespace rec {

namespace {

constexpr uint32_t kChunkFrames = 2048;  // ring -> file per write (8 KB)
constexpr uint32_t kStepUs = 2000;       // loop() time a pass may take
constexpr uint32_t kProgressMs = 100;
constexpr uint32_t kMemLogMs = 10000;    // AudioMemoryUsageMax while a window is open (temporary)

// None: no take. Take: armed / recording, the ring drained into the file (the capture may have
// ended on its own: maximum length or a full ring). Sealing: RecStop came, the rest of the ring goes
// to the file, then the header. Importing: the sealed file into the store.
enum class Phase : uint8_t { None, Take, Sealing, Importing };

SynthStream* out = nullptr;
mt::RecCapture cap;
bool ringOk = false;
Phase phase = Phase::None;
bool watching = false;  // the window's meter (out->setRec) runs
bool keepOnly = false;  // REC -> SD (RecStartReq::toSd): the take is the file, never the store
bool spaceBound = false;  // keepOnly: the card's free space, not the 4 GB, sets the take's maximum
uint32_t maxFrames = 0;   // the take's maximum (RecLevelRep::left)
uint32_t lastLevelMs = 0, lastProgress = 0, lastMemLog = 0;

FsFile file;
mt::RecWriter writer;
int number = 0;
char path[mt::kRecPathMax] = {}, tmp[mt::kRecPathMax] = {};
uint16_t takePeak = 0;
uint32_t takeClips = 0;
MT_RAM2 int16_t buf[kChunkFrames * 2];

// RecStop: its seq, its reply once sent (again for a retry with the same seq).
uint8_t stopSeq = 0;
bool stopPending = false, haveStopRep = false;
int stopRepLen = 0;
uint8_t stopRep[32];  // a RecStopRep is 21 bytes
RecStopRep stopRes;

template <class M>
int put(const M& m, uint8_t* p) {
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? w.size() : 0;
}

bool sink(const void* d, uint32_t n, void* ctx) { return static_cast<FsFile*>(ctx)->write(d, n) == n; }

bool inputSource(mt::RecSource s) { return s != mt::RecSource::Resample; }

// The codec on the source's input at gain (MIC dB / LINE level); RESAMPLE leaves it as it is.
void setInput(mt::RecSource s, int gain) {
  if (s == mt::RecSource::Mic) {
    codec::input(codec::Input::Mic);
    codec::micGain(gain);
  } else if (s == mt::RecSource::Line) {
    codec::input(codec::Input::Line);
    codec::lineLevel(gain);
  }
}

// The audio ISR (SynthStream::update) reads cap's source, monitor_ and rec_ together: they change
// as one, with the ISR held off, so a block never sees a half-switched source/monitor pair.
// cap itself is init'ed in begin() before it is ever published; arm/stop go through the same
// guard; the meter and ring are atomics (SPSC: ISR writes, loop reads).
void watch(mt::RecSource s, bool mon) {
  AudioNoInterrupts();
  if (phase == Phase::None) cap.watch(s);
  out->setMonitor(mon && inputSource(s));
  out->setRec(&cap);
  AudioInterrupts();
  if (!watching) lastMemLog = millis();
  watching = true;
}

void unwatch() {
  AudioNoInterrupts();
  out->setMonitor(false);
  out->setRec(nullptr);
  AudioInterrupts();
  watching = false;
}

// The take's meter so far, into the take's totals.
void takeMeter(uint16_t& peak, uint32_t& clips) {
  cap.meter().take(peak, clips);
  if (phase != Phase::None) {
    if (peak > takePeak) takePeak = peak;
    takeClips += clips;
  }
}

mt::RecState shownState() {
  switch (phase) {
    case Phase::None: return mt::RecState::Idle;
    case Phase::Take: {
      const mt::RecState s = cap.state();
      return s == mt::RecState::Idle ? mt::RecState::Saving : s;  // ended on its own: waiting for RecStop
    }
    default: return mt::RecState::Saving;
  }
}

int levelRep(RecResult r, uint8_t* o) {
  RecLevelRep m;
  m.result = static_cast<uint8_t>(r);
  if (r == RecResult::Ok && phase != Phase::None) {
    if (writer.failed()) m.result = static_cast<uint8_t>(RecResult::WriteFail);
    else if (cap.overflow()) m.result = static_cast<uint8_t>(RecResult::CardSlow);
  }
  m.state = static_cast<uint8_t>(shownState());
  takeMeter(m.peak, m.clips);
  m.frames = phase != Phase::None ? cap.frames() : 0;
  m.codec = codec::present() ? 1 : 0;
  if (phase == Phase::Take) m.left = maxFrames > m.frames ? maxFrames - m.frames : 0;
  return put(m, o);
}

// The ring into the file for up to kStepUs; false when a write failed.
bool drain() {
  const uint32_t t0 = micros();
  do {
    const uint32_t n = cap.ring().pop(buf, kChunkFrames);
    if (!n) break;
    if (!writer.write(buf, n)) return false;
  } while (micros() - t0 < kStepUs);
  return true;
}

bool drainAll() {
  while (cap.ring().size())
    if (!drain()) return false;
  return true;
}

void dropFile() {
  if (file) file.close();
  SD.sdfs.remove(tmp);
  card::spaceChanged();
}

// The ring is empty and the capture idle: header, rename. Ok, TooShort (nothing kept) or WriteFail.
RecResult seal() {
  const uint32_t frames = writer.frames();
  if (!writer.failed() && frames < mt::kRecMinFrames) {
    dropFile();
    return RecResult::TooShort;
  }
  uint8_t hdr[mt::kWavHeaderBytes];
  bool ok = writer.finish(hdr);
  const uint64_t size = mt::kWavHeaderBytes + static_cast<uint64_t>(frames) * cap.channels() * 2;
  ok = ok && file.truncate(size) && file.seekSet(0) && file.write(hdr, sizeof hdr) == sizeof hdr;
  ok = file.close() && ok;
  card::spaceChanged();
  if (!ok) return RecResult::WriteFail;  // the .tmp stays for a look on a computer
  if (SD.sdfs.exists(path)) SD.sdfs.remove(path);
  return SD.sdfs.rename(tmp, path) ? RecResult::Ok : RecResult::WriteFail;
}

void sendStop() {
  stopRes.number = static_cast<uint16_t>(number);
  stopRes.peak = takePeak;
  stopRes.clips = takeClips;
  Writer w(stopRep, sizeof stopRep);
  encode(stopRes, w);
  stopRepLen = w.ok() ? w.size() : 0;
  haveStopRep = true;
  stopPending = false;
  link::reply(Msg::RecStop, stopSeq, stopRep, stopRepLen);
}

void stopCapture() {
  AudioNoInterrupts();
  cap.stop();
  AudioInterrupts();
}

// The take sealed into its file, no store (the ESP is gone or restarted).
void keep(const char* why) {
  stopCapture();
  const RecResult r = writer.failed() || !drainAll() ? RecResult::WriteFail : seal();
  // seal() closes on all its paths (file is then false); this closes only when seal() was skipped.
  if (r == RecResult::WriteFail && file) {
    file.close();
    card::spaceChanged();
  }
  if (r == RecResult::Ok) link::log("rec: %s, kept REC%d.wav", why, number);
  else link::log("rec: %s, REC%d not kept (%s)", why, number, r == RecResult::TooShort ? "too short" : "card write failed");
  phase = Phase::None;
  stopPending = false;
}

// The card's free bytes; false when the volume does not tell. FAT32 counts its whole FAT the first
// time (SdFat keeps the count up to date after that, MAINTAIN_FREE_CLUSTER_COUNT); exFAT reads its
// bitmap each time (a few hundred sectors on a big card): once per take, at RecStart.
bool freeBytes(uint64_t& out) {
  const uint64_t cl = SD.sdfs.bytesPerCluster();
  const int32_t freeCl = static_cast<int32_t>(SD.sdfs.freeClusterCount());
  if (cl == 0 || freeCl < 0) return false;
  out = cl * static_cast<uint32_t>(freeCl);
  return true;
}

RecResult start(Reader& r, uint16_t& n) {
  RecStartReq q;
  if (!decode(r, q) || q.source > static_cast<uint8_t>(mt::RecSource::Resample) || q.thresh > 0)
    return RecResult::BadArg;
  if (phase != Phase::None) return RecResult::Busy;
  const mt::RecSource src = static_cast<mt::RecSource>(q.source);
  const mt::BankResult w = bank::writable();
  if (w == mt::BankResult::NoSd) return RecResult::NoSd;
  if (w != mt::BankResult::Ok || render::active()) return RecResult::Busy;
  if (inputSource(src) && !codec::present()) return RecResult::NoCodec;
  if (!ringOk || !psram::recRing()) return RecResult::NoPsram;
  if (!SD.sdfs.exists("/samples/rec") && !SD.sdfs.mkdir("/samples/rec", true)) return RecResult::WriteFail;
  const uint8_t ch = src == mt::RecSource::Mic ? 1 : 2;
  uint32_t limit = mt::kRecMaxFrames;
  bool bySpace = false;
  if (q.toSd) {
    uint64_t fb = 0;
    if (!freeBytes(fb)) return RecResult::WriteFail;
    limit = mt::recSdLimitFrames(fb, ch);
    bySpace = limit < mt::recSdLimitFrames(~0ull, ch);
    if (limit < mt::kRecRate) return RecResult::CardFull;
  }
  // The first free number from q.number on, wrapping to 1: at most kRecNumberMax tries.
  const int from = q.number < 1 || q.number > mt::kRecNumberMax ? 1 : q.number;
  int k = 0;
  for (int i = 0; i < mt::kRecNumberMax && !k; ++i) {
    const int c = (from - 1 + i) % mt::kRecNumberMax + 1;
    mt::recPath(c, path);
    mt::recTmpPath(c, tmp);
    if (!SD.sdfs.exists(path) && !SD.sdfs.exists(tmp)) k = c;
  }
  if (!k) return RecResult::WriteFail;
  file = SD.sdfs.open(tmp, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) return RecResult::WriteFail;
  const uint8_t zeros[mt::kWavHeaderBytes] = {};
  if (file.write(zeros, sizeof zeros) != sizeof zeros) {
    dropFile();
    return RecResult::WriteFail;
  }
  // A hint (seal truncates); an SD take past 5 min grows from there.
  const uint32_t pre = limit < mt::kRecMaxFrames ? limit : mt::kRecMaxFrames;
  file.preAllocate(mt::kWavHeaderBytes + static_cast<uint64_t>(pre) * ch * 2);
  number = k;
  n = static_cast<uint16_t>(k);
  if (inputSource(src)) setInput(src, q.gain);
  writer.begin(ch, sink, &file);
  takePeak = 0;
  takeClips = 0;
  keepOnly = q.toSd != 0;
  spaceBound = bySpace;
  maxFrames = limit;
  AudioNoInterrupts();
  cap.arm(src, q.thresh, limit);
  AudioInterrupts();
  uint16_t pk;
  uint32_t cl;
  cap.meter().take(pk, cl);  // the take's meter starts now
  phase = Phase::Take;
  haveStopRep = false;
  watch(src, q.mon);
  link::log("rec: REC%d armed (%s%s, max %lu s)", k,
            src == mt::RecSource::Mic ? "MIC" : src == mt::RecSource::Line ? "LINE" : "RESAMPLE",
            keepOnly ? ", to SD" : "", static_cast<unsigned long>(limit / mt::kRecRate));
  return RecResult::Ok;
}

int set(Reader& r, uint8_t* o) {
  RecSetReq q;
  if (!decode(r, q) || q.source > static_cast<uint8_t>(mt::RecSource::Resample)) return levelRep(RecResult::BadArg, o);
  const mt::RecSource src = static_cast<mt::RecSource>(q.source);
  if (phase != Phase::None && src != cap.source()) return levelRep(RecResult::BadArg, o);
  if (inputSource(src) && !codec::present()) {
    unwatch();
    return levelRep(RecResult::NoCodec, o);
  }
  if (inputSource(src)) setInput(src, q.gain);
  watch(src, q.mon);
  return levelRep(RecResult::Ok, o);
}

int cancel(uint8_t* o) {
  if (phase == Phase::Sealing || phase == Phase::Importing) return levelRep(RecResult::Busy, o);
  if (phase == Phase::Take) {
    stopCapture();
    dropFile();
    phase = Phase::None;
    link::log("rec: REC%d cancelled", number);
  }
  unwatch();
  return levelRep(RecResult::Ok, o);
}

}  // namespace

void begin(SynthStream& o) {
  out = &o;
  if (int16_t* m = psram::recRing()) {
    cap.init(m, psram::recRingFrames());
    ringOk = true;
  }
}

bool isRequest(Msg t) {
  return t == Msg::RecStart || t == Msg::RecSet || t == Msg::RecLevel || t == Msg::RecStop || t == Msg::RecCancel;
}

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* o) {
  Reader r(p, n);
  lastLevelMs = millis();
  switch (t) {
    case Msg::RecStart: {
      RecStartRep m;
      m.result = static_cast<uint8_t>(start(r, m.number));
      return put(m, o);
    }
    case Msg::RecSet: return set(r, o);
    case Msg::RecLevel: return levelRep(RecResult::Ok, o);
    case Msg::RecCancel: return cancel(o);
    case Msg::RecStop:
      if (seq == stopSeq && (stopPending || haveStopRep)) {  // a retry: the reply comes / came
        if (stopPending) return -1;
        memcpy(o, stopRep, stopRepLen);
        return stopRepLen;
      }
      if (phase != Phase::Take) {
        RecStopRep m;
        m.result = static_cast<uint8_t>(phase == Phase::None ? RecResult::NotRec : RecResult::Busy);
        return put(m, o);
      }
      stopRes = RecStopRep{};
      if (cap.autoStopped())  // ended on its own before this RecStop
        stopRes.ended = static_cast<uint8_t>(keepOnly && spaceBound ? RecEnd::Space : RecEnd::Length);
      stopCapture();
      stopSeq = seq;
      stopPending = true;
      haveStopRep = false;
      stopRes.result = static_cast<uint8_t>(cap.overflow() ? RecResult::CardSlow : RecResult::Ok);
      phase = Phase::Sealing;
      lastProgress = millis();
      return -1;
    default: return -1;
  }
}

void reset() {
  if (phase == Phase::Take || phase == Phase::Sealing) keep("ESP restarted");
  stopPending = false;  // an import goes on, its reply has no one to go to
  haveStopRep = false;
  unwatch();
}

bool busy() { return phase != Phase::None; }

void step() {
  const uint32_t now = millis();
  if (watching && now - lastMemLog >= kMemLogMs) {  // temporary: AudioMemory sizing with the input on
    lastMemLog = now;
    link::log("rec: audio blocks max %u of 16", static_cast<unsigned>(AudioMemoryUsageMax()));
  }
  switch (phase) {
    case Phase::None:
      if (watching && now - lastLevelMs > kKeepaliveMs) unwatch();  // the window is gone
      return;
    case Phase::Take:
      if (now - lastLevelMs > kKeepaliveMs) {
        keep("ESP lost");
        unwatch();
        return;
      }
      if (!writer.failed() && !drain()) {
        stopCapture();  // shown as Saving with WriteFail; RecStop answers WriteFail
        link::log("rec: REC%d card write failed", number);
      }
      return;
    case Phase::Sealing: {
      if (!writer.failed() && cap.ring().size()) {
        drain();
        return;
      }
      RecResult res = writer.failed() ? RecResult::WriteFail : seal();
      if (res == RecResult::WriteFail && file) {
        file.close();
        card::spaceChanged();
      }
      if (res == RecResult::Ok && stopRes.result == static_cast<uint8_t>(RecResult::CardSlow)) res = RecResult::CardSlow;
      if (keepOnly) {  // REC -> SD: the file is the take; no bank / crc
        if (res == RecResult::Ok || res == RecResult::CardSlow) stopRes.frames = writer.frames();
        stopRes.result = static_cast<uint8_t>(res);
        phase = Phase::None;
        link::log("rec: REC%d on the card, %lu frames, result %u", number, static_cast<unsigned long>(writer.frames()),
                  static_cast<unsigned>(res));
        if (stopPending) sendStop();
        return;
      }
      if (res == RecResult::Ok || res == RecResult::CardSlow) {
        const mt::BankResult b = bank::importFile(path);
        if (b == mt::BankResult::Running) {
          stopRes.result = static_cast<uint8_t>(res);
          phase = Phase::Importing;
          return;
        }
        stopRes.bank = static_cast<uint8_t>(b);
        res = RecResult::Bank;
      }
      stopRes.result = static_cast<uint8_t>(res);
      phase = Phase::None;
      link::log("rec: REC%d stopped, result %u", number, static_cast<unsigned>(res));
      if (stopPending) sendStop();
      return;
    }
    case Phase::Importing: {
      mt::ImportOut io;
      uint32_t done = 0, total = 0;
      const mt::BankResult b = bank::importPoll(io, done, total);
      if (b == mt::BankResult::Running) {
        if (stopPending && now - lastProgress >= kProgressMs) {
          lastProgress = now;
          Progress pr;
          pr.op = static_cast<uint8_t>(Msg::RecStop);
          pr.done = done;
          pr.total = total;
          link::reply(Msg::Progress, stopSeq, pr);
        }
        return;
      }
      phase = Phase::None;
      if (b == mt::BankResult::Ok) {
        stopRes.crc = io.crc;
        stopRes.frames = io.frames;
        if (io.crc != writer.crc()) link::log("rec: REC%d crc %08lx in the store, %08lx written", number,
                                              static_cast<unsigned long>(io.crc), static_cast<unsigned long>(writer.crc()));
      } else {
        stopRes.bank = static_cast<uint8_t>(b);
        stopRes.result = static_cast<uint8_t>(RecResult::Bank);
      }
      link::log("rec: REC%d in the store, %lu frames, result %u", number, static_cast<unsigned long>(io.frames),
                static_cast<unsigned>(stopRes.result));
      if (stopPending) sendStop();
      return;
    }
  }
}

}  // namespace rec
