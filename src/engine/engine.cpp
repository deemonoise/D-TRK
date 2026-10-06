#include "engine.h"
#include <Arduino.h>
#include <atomic>
#include <new>
#include "driver/gptimer.h"
#include "esp_heap_caps.h"
#include "audio/audio.h"
#include "esp_random.h"
#include "hw/midi_uart.h"
#include "record.h"
#include "sequencer.h"

namespace engine {
namespace {

// MIDI tracks go to the UART, INT tracks to the synth queue (stamped with their scheduled time:
// the synth plays them there, whatever the jitter of the engine wakeup).
struct Router : mt::MidiSink {
  hw::MidiUart uart;
  void send(const uint8_t* b, uint8_t len) override { uart.send(b, len); }
  void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) override { audio::post(t, track, b, len); }
};

mt::Sequencer* seq;
Router midi;
gptimer_handle_t timer;
TaskHandle_t task;
QueueHandle_t cmds;
SemaphoreHandle_t projMutex;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Status st{};
// Track bits (16); 32-bit so the atomic is a plain s32c1i CAS on the ESP32, no lock in the
// engine task (xtensa gcc reports is_always_lock_free false even for 32 bits, so no static_assert).
std::atomic<uint32_t> activity{0};

bool IRAM_ATTR onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &woken);
  return woken == pdTRUE;
}

void handle(const Command& c) {
  const uint64_t now = nowUs();
  switch (c.cmd) {
    case Cmd::StartStop:
      if (seq->playing()) seq->stop(now, midi);
      else seq->start(now, midi);
      break;
    case Cmd::TogglePlay: seq->togglePlay(now, midi); break;
    case Cmd::Stop: seq->stop(now, midi); break;
    case Cmd::QueuePattern: seq->queuePattern(c.arg); break;
    case Cmd::SelectPattern: seq->selectPattern(c.arg); break;
    case Cmd::SetBpm: seq->setBpm(c.arg); break;
    case Cmd::SendProgram: seq->sendProgram(now, c.arg, midi); break;
    case Cmd::ReleaseTies: seq->releaseTies(now, midi); break;
    case Cmd::TrackOut: seq->trackOutChanged(now, c.arg, midi); break;
    case Cmd::ChainEdit: seq->chainEdited(now, midi, c.arg >> 8, static_cast<mt::ChainOp>(c.arg & 0xFF)); break;
    case Cmd::Fill: seq->setFill(c.arg != 0); break;
    case Cmd::PerfOn: seq->perfOn(c.arg & 0xFF, static_cast<mt::PerfFx>(c.arg >> 8)); break;
    case Cmd::PerfOff: seq->perfOff(c.arg); break;
  }
}

void publish() {
  const Status s{seq->playing(),       seq->paused(),       seq->heardPattern(),
                 static_cast<int8_t>(seq->pendingPattern()), seq->playPos(), seq->loopCount(),
                 static_cast<int8_t>(seq->heardSongPos()), seq->fill(),
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
    const uint64_t next = seq->process(nowUs(), midi);
    const uint16_t act = seq->takeActivity();
    xSemaphoreGive(projMutex);
    if (act) activity.fetch_or(act, std::memory_order_relaxed);
    publish();

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

void begin(mt::Project* p) {
  // ~34 KB: keep it out of PSRAM, the engine touches it on every event.
  void* mem = heap_caps_malloc(sizeof(mt::Sequencer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  seq = mem ? new (mem) mt::Sequencer(*p) : new mt::Sequencer(*p);
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

bool post(Cmd c, uint16_t arg) {
  const Command cmd{c, arg};
  const bool ok = xQueueSend(cmds, &cmd, 0) == pdTRUE;
  xTaskNotifyGive(task);
  return ok;
}

bool postWait(Cmd c, uint16_t arg) {
  const Command cmd{c, arg};
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

uint16_t takeActivity() { return static_cast<uint16_t>(activity.exchange(0, std::memory_order_relaxed)); }

void lockProject() { xSemaphoreTake(projMutex, portMAX_DELAY); }
void unlockProject() { xSemaphoreGive(projMutex); }

}  // namespace engine
