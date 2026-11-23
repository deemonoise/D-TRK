#include <algorithm>
#include <atomic>
#include <SDL.h>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <FS.h>
#include "audio/audio.h"
#include "brick_runtime.h"
#include "engine/engine.h"
#include "hw/sdcard.h"
#include "offline_render.h"
#include "output_gain.h"
#include "sample_set.h"
#include "sample_store.h"
#include "storage/render_io.h"

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Exercise the same pull callback as the handheld while exports use the real filesystem.
struct LiveOutput {
  SDL_AudioDeviceID device{};
  static constexpr uint64_t unity = (uint64_t{65536} << 32) | 65536;
  int gainFd = -1;
  brick::OutputGain gain;
  std::atomic<unsigned> silent{0}, audible{0}, suppressed{0};
  static void callback(void* ctx, Uint8* bytes, int n) {
    auto& out = *static_cast<LiveOutput*>(ctx);
    auto* pcm = reinterpret_cast<int16_t*>(bytes);
    brick::render(pcm, n / 4);
    const auto hasSignal = [=] { return std::any_of(pcm, pcm + n / 2, [](int16_t v) { return v != 0; }); };
    const bool input = hasSignal();
    out.gain.apply(pcm, n / 4);
    const bool output = hasSignal();
    if (input && !output) ++out.suppressed;
    auto& counter = output ? out.audible : out.silent;
    ++counter;
  }
  void setGain(uint64_t stereo) {
    if (device) SDL_LockAudioDevice(device);
    const auto written = pwrite(gainFd, &stereo, sizeof stereo, 0);
    if (device) SDL_UnlockAudioDevice(device);
    require(written == sizeof stereo, "Cannot write playback gain");
  }
  void start(const std::string& path) {
    gainFd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    setGain(unity);
    require(gain.open(path.c_str()), "Cannot map playback gain");
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    require(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0, SDL_GetError());
    SDL_AudioSpec want{};
    want.freq = mt::kSynthRate;
    want.channels = 2;
    want.format = AUDIO_S16SYS;
    want.samples = 512;
    want.userdata = this;
    want.callback = callback;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    require(device != 0, SDL_GetError());
    SDL_PauseAudioDevice(device, 0);
  }
  ~LiveOutput() {
    if (device) SDL_CloseAudioDevice(device);
    if (gainFd >= 0) close(gainFd);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  static bool advances(const std::atomic<unsigned>& counter) {
    const auto first = counter.load();
    const auto start = SDL_GetTicks();
    while (counter.load() - first < 2 && SDL_GetTicks() - start < 750) SDL_Delay(1);
    return counter.load() - first >= 2;
  }
};

std::vector<int16_t> loadPcm(const char* path, uint32_t frames, uint16_t bits, uint16_t channels,
                             std::vector<uint8_t>* raw = nullptr) {
  auto& disk = brick::disk();
  auto* file = disk.openRead(path);
  require(file, path);
  mt::WavInfo info;
  require(mt::wavParse(*file, info) == mt::WavErr::Ok, "Invalid exported WAV");
  require(info.frames() == frames && info.rate == mt::kSynthRate && info.bits == bits &&
          info.channels == channels, "Wrong WAV geometry");
  require(file->seek(info.dataOffset), "Cannot seek to PCM");
  std::vector<uint8_t> data(info.dataBytes);
  require(file->read(data.data(), data.size()), "Truncated PCM");
  disk.closeRead();
  std::vector<int16_t> mono(frames);
  mt::wavToMono(data.data(), frames, info, mono.data());
  require(std::any_of(mono.begin(), mono.end(), [](int16_t x) { return std::abs(int(x)) > 32; }), "Silent PCM");
  if (raw) *raw = std::move(data);
  return mono;
}

void checkExport(mt::Project& project, LiveOutput& live) {
  mt::ExportSpec spec;
  spec.stems = mt::ExportStems::Tracks;
  spec.tail = mt::ExportTail::Off;
  spec.bits = 24;
  storage::ExportStats stats;
  struct Observation { LiveOutput& live; bool silence = false; } state{live};
  require(storage::exportSet(project, spec, stats, [](int, int, uint32_t done, uint32_t total, void* ctx) {
    auto& obs = *static_cast<Observation*>(ctx);
    if (done == total && !obs.silence) obs.silence = LiveOutput::advances(obs.live.silent);
    return true;
  }, &state) == storage::Result::Ok, "Export failed");
  require(state.silence, "Live output callback blocked during export");
  mt::ExportOut outs[mt::kExportMaxOut];
  const int files = mt::exportOutputs(project, spec, outs);
  require(files == 2 && stats.files == files && stats.frames == mt::exportBodyFrames(project, spec), "Stem count or length");
  char dir[128];
  mt::exportDir(project, spec, dir, sizeof dir);
  auto readExports = [&] {
    std::vector<std::vector<uint8_t>> raw(files);
    for (int i = 0; i < files; ++i) {
      const auto path = std::string(dir) + "/" + outs[i].name;
      loadPcm(path.c_str(), stats.frames, spec.bits, 2, &raw[i]);
    }
    return raw;
  };
  const auto unityPcm = readExports();
  // A missing write in the second pass must not reuse the first pass's WAVs.
  std::filesystem::rename(fs::resolve(dir), fs::resolve(std::string(dir).append("-unity").c_str()));
  live.setGain(0);
  engine::post(engine::Cmd::StartStop);
  require(LiveOutput::advances(live.suppressed), "Playback mute did not suppress live audio");
  engine::post(engine::Cmd::Stop);
  require(storage::exportSet(project, spec, stats, nullptr, nullptr) == storage::Result::Ok, "Muted export failed");
  require(readExports() == unityPcm, "Playback mute changed exported master or stem PCM");
  live.setGain(LiveOutput::unity);
  // Cancel an opened replacement pass. Existing exports survive, temporary files disappear.
  using namespace mt::link;
  auto call = [](Msg type, const auto& request) {
    uint8_t data[kMaxPayload], reply[kMaxPayload];
    Writer writer(data, sizeof data);
    encode(request, writer);
    require(writer.ok(), "Malformed request");
    Reader reader(reply, brick::renderRequest(type, data, writer.size(), reply));
    RenderRep decoded;
    require(decode(reader, decoded) && decoded.result == static_cast<uint8_t>(RenderResult::Ok), "Render transaction failed");
  };
  RenderStartReq start;
  start.target = RenderStartReq::kExport;
  std::snprintf(start.path, sizeof start.path, "%s", dir);
  call(Msg::RenderStart, start);
  RenderOut add;
  std::snprintf(add.name, sizeof add.name, "%s", outs[0].name);
  call(Msg::RenderOut, add);
  RenderEndReq end;
  end.abort = 1;
  call(Msg::RenderEnd, end);
  const auto master = std::string(dir) + "/" + outs[0].name;
  require(!brick::disk().exists((master + ".tmp").c_str()), "Cancelled render left a temporary file");
  loadPcm(master.c_str(), stats.frames, spec.bits, 2);
}

void checkBank(mt::Project& project) {
  auto plan = mt::resamplePlan(project, 0, 1, 0, storage::kResampleMaxFrames);
  storage::RenderStats stats;
  char name[mt::kSampleNameMax + 1];
  require(storage::resample(project, plan, name, stats, nullptr, nullptr) == storage::Result::Ok, "Resample failed");
  require(stats.frames == plan.frames, "Wrong loop length");
  mt::BounceSpec shot;
  shot.note = 64;
  shot.holdBlocks = 12;
  require(storage::bounce(project, shot, "SHOT", stats, nullptr, nullptr) == storage::Result::Ok, "One-shot failed");
  require(stats.copied, "One-shot library copy failed");
  const int index = mt::projSampleFind(project, "SHOT");
  require(index >= 0 && mt::projSampleBank(project, brick::bank(), index) >= 0, "Missing bank entry");
  char path[mt::kStorePathMax];
  const auto crc = project.samples[index].crc;
  mt::storePath(crc, path);
  auto mono = loadPcm(path, stats.frames, 16, 1);
  require(mt::sampleCrc(mono.data(), stats.frames) == crc, "Store PCM differs from the sample key");
}
}  // namespace

int main() {
  char temp[] = "/tmp/d-trk-render-XXXXXX";
  if (!mkdtemp(temp) || setenv("DTRK_DATA", temp, 1) != 0) return 1;
  int result = 0;
  try {
    static mt::Project project;
    project.reset();
    std::strcpy(project.name, "CHECK");
    project.tracks[0].out = mt::TrackOut::Int;
    mt::instrSetType(project.instruments[0], mt::InstrType::Chip);
    project.patterns[0].length = 8;
    project.patterns[0].steps[0][0].note = 60;
    audio::begin(&project);
    require(hw::sdBegin(), "Cannot initialize storage");
    engine::begin(&project);
    LiveOutput live;
    live.start(std::string(temp) + "/output.gain");
    engine::post(engine::Cmd::StartStop);
    require(LiveOutput::advances(live.audible), "No playback before export");
    engine::post(engine::Cmd::Stop);
    checkExport(project, live);
    checkBank(project);
    engine::post(engine::Cmd::StartStop);
    require(LiveOutput::advances(live.audible), "Live playback did not resume after offline render");
    engine::post(engine::Cmd::Stop);
    std::puts("Export master + stem identical at unity/mute, cancel, resample, one-shot, WAV and PCM CRC OK");
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    result = 1;
  }
  std::filesystem::remove_all(temp);
  return result;
}
