#include "recorder.h"

#include <FS.h>
#include <SDL.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>

#include "brick_runtime.h"
#include "capture_controls.h"
#include "link/rec_client.h"
#include "rec.h"

namespace {
using namespace mt;
using Result = mt::link::RecResult;
RecCapture capture;
RecWriter writer;
RecAutoGain controller;
std::array<int16_t, kRecRingBytes / sizeof(int16_t)> ring;
std::atomic<float> multiplier{1};
SDL_AudioDeviceID input;
std::atomic<bool> delivered{false};
int failedDeviceCount = -1;
uint32_t failedAt = 0;
FILE* file;
char path[kRecPathMax], tmp[kRecPathMax];
uint16_t number, peak;
uint32_t clips, limit, gainTime;
bool sdOnly, limitedBySpace, automatic;
int applied;
void adjust(int db) {
  applied = db;
  multiplier = std::pow(10.f, brick::microphoneGainDb(db) / 20.f);
}
void inputCallback(void*, uint8_t* bytes, int length) {
  if (length < 4) return;
  delivered.store(true, std::memory_order_release);
  auto* samples = reinterpret_cast<int16_t*>(bytes);
  const float gain = multiplier.load();
  for (int i = 0; i < length / 2; ++i)
    samples[i] = static_cast<int16_t>(std::clamp(samples[i] * gain, -32768.f, 32767.f));
  brick::captureInput(samples, length / 4);
}
void closeInput() {
  if (input) SDL_CloseAudioDevice(input);
  input = 0;
  brick::releaseCaptureControls();
}
bool tryInput(const char* name, RecSource source) {
  if (!name && !brick::acquireCaptureControls(source == RecSource::Mic)) return false;
  delivered.store(false, std::memory_order_relaxed);
  SDL_AudioSpec spec{};
  spec.freq = kRecRate;
  spec.format = AUDIO_S16SYS;
  spec.channels = 2;
  spec.samples = 512;
  spec.callback = inputCallback;
  input = SDL_OpenAudioDevice(name, 1, &spec, nullptr, 0);
  if (!input) {
    brick::releaseCaptureControls();
    return false;
  }
  SDL_PauseAudioDevice(input, 0);
  // KNULLI's PipeWire ALSA plugin may accept a capture stream even when the kernel rejected
  // its hw_params. Report a usable input only after it actually supplied PCM, including silence.
  const uint32_t start = SDL_GetTicks();
  while (!delivered.load(std::memory_order_acquire) && SDL_GetTicks() - start < 1000) SDL_Delay(5);
  if (delivered.load(std::memory_order_acquire)) return true;
  closeInput();
  return false;
}
bool openInput(RecSource source) {
  if (input) return true;
  const int count = SDL_GetNumAudioDevices(1);
  if (const char* selected = std::getenv("DTRK_CAPTURE_DEVICE")) {
    if (*selected && tryInput(selected, source)) return true;
  } else {
    if (tryInput(nullptr, source)) return true;
    for (int i = 0; i < count; ++i) {
      const char* name = SDL_GetAudioDeviceName(i, 1);
      if (name && tryInput(name, source)) return true;
    }
  }
  failedDeviceCount = count;
  failedAt = SDL_GetTicks();
  return false;
}
bool sink(const void* data, uint32_t size, void*) { return file && fwrite(data, 1, size, file) == size; }
void drain() {
  // Armed capture retains the latest pre-roll and is the sole owner of the ring tail.
  if (capture.state() == RecState::Armed) return;
  int16_t samples[2048];
  while (auto n = capture.ring().pop(samples, 1024))
    if (!writer.write(samples, n)) {
      capture.stop();
      break;
    }
}
void disconnect() {
  brick::setCapture(nullptr);
  brick::monitorInput(false);
  closeInput();
  capture.stop();
}
void releaseFile(bool discard) {
  if (file) fclose(file);
  file = nullptr;
  if (discard && tmp[0]) std::remove(fs::resolve(tmp).c_str());
  tmp[0] = 0;
}
Result configure(int gain, bool monitor, uint8_t src, bool agc) {
  if (src > 2 || (file && src != uint8_t(capture.source()))) return Result::BadArg;
  auto source = static_cast<RecSource>(src);
  const bool sourceChanged = source != capture.source();
  brick::setCapture(nullptr);
  if (!file) capture.watch(source);
  if (sourceChanged || source == RecSource::Resample) closeInput();
  if (source != RecSource::Resample && !openInput(source)) return Result::NoCodec;
  automatic = agc && source != RecSource::Resample;
  controller.reset(source, gain);
  adjust(recGainClamp(source, gain));
  brick::setCapture(&capture);
  brick::monitorInput(monitor && input);
  return Result::Ok;
}
}  // namespace
namespace brick {
bool recording() { return file != nullptr; }
}  // namespace brick
namespace audio {
bool recLevel(mt::link::RecLevelRep& rep) {
  rep = {};
  if (file) drain();
  const int candidates = SDL_GetNumAudioDevices(1);
  rep.codec =
      input != 0 || (candidates > 0 && (candidates != failedDeviceCount || SDL_GetTicks() - failedAt >= 5000));
  capture.meter().take(rep.peak, rep.clips);
  if (automatic && millis() - gainTime >= mt::RecAutoGain::kWindowMs) {
    gainTime = millis();
    if (controller.update(capture.meter().takeGainPeak())) adjust(controller.gain());
  }
  rep.gain = applied;
  rep.frames = file ? capture.frames() : 0;
  rep.left = file && rep.frames < limit ? limit - rep.frames : 0;
  rep.state = uint8_t(file && capture.state() == mt::RecState::Idle ? mt::RecState::Saving : capture.state());
  if (file) {
    peak = std::max(peak, rep.peak);
    clips += rep.clips;
  }
  return true;
}
RecResult recSet(int8_t g, bool mon, uint8_t src, bool agc, mt::link::RecLevelRep* rep, bool* link) {
  if (link) *link = true;
  auto result = configure(g, mon, src, agc);
  if (rep) {
    recLevel(*rep);
    rep->result = uint8_t(result);
    if (result == Result::NoCodec) rep->codec = 0;
  }
  return result;
}
RecResult recStart(const mt::link::RecStartReq& req, uint16_t& out, bool* link) {
  if (link) *link = true;
  if (file) return Result::Busy;
  auto r = configure(req.gain, req.mon, req.source, req.autoGain);
  if (r != Result::Ok) return r;
  // Card, filename and header failures must not leave the input, ADC or monitor active.
  struct Rollback {
    bool armed = false;
    ~Rollback() {
      if (!armed) disconnect();
    }
  } rollback;
  brick::setCapture(nullptr);
  capture.init(ring.data(), ring.size() / 2);
  sdOnly = req.toSd;
  std::error_code ec;
  auto space = fs::disk::space(fs::root(), ec);
  if (ec) return Result::NoSd;
  auto diskLimit = mt::recSdLimitFrames(space.available, capture.channels());
  limit = sdOnly ? diskLimit : std::min(diskLimit, mt::kRecMaxFrames);
  limitedBySpace = limit == diskLimit && space.available < mt::kRecSdMaxFileBytes + mt::kRecSdReserveBytes;
  if (limit < mt::kRecRate) return Result::CardFull;
  for (int i = 0; i < mt::kRecNumberMax; ++i) {
    number = (std::max<int>(1, req.number) - 1 + i) % mt::kRecNumberMax + 1;
    mt::recPath(number, path);
    mt::recTmpPath(number, tmp);
    if (fs::disk::exists(fs::resolve(path))) continue;
    int fd = open(fs::resolve(tmp).c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
      if (errno == EEXIST) continue;
      return Result::WriteFail;
    }
    file = fdopen(fd, "wb");
    if (!file) {
      close(fd);
      releaseFile(true);
      return Result::WriteFail;
    }
    break;
  }
  if (!file) return Result::CardFull;
  uint8_t header[mt::kWavHeaderBytes]{};
  if (!sink(header, sizeof(header), nullptr)) {
    releaseFile(true);
    return Result::WriteFail;
  }
  peak = 0;
  clips = 0;
  out = number;
  writer.begin(capture.channels(), sink, nullptr);
  capture.arm(static_cast<mt::RecSource>(req.source), req.thresh, limit);
  brick::setCapture(&capture);
  rollback.armed = true;
  return Result::Ok;
}
RecResult recStartSd(uint8_t s, int8_t g, int8_t t, bool m, bool a, uint16_t& n, bool* l) {
  mt::link::RecStartReq req;
  req.source = s;
  req.gain = g;
  req.thresh = t;
  req.mon = m;
  req.autoGain = a;
  req.toSd = 1;
  return recStart(req, n, l);
}
RecResult recStop(mt::link::RecStopRep& rep, BankProgressFn cb, void* ctx, bool* link) {
  if (link) *link = true;
  rep = {};
  if (!file) return Result::NotRec;
  disconnect();
  drain();
  mt::link::RecLevelRep finalMeter{};
  recLevel(finalMeter);
  rep.number = number;
  rep.frames = writer.frames();
  rep.peak = peak;
  rep.clips = clips;
  rep.ended = uint8_t(capture.autoStopped() ? (limitedBySpace ? mt::link::RecEnd::Space : mt::link::RecEnd::Length)
                                            : mt::link::RecEnd::Stop);
  uint8_t header[mt::kWavHeaderBytes];
  if (!writer.finish(header) || fseek(file, 0, SEEK_SET) || !sink(header, sizeof(header), nullptr) ||
      fflush(file)) {
    releaseFile(true);
    return Result::WriteFail;
  }
  if (rep.frames < mt::kRecMinFrames) {
    releaseFile(true);
    return Result::TooShort;
  }
  if (std::rename(fs::resolve(tmp).c_str(), fs::resolve(path).c_str())) {
    releaseFile(true);
    return Result::WriteFail;
  }
  releaseFile(false);
  if (!sdOnly) {
    ImportOut result;
    auto status = brick::importRecording(path, result, cb, ctx);
    rep.bank = uint8_t(status);
    if (status != BankResult::Ok) return Result::Bank;
    rep.crc = result.crc;
  }
  return capture.overflow() ? Result::CardSlow : Result::Ok;
}
RecResult recCancel() {
  disconnect();
  if (file) releaseFile(true);
  return Result::Ok;
}
}  // namespace audio
