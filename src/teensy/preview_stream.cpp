#include "preview_stream.h"
#include <Arduino.h>
#include <SD.h>
#include "audio_out.h"
#include "bank_ops.h"
#include "fs_server.h"
#include "link_server.h"
#include "model.h"
#include "stream_ring.h"
#include "wav.h"

using namespace mt::link;
using mt::BankResult;

namespace preview {

namespace {

class Src final : public mt::ByteSource {
 public:
  FsFile f;
  bool read(void* d, size_t n) override { return f.read(d, n) == static_cast<int>(n); }
  bool skip(size_t n) override { return f.seekCur(static_cast<int64_t>(n)); }
};

constexpr uint32_t kChunk = 512;     // input frames per card read
constexpr uint32_t kRawBytes = 6144;  // up to 512 stereo 24 bit frames (more channels: fewer frames)

mt::SynthModel* model = nullptr;
SynthStream* out = nullptr;
mt::StreamRing ring;
mt::LinearResampler rs;
mt::WavInfo info;
Src src;
uint32_t left = 0;  // input frames still to read
bool streaming = false;
uint8_t raw[kRawBytes];
int16_t mono[kChunk];
int16_t res[kChunk * 6 + 2];  // kPreviewMaxRate up to 6x below the output rate: 8 kHz

uint8_t code(BankResult r) { return static_cast<uint8_t>(r); }

void closeFile() {
  if (src.f) src.f.close();
  left = 0;
}

// Reads one chunk (as much as the ring takes); at the end the ring is told so.
void fill() {
  if (!left) return;
  const uint32_t fb = info.frameBytes();
  uint32_t n = rs.inFor(ring.space());
  if (n > kChunk) n = kChunk;
  if (n > kRawBytes / fb) n = kRawBytes / fb;
  if (n > left) n = left;
  if (!n) return;
  if (!src.read(raw, n * fb)) n = 0;  // a read error ends it
  if (n) {
    mt::wavToMono(raw, n, info, mono);
    ring.write(res, static_cast<uint32_t>(rs.push(mono, static_cast<int>(n), res)));
    left -= n;
  }
  if (!n || !left) {
    closeFile();
    ring.finish();
  }
}

BankResult open(const char* path, uint32_t& frames, uint32_t& rate) {
  if (!card::ready()) return BankResult::NoSd;
  src.f = SD.sdfs.open(path, O_RDONLY);
  if (src.f && src.f.isDir()) src.f.close();
  if (!src.f) return BankResult::OpenFail;
  info = mt::WavInfo();
  const mt::WavErr e = mt::wavParse(src, info);
  if (e != mt::WavErr::Ok) {
    closeFile();
    return e == mt::WavErr::NotWav ? BankResult::NotWav : (e == mt::WavErr::Truncated ? BankResult::Truncated : BankResult::Unsupported);
  }
  const uint32_t fb = info.frameBytes();
  if (!fb || fb > kRawBytes || !info.rate || info.rate > kPreviewMaxRate || info.rate * 6 < AUDIO_SAMPLE_RATE_EXACT) {
    closeFile();
    return BankResult::Unsupported;
  }
  const uint64_t size = src.f.fileSize();
  uint32_t bytes = info.dataBytes;
  if (info.dataOffset >= size) bytes = 0;
  else if (bytes > size - info.dataOffset) bytes = static_cast<uint32_t>(size - info.dataOffset);  // a cut file
  if (!src.f.seekSet(info.dataOffset)) {
    closeFile();
    return BankResult::ReadFail;
  }
  frames = bytes / fb;
  rate = info.rate;
  left = frames;
  rs = mt::LinearResampler(info.rate, static_cast<uint32_t>(AUDIO_SAMPLE_RATE_EXACT + 0.5f));
  return BankResult::Ok;
}

// The stream ended or was stopped: underruns go to the link log.
void ended() {
  if (!streaming) return;
  streaming = false;
  if (const uint32_t u = ring.takeUnderruns()) link::log("preview: %lu underruns", static_cast<unsigned long>(u));
}

}  // namespace

void begin(mt::SynthModel& m, SynthStream& o) {
  model = &m;
  out = &o;
  out->setPreview(&ring);
}

bool isRequest(Msg t) { return t == Msg::PreviewFile; }

int handle(Msg, const uint8_t* p, int n, uint8_t* o) {
  stop();
  PreviewFileRep m;
  Reader r(p, n);
  static PreviewFileReq q;
  if (!decode(r, q)) {
    m.result = code(BankResult::ReadFail);
  } else {
    const BankResult e = open(q.path, m.frames, m.rate);
    m.result = code(e);
    if (e == BankResult::Ok) {
      ring.reset();
      for (int i = 0; i < 64 && left && ring.space() > kChunk * 6 + 2; ++i) fill();  // a head start
      ring.start();
      streaming = true;
    }
  }
  Writer w(o, kMaxPayload);
  encode(m, w);
  return w.ok() ? w.size() : 0;
}

void stop() {
  ring.stop();
  closeFile();
  ended();
}

void step() {
  if (!ring.playing()) {
    closeFile();  // played to its end
    ended();
    return;
  }
  out->setPreviewGain((model->masterVol > mt::kMasterVolMax ? mt::kMasterVolMax : model->masterVol) * 0.01f);
  fill();
}

}  // namespace preview
