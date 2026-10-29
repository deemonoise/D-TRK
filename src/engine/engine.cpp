#include "engine.h"
#include <Arduino.h>
#include <atomic>
#include <new>
#include <string.h>
#include "driver/gptimer.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "audio/audio.h"
#include "esp_random.h"
#include "hw/ble_midi.h"
#include "hw/midi_uart.h"
#include "record.h"
#include "sequencer.h"

namespace engine {
namespace {

// MIDI tracks go to the UART and to the BLE OUT device when one is up (one atomic load otherwise),
// INT tracks to the synth queue (stamped with their scheduled time: the synth plays them there,
// whatever the jitter of the engine wakeup).
struct Router : mt::MidiSink {
  hw::MidiUart uart;
  void send(const uint8_t* b, uint8_t len) override {
    uart.send(b, len);
    if (hw::ble::outReady()) hw::ble::send(nowUs(), b, len);
  }
  void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) override { audio::post(t, track, b, len); }
};

mt::Sequencer* seq;
void* seqMem = nullptr;  // reserve()
Router midi;
gptimer_handle_t timer;
TaskHandle_t task;
QueueHandle_t cmds;
SemaphoreHandle_t projMutex;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Status st{};
// Track bits (16); 32-bit so the atomic is a plain s32c1i CAS on the ESP32, no lock in the
// engine task (xtensa gcc reports is_always_lock_free false even for 32 bits, so no static_assert).
std::atomic<uint32_t> mutClearBits{0};  // mutClear(): tracks to clear before the next plan

bool IRAM_ATTR onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &woken);
  return woken == pdTRUE;
}

void handle(const Command& c) {
  const uint64_t now = nowUs();
  switch (c.cmd) {
    case Cmd::StartStop:
      if (seq->playing()) {
        seq->stop(now, midi);
      } else {
        seq->setFollowSalt(esp_random());  // live: a new follow path on every start
        seq->start(now, midi);
      }
      break;
    case Cmd::TogglePlay:
      if (!seq->playing() && !seq->paused()) seq->setFollowSalt(esp_random());  // resume keeps the path
      seq->togglePlay(now, midi);
      break;
    case Cmd::Stop: seq->stop(now, midi); break;
    case Cmd::QueuePattern: seq->queuePattern(c.arg); break;
    case Cmd::SelectPattern: seq->selectPattern(c.arg); break;
    case Cmd::SetBpm: seq->setBpm(c.arg); break;
    case Cmd::SetLiveBpm: seq->setLiveBpm(c.arg); break;
    case Cmd::SendProgram: seq->sendProgram(now, c.arg, midi); break;
    case Cmd::ReleaseTies: seq->releaseTies(now, midi); break;
    case Cmd::TrackOut: seq->trackOutChanged(now, c.arg, midi); break;
    case Cmd::SongEdit: seq->songEdited(now, midi, c.arg >> 8, static_cast<mt::SongOp>(c.arg & 0xFF)); break;
    case Cmd::SetSong: seq->setSong(now, midi, c.arg >> 15, c.arg & 0xFF); break;
    case Cmd::Fill: seq->setFill(c.arg != 0); break;
    case Cmd::GenEdit: seq->genEdited(now, midi); break;
    case Cmd::GenReseed: seq->genReseed(now, midi, c.arg); break;
    case Cmd::MutEdit: seq->mutEdited(now, midi); break;
    case Cmd::LiveNote: seq->liveNote(now, c.arg >> 8, c.arg & 0xFF, c.arg2, midi); break;
    case Cmd::LiveAllOff: seq->liveAllOff(now, midi); break;
    case Cmd::MidiPanic: seq->midiPanic(now, midi); break;
  }
}

void publish() {
  const Status s{seq->playing(),       seq->paused(),       seq->heardPattern(),
                 static_cast<int8_t>(seq->pendingPattern()), seq->playPos(), seq->heardLoop(),
                 static_cast<int8_t>(seq->heardSongPos()), seq->fill(), seq->song(),
                 seq->heardStepTime(),  seq->stepDuration()};
  portENTER_CRITICAL(&statusMux);
  st = s;
  portEXIT_CRITICAL(&statusMux);
}

void run(void*) {
  for (;;) {
    xSemaphoreTake(projMutex, portMAX_DELAY);
    Command c;
    while (xQueueReceive(cmds, &c, 0) == pdTRUE) handle(c);
    for (uint32_t b = mutClearBits.exchange(0); b; b &= b - 1) seq->mutClear(nowUs(), midi, __builtin_ctz(b));
    const uint64_t next = seq->process(nowUs(), midi);
    publish();  // under the mutex: core_ram may move seq (moveSeq)
    xSemaphoreGive(projMutex);

    if (next != mt::kNever) {
      if (next <= nowUs() + 30) continue;  // due almost now: spin once more
      gptimer_alarm_config_t a = {};
      a.alarm_count = next;
      gptimer_set_alarm_action(timer, &a);
      if (nowUs() >= next) continue;  // passed while arming: don't rely on a past alarm firing
    }
    // Woken by the alarm or by post(); the timeout is only a safety net.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
  }
}

}  // namespace

uint64_t nowUs() {
  uint64_t v = 0;
  gptimer_get_raw_count(timer, &v);
  return v;
}

void reserve(void* mem) { seqMem = mem; }

bool seqInternal() { return seq && esp_ptr_internal(seq); }

// The sequencer holds no pointers into itself (Project& and plain arrays): a byte copy moves it.
void* moveSeq(void* dst) {
  xSemaphoreTake(projMutex, portMAX_DELAY);
  void* old = seq;
  memcpy(dst, old, sizeof(mt::Sequencer));
  seq = static_cast<mt::Sequencer*>(dst);
  xSemaphoreGive(projMutex);
  return old;
}

void begin(mt::Project* p) {
  // ~34 KB: keep it out of PSRAM, the engine touches it on every event. reserve() handed it over at boot.
  // Its cold part (step history, MIDI-out note table, ~17 KB) in PSRAM: touched once per step / note.
  void* cold = heap_caps_malloc(mt::Sequencer::coldBytes(), MALLOC_CAP_SPIRAM);
  seq = seqMem ? new (seqMem) mt::Sequencer(*p, cold) : new mt::Sequencer(*p, cold);
  seq->seed(esp_random());
  midi.uart.begin();
  cmds = xQueueCreate(16, sizeof(Command));
  projMutex = xSemaphoreCreateMutex();

  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = onAlarm;
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, nullptr));
  ESP_ERROR_CHECK(gptimer_enable(timer));
  ESP_ERROR_CHECK(gptimer_start(timer));

  xTaskCreatePinnedToCore(run, "engine", 6144, nullptr, configMAX_PRIORITIES - 2, &task, 0);
}

bool post(Cmd c, uint16_t arg, uint8_t arg2) {
  const Command cmd{c, arg2, arg};
  const bool ok = xQueueSend(cmds, &cmd, 0) == pdTRUE;
  xTaskNotifyGive(task);
  return ok;
}

bool postWait(Cmd c, uint16_t arg, uint8_t arg2) {
  const Command cmd{c, arg2, arg};
  xTaskNotifyGive(task);  // a full queue drains while we wait
  const bool ok = xQueueSend(cmds, &cmd, pdMS_TO_TICKS(50)) == pdTRUE;
  xTaskNotifyGive(task);
  return ok;
}

Status status() {
  portENTER_CRITICAL(&statusMux);
  const Status s = st;
  portEXIT_CRITICAL(&statusMux);
  return s;
}

uint8_t stepPhase(const Status& s) { return mt::stepPhase256(nowUs(), s.stepT, s.stepUs); }


void lockProject() { xSemaphoreTake(projMutex, portMAX_DELAY); }
void unlockProject() { xSemaphoreGive(projMutex); }
bool savedMutes(uint16_t& bits) { return seq && seq->savedMutes(bits); }
bool savedBpm(uint16_t& bpm) { return seq && seq->savedBpm(bpm); }

uint16_t projectBpm(const mt::Project& p) {
  lockProject();
  uint16_t b;
  if (!savedBpm(b)) b = p.bpm;
  unlockProject();
  return b;
}

mt::GenState heardGen(int track) { return seq ? seq->heardGen(track) : mt::GenState{}; }

mt::GenState nextGen(int track, int pat, int& fromStep) {
  if (!seq) {
    fromStep = 0;
    return mt::GenState{};
  }
  fromStep = seq->genFromStep(track, pat);
  return seq->genNext(track);  // the track's register goes on over patterns
}

void heardEvo(int track, mt::EvoTrack& out, uint8_t& passes) {
  if (!seq) {
    mt::evoReset(out);
    passes = 0;
    return;
  }
  out = seq->heardEvo(track);
  passes = seq->evoPasses(track);
}

EvoInfo heardEvoInfo(int track, int len) {
  if (!seq) return EvoInfo{0, 0, 0};
  const mt::EvoTrack& e = seq->heardEvo(track);
  return EvoInfo{e.n, mt::evoChanged(e, len), seq->evoPasses(track)};
}

void heardVar(int track, mt::VarTrack& out, uint8_t& passes) {
  if (!seq) {
    mt::varReset(out);
    passes = 0;
    return;
  }
  out = seq->heardVar(track);
  passes = seq->varPasses(track);
}

VarInfo heardVarInfo(int track) {
  if (!seq) return VarInfo{0, 0, 0};
  const mt::VarTrack& v = seq->heardVar(track);
  return VarInfo{v.n, v.sinceReset, seq->varPasses(track)};
}

void mutClear(int track) {
  if (track >= 0 && track < mt::kTracks) mutClearBits.fetch_or(1u << track);
}

mt::LiveCapture captureLive(int track, int pat, mt::Step* scratch) {
  if (!seq || !scratch) return mt::LiveCapture::Off;
  return seq->captureLive(nowUs(), track, pat, scratch);
}

}  // namespace engine
