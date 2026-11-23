#include "engine/engine.h"

#include <fcntl.h>
#include <glob.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <random>

#include "audio/audio.h"
#include "engine_native.h"
#include "hw/ble_midi.h"
#include "record.h"
#include "sequencer.h"

namespace {
brick::ProjectMutex projectLock;
uint64_t clockUs;
}  // namespace

namespace engine {
namespace {
std::unique_ptr<mt::Sequencer> seq;
mt::RecordPractice practice;
mt::PracticePrefs practicePrefs;
uint32_t freshSeed() {
  static std::mt19937 rng(std::random_device{}());
  return rng();
}
void click(uint64_t t, bool accent) {
  uint8_t b[] = {mt::kMetronomeMsg, static_cast<uint8_t>(accent), practicePrefs.clickLevel};
  audio::post(t, mt::kTracks, b, 3);
}

struct Router final : mt::MidiSink {
  int fd = -1;
  ~Router() override {
    if (fd >= 0) close(fd);
  }
  void openOut() {
    if (fd >= 0) close(fd);
    fd = -1;
    if (const char* path = std::getenv("DTRK_MIDI_OUT")) {
      fd = open(path, O_WRONLY | O_NONBLOCK);
      return;
    }
    // The ALSA sequencer opens USB rawmidi subdevices on subscription. Opening the same
    // subdevice here would reserve it before the MIDI settings screen could select it.
    if (access("/dev/snd/seq", R_OK | W_OK) == 0) return;
    glob_t paths{};
    if (glob("/dev/snd/midiC*D*", 0, nullptr, &paths) == 0)
      for (size_t i = 0; i < paths.gl_pathc && fd < 0; ++i) fd = open(paths.gl_pathv[i], O_WRONLY | O_NONBLOCK);
    globfree(&paths);
  }
  void send(const uint8_t* b, uint8_t n) override {
    if (hw::ble::outReady())
      hw::ble::send(nowUs(), b, n);
    else if (fd >= 0)
      (void)!write(fd, b, n);
  }
  void synth(uint64_t t, uint8_t tr, const uint8_t* b, uint8_t n) override { audio::post(t, tr, b, n); }
  void beat(uint64_t t, bool accent) override {
    if (practicePrefs.clickMode == 2 || (practicePrefs.clickMode == 1 && practice.active())) click(t, accent);
  }
} midi;

#include "../../src/engine/commands.inc"
}  // namespace

void begin(mt::Project* p) {
  std::lock_guard<brick::ProjectMutex> guard(projectLock);
  seq = std::make_unique<mt::Sequencer>(*p);
  seq->seed(freshSeed());
  practice = {};
  practicePrefs = {};
  clockUs = 0;
  midi.openOut();
}
uint64_t nowUs() {
  std::lock_guard<brick::ProjectMutex> guard(projectLock);
  return clockUs;
}
bool post(Cmd cmd, uint16_t arg, uint8_t arg2) {
  std::lock_guard<brick::ProjectMutex> guard(projectLock);
  if (!seq) return false;
  handle({cmd, arg2, arg});
  return true;
}
bool postWait(Cmd cmd, uint16_t arg, uint8_t arg2) { return post(cmd, arg, arg2); }
void projectReplaced(uint16_t bpm) {
  post(Cmd::ReleaseTies);
  post(Cmd::SetBpm, bpm);
  brick::resetAudio();
}
void reserve(void*) {}
bool seqInternal() { return seq != nullptr; }
void* moveSeq(void*) { return nullptr; }
void lockProject() { projectLock.lock(); }
void unlockProject() { projectLock.unlock(); }
Status status() {
  std::lock_guard<brick::ProjectMutex> guard(projectLock);
  if (!seq) return {};
  return {seq->playing(),
          seq->paused(),
          seq->heardPattern(),
          static_cast<int8_t>(seq->pendingPattern()),
          seq->playPos(),
          seq->heardLoop(),
          static_cast<int8_t>(seq->heardSongPos()),
          seq->fill(),
          seq->song(),
          seq->heardStepTime(),
          seq->stepDuration(),
          practice.remaining(),
          practice.active(),
          practice.completed(),
          practice.endedAt()};
}
uint8_t stepPhase(const Status& s) { return mt::stepPhase256(nowUs(), s.stepT, s.stepUs); }
bool savedMutes(uint16_t& bits) { return seq && seq->savedMutes(bits); }
bool savedBpm(uint16_t& bpm) { return seq && seq->savedBpm(bpm); }
uint16_t projectBpm(const mt::Project& p) {
  std::lock_guard<brick::ProjectMutex> guard(projectLock);
  uint16_t bpm;
  return savedBpm(bpm) ? bpm : p.bpm;
}
mt::GenState heardGen(int tr) { return seq->heardGen(tr); }
EvoInfo heardEvoInfo(int tr, int len) {
  const auto& e = seq->heardEvo(tr);
  return {e.n, mt::evoChanged(e, len), seq->evoPasses(tr)};
}
void heardVar(int tr, mt::VarTrack& out, uint8_t& passes) {
  out = seq->heardVar(tr);
  passes = seq->varPasses(tr);
}
VarInfo heardVarInfo(int tr) {
  const auto& v = seq->heardVar(tr);
  return {v.n, v.sinceReset, seq->varPasses(tr)};
}
void mutClear(int tr) {
  if (tr >= 0 && tr < mt::kTracks) seq->mutClear(clockUs, midi, tr);
}
mt::LiveCapture captureLive(int tr, int pat, mt::Step* scratch) {
  return seq->captureLive(clockUs, tr, pat, scratch);
}
}  // namespace engine

namespace brick {
ProjectMutex& projectMutex() { return projectLock; }
void advance(uint64_t until) {
  using namespace engine;
  if (!seq) return;
  while (practice.counting() && practice.next() <= until) {
    const auto at = practice.next();
    const auto beat = practice.tick(at);
    if (beat == mt::RecordPractice::Tick::Start) {
      seq->setFollowSalt(freshSeed());
      seq->start(at, midi);
      practice.started(seq->heardPattern(), seq->heardLoop());
    } else
      click(at, beat == mt::RecordPractice::Tick::Accent);
  }
  seq->process(clockUs, midi);
  seq->process(until, midi);
  clockUs = until;
  practice.observe(seq->playing(), seq->heardPattern(), seq->heardLoop(), seq->heardStepTime(),
                   practicePrefs.onePass);
}
}  // namespace brick
