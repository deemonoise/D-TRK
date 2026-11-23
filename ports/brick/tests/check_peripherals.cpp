#include <FS.h>
#include <SDL.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "audio/audio.h"
#include "brick_runtime.h"
#include "engine/engine.h"
#include "hw/ble_midi.h"
#include "hw/sdcard.h"
#include "link/rec_client.h"
#include "rec.h"
#ifdef __linux__
#include <fcntl.h>
#include <sound/asequencer.h>
#include <sys/ioctl.h>
#endif
namespace {
void require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}
// Confirms the microphone captured the two notes, in order, through the speakers.
bool recordedTones(const char* path) {
  auto* file = brick::disk().openRead(path);
  if (!file) return false;
  mt::WavInfo wav;
  bool ok = mt::wavParse(*file, wav) == mt::WavErr::Ok && wav.channels == 1 && wav.bits == 16 &&
            wav.rate == 44100 && file->seek(wav.dataOffset);
  constexpr size_t window = 2205;
  std::array<int16_t, window> pcm{};
  double best440 = 0, best880 = 0;
  bool first = false, second = false;
  for (size_t at = 0; ok && at + window <= wav.frames(); at += window) {
    if (!file->read(pcm.data(), sizeof(pcm))) {
      ok = false;
      break;
    }
    double energy = 0, x = 0, y = 0, x2 = 0, y2 = 0;
    for (size_t i = 0; i < window; ++i) {
      const auto phase = 6.283185307179586 * 440. * i / wav.rate;
      const double sample = pcm[i];
      energy += sample * sample;
      x += sample * std::cos(phase);
      y += sample * std::sin(phase);
      x2 += sample * std::cos(2 * phase);
      y2 += sample * std::sin(2 * phase);
    }
    const double p440 = 2 * (x * x + y * y) / (1 + window * energy);
    const double p880 = 2 * (x2 * x2 + y2 * y2) / (1 + window * energy);
    best440 = std::max(best440, p440);
    best880 = std::max(best880, p880);
    if (first && p880 > .08 && p880 > 2 * p440) second = true;
    if (p440 > .08 && p440 > 2 * p880) first = true;
  }
  brick::disk().closeRead();
  printf("Microphone spectral fractions: 440 Hz %.3f, 880 Hz %.3f\n", best440, best880);
  return ok && second;
}
struct Tones {
  SDL_AudioDeviceID device = 0;
  ~Tones() {
    if (device) SDL_CloseAudioDevice(device);
  }
  void prepare() {
    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.channels = 2;
    spec.format = AUDIO_S16SYS;
    spec.samples = 512;
    device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    require(device != 0, "open physical playback device");
    std::vector<int16_t> pcm(2 * 44100);
    for (int i = 0; i < 44100; ++i) {
      const int hz = i < 22050 ? 440 : 880;
      // -32 dBFS. Never adjust the system mixer.
      pcm[2 * i] = pcm[2 * i + 1] = static_cast<int16_t>(850 * std::sin(6.283185307179586 * hz * i / 44100.));
    }
    require(SDL_QueueAudio(device, pcm.data(), pcm.size() * sizeof(int16_t)) == 0, "queue quiet test tones");
  }
  void replay(const char* path, uint32_t frames) {
    SDL_AudioSpec spec{};
    Uint8* bytes = nullptr;
    Uint32 length = 0;
    require(SDL_LoadWAV(fs::resolve(path).c_str(), &spec, &bytes, &length) != nullptr, "reopen recorded WAV");
    std::unique_ptr<Uint8, decltype(&SDL_FreeWAV)> wave(bytes, SDL_FreeWAV);
    require(spec.format == AUDIO_S16LSB && spec.freq == 44100 && spec.channels == 1 &&
                length == frames * sizeof(int16_t), "replay original microphone WAV");
    std::vector<int16_t> stereo(2 * frames);
    for (uint32_t i = 0; i < frames; ++i) {
      const auto sample = static_cast<int16_t>(Uint16(bytes[2 * i]) | (Uint16(bytes[2 * i + 1]) << 8));
      stereo[2 * i] = stereo[2 * i + 1] = sample / 40;  // Limit replay to -32 dBFS.
    }
    require(SDL_QueueAudio(device, stereo.data(), stereo.size() * sizeof(int16_t)) == 0, "queue recorded WAV");
    const auto start = SDL_GetTicks();
    while (SDL_GetQueuedAudioSize(device) && SDL_GetTicks() - start < 1000 + frames * 1000 / 44100)
      SDL_Delay(10);
    require(SDL_GetQueuedAudioSize(device) == 0 && SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PLAYING,
            "physical output consumed recorded WAV");
    SDL_Delay(30);  // Let the final ALSA period play before the device closes.
    printf("Reopened and played recorded WAV: %u frames at 44100 Hz\n", frames);
  }
};
void checkRecorder(bool device) {
  auto p = std::make_unique<mt::Project>();
  p->reset();
  require(hw::sdBegin(), "SD initialization");
  audio::begin(p.get());
  engine::begin(p.get());
  mt::link::RecStartReq request;
  request.source = uint8_t(mt::RecSource::Resample);
  request.toSd = 1;
  uint16_t number = 0;
  std::array<int16_t, 1024> samples{};
  mt::link::RecStopRep result{};
  request.thresh = 0;  // Silence never reaches 0 dBFS.
  require(audio::recStart(request, number) == audio::RecResult::Ok, "arm threshold recording");
  for (int i = 0; i < 12; ++i) {
    brick::render(samples.data(), samples.size() / 2);
    mt::link::RecLevelRep level{};
    require(audio::recLevel(level) && level.frames == 0, "silence stays armed");
  }
  require(audio::recStop(result) == audio::RecResult::TooShort, "untriggered recording discards pre-roll");
  request.thresh = mt::kRecThreshOff;
  require(audio::recStart(request, number) == audio::RecResult::Ok, "arm resample to SD");
  engine::post(engine::Cmd::LiveNote, 60, 110);
  for (int i = 0; i < 12; ++i) brick::render(samples.data(), samples.size() / 2);
  require(audio::recStop(result) == audio::RecResult::Ok && result.frames == 6144 && result.peak > 0,
          "resample recording");
  char path[mt::kRecPathMax];
  mt::recPath(number, path);
  auto f = fs::File(fs::resolve(path), FILE_READ);
  require(f.size() == mt::kWavHeaderBytes + result.frames * 4, "sealed stereo WAV");
  request.toSd = 0;
  require(audio::recStart(request, number) == audio::RecResult::Ok, "arm cache recording");
  for (int i = 0; i < 12; ++i) brick::render(samples.data(), samples.size() / 2);
  require(audio::recStop(result) == audio::RecResult::Ok && result.crc, "recording imported into sample bank");
  if (device) {
    Tones tones;
    tones.prepare();
    request.source = uint8_t(mt::RecSource::Mic);
    request.toSd = 1;
    require(audio::recStart(request, number) == audio::RecResult::Ok, "open physical capture device");
    SDL_PauseAudioDevice(tones.device, 0);
    const uint32_t start = SDL_GetTicks();
    mt::link::RecLevelRep level{};
    do {
      SDL_Delay(20);
      audio::recLevel(level);
    } while (level.frames < 55125 && SDL_GetTicks() - start < 3000);
    const auto stop = audio::recStop(result);
    printf("capture driver=%s, available=%d, elapsed=%u ms, frames=%u, stop=%d\n", SDL_GetCurrentAudioDriver(),
           SDL_GetNumAudioDevices(1), SDL_GetTicks() - start, result.frames, static_cast<int>(stop));
    require(stop == audio::RecResult::Ok && result.frames >= mt::kRecMinFrames,
            "physical capture produced frames");
    mt::recPath(number, path);
    require(recordedTones(path), "recorded both acoustic test tones in order");
    tones.replay(path, result.frames);
    printf("ALSA capture: %u frames, peak %u\n", result.frames, result.peak);
  }
  audio::recCancel();
}
#ifdef __linux__
void checkMidi() {
  namespace ble = hw::ble;
  int fd = open("/dev/snd/seq", O_RDWR | O_NONBLOCK);
  require(fd >= 0, "open ALSA sequencer");
  snd_seq_port_info info{};
  int client = -1;
  require(ioctl(fd, SNDRV_SEQ_IOCTL_CLIENT_ID, &client) == 0, "query MIDI peer client id");
  info.addr.client = static_cast<unsigned char>(client);
  strcpy(info.name, "D-TRK test peer");
  info.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC;
  info.capability = SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_READ |
                    SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
  require(ioctl(fd, SNDRV_SEQ_IOCTL_CREATE_PORT, &info) == 0, "create loopback peer");
  ble::Found devices[64]{};
  int n = ble::scan(devices, 64);
  int i = 0;
  while (i < n && strcmp(devices[i].name, info.name)) ++i;
  require(i < n, "find loopback peer");
  const auto& peer = devices[i];
  ble::setActive(peer.addr, 0, peer.name, peer.addr, 0, peer.name);
  require(ble::outReady() && ble::state(ble::Slot::In) == ble::State::Connected, "subscribe both endpoints");
  auto exchange = [&](uint8_t status, uint8_t key, uint8_t value, int type, bool receive) {
    const uint8_t bytes[] = {status, key, value};
    const bool channelMessage = status < 0xf0;
    require(ble::send(0, bytes, channelMessage ? 3 : 1), "send test message");
    snd_seq_event ev{};
    bool got = false;
    for (int attempt = 0; attempt < 100 && !got; ++attempt) {
      if (read(fd, &ev, sizeof(ev)) == sizeof(ev))
        got = ev.type == type;
      else
        SDL_Delay(2);
    }
    require(got, "peer received event type");
    if (channelMessage)
      require(ev.data.note.channel == (status & 15) && ev.data.note.note == key && ev.data.note.velocity == value,
              "peer received exact note");
    if (!receive) return;
    ev.source = info.addr;
    ev.dest = {SNDRV_SEQ_ADDRESS_SUBSCRIBERS, 0};
    require(write(fd, &ev, sizeof(ev)) == sizeof(ev), "peer reply");
    mt::MidiMsg m{};
    got = false;
    for (int attempt = 0; attempt < 100 && !got; ++attempt) {
      got = ble::next(m);
      if (!got) SDL_Delay(2);
    }
    require(got && m.status == status && m.d1 == key && m.d2 == value, "received matching input");
  };
  exchange(0x92, 67, 98, SNDRV_SEQ_EVENT_NOTEON, true);
  exchange(0x82, 67, 32, SNDRV_SEQ_EVENT_NOTEOFF, true);
  exchange(0xfa, 0, 0, SNDRV_SEQ_EVENT_START, true);
  exchange(0xfc, 0, 0, SNDRV_SEQ_EVENT_STOP, true);
  exchange(0xf8, 0, 0, SNDRV_SEQ_EVENT_CLOCK, false);  // The tracker generates clock.
  ble::end();
  close(fd);
  puts("ALSA MIDI sequencer: Note On/Off and Start/Stop round trips, Clock OUT OK");
}
#endif
}  // namespace
int main(int argc, char** argv) {
  auto temp = (fs::disk::temp_directory_path() / "d-trk-peripherals-XXXXXX").string();
  if (!mkdtemp(temp.data()) || setenv("DTRK_DATA", temp.c_str(), 1)) return 1;
  setenv("DTRK_MIDI_OUT", "/dev/null", 1);
  bool device = argc == 2 && strcmp(argv[1], "--device") == 0;
  if (!device) SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
  int code = 0;
  try {
    require(SDL_Init(SDL_INIT_AUDIO) == 0, SDL_GetError());
#ifdef __linux__
    if (device) checkMidi();
#endif
    checkRecorder(device);
    puts("Recording round trip OK");
  } catch (const std::exception& e) {
    fprintf(stderr, "%s: %s\n", e.what(), SDL_GetError());
    code = 1;
  }
  SDL_Quit();
  fs::disk::remove_all(temp);
  return code;
}
