#include <FS.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>

#include "brick_runtime.h"
#include "engine_native.h"
#include "link/bank_client.h"
#include "wt_audition.h"

namespace {
mt::WtAudition table;
int16_t active[256], stage[256];
uint8_t* mapped;
size_t mappedSize;
mt::WavInfo wave;
uint64_t phase;
void stop() {
  table.stop();
  if (mapped) munmap(mapped, mappedSize);
  mapped = nullptr;
  phase = 0;
}
int16_t sample(uint32_t frame, int channel) {
  const uint32_t stride = wave.frameBytes();
  const auto* raw = mapped + wave.dataOffset + uint64_t(frame) * stride;
  mt::WavInfo ch = wave;
  if (wave.channels == 2) {
    ch.channels = 1;
    raw += channel * (wave.bits / 8);
  }
  int16_t value;
  mt::wavToMono(raw, 1, ch, &value);
  return value;
}
int16_t mix(int value, int addition) { return std::clamp(value + addition, -32768, 32767); }
}  // namespace

namespace audio {
BankResult wtAudition(const int16_t* pts) {
  if (!pts) return BankResult::Unsupported;
  std::lock_guard<brick::ProjectMutex> guard(brick::projectMutex());
  stop();
  table.bind(active, stage);
  float scratch[2 * mt::kWtHarm];
  table.part(1, 0, pts, 0, scratch);
  return table.part(1, 1, pts + 128, 0, scratch) ? BankResult::Ok : BankResult::ReadFail;
}
BankResult previewFile(const char* path, uint32_t* frames, uint32_t* rate) {
  std::lock_guard<brick::ProjectMutex> guard(brick::projectMutex());
  stop();
  auto& disk = brick::disk();
  auto* f = disk.openRead(path);
  if (!f) return BankResult::OpenFail;
  const auto e = mt::wavParse(*f, wave);
  mappedSize = f->size();
  disk.closeRead();
  if (e != mt::WavErr::Ok) return e == mt::WavErr::Unsupported ? BankResult::Unsupported : BankResult::NotWav;
  if (!wave.frames() || !wave.rate || uint64_t(wave.dataOffset) + wave.dataBytes > mappedSize)
    return BankResult::Truncated;
  int fd = open(fs::resolve(path).c_str(), O_RDONLY);
  if (fd < 0) return BankResult::OpenFail;
  void* p = mmap(nullptr, mappedSize, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (p == MAP_FAILED) return BankResult::NoMemory;
  mapped = static_cast<uint8_t*>(p);
  if (frames) *frames = wave.frames();
  if (rate) *rate = wave.rate;
  return BankResult::Ok;
}
void previewStop() {
  std::lock_guard<brick::ProjectMutex> guard(brick::projectMutex());
  stop();
}
}  // namespace audio

namespace brick {
void mixPreview(int16_t* l, int16_t* r, int n) {
  if (table.playing()) {
    int16_t mono[mt::Synth::kBlock];
    const int got = table.render(mono, n);
    for (int i = 0; i < got; ++i) {
      l[i] = mix(l[i], mono[i]);
      r[i] = mix(r[i], mono[i]);
    }
  }
  if (!mapped) return;
  for (int i = 0; i < n; ++i) {
    const auto at = static_cast<uint32_t>(phase / mt::kSynthRate);
    if (at >= wave.frames()) {
      stop();
      return;
    }
    const auto next = std::min(at + 1, wave.frames() - 1);
    const float frac = float(phase % mt::kSynthRate) / mt::kSynthRate;
    const auto value = [&](int ch) {
      const int v = sample(at, ch);
      return int(v + (sample(next, ch) - v) * frac);
    };
    l[i] = mix(l[i], value(0));
    r[i] = mix(r[i], value(1));
    phase += wave.rate;
  }
}
}  // namespace brick
