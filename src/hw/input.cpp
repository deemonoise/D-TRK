#include "input.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "pins.h"
#include "trackio.h"

namespace hw {
namespace {

QueueHandle_t queue;
pcnt_unit_handle_t unit;
int encAcc = 0;
bool shiftHeld = false;

// Integrating debouncer on an expander pin: state flips after 5 equal 1 ms samples
// (the expander is polled every 5 ms, so a change counts once it holds for one poll).
struct Debounce {
  uint8_t bit;
  bool state = false;
  uint8_t cnt = 0;
  // +1 on press, -1 on release, 0 otherwise.
  int update() {
    const bool raw = expanderDown(bit);
    if (raw == state) {
      cnt = 0;
      return 0;
    }
    if (++cnt < 5) return 0;
    cnt = 0;
    state = raw;
    return raw ? 1 : -1;
  }
};

Debounce encBtn{pins::kEncSwBit};
Debounce playBtn{pins::kPlayBit};
Debounce shiftBtn{pins::kShiftBit};
uint32_t encDownAt = 0;
bool longSent = false;

// Releases must not be lost (a dropped one leaves fill / a punch-in effect on): they wait for room.
void emit(InputType t, int8_t d = 0) {
  InputEvent e{t, d, shiftHeld};
  const bool release = t == InputType::PlayRelease || t == InputType::TrackRelease || t == InputType::ShiftUp;
  xQueueSend(queue, &e, release ? pdMS_TO_TICKS(100) : 0);
}

constexpr UBaseType_t kKeyRoom = 6;  // queue slots kept for buttons: turns wait in the counter

void setupEncoder() {
  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -1000;
  ucfg.high_limit = 1000;
  ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &unit));
  pcnt_glitch_filter_config_t fcfg = {};
  fcfg.max_glitch_ns = 1000;
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit, &fcfg));

  pcnt_chan_config_t a = {};
  a.edge_gpio_num = pins::kEncA;
  a.level_gpio_num = pins::kEncB;
  pcnt_chan_config_t b = {};
  b.edge_gpio_num = pins::kEncB;
  b.level_gpio_num = pins::kEncA;
  pcnt_channel_handle_t ca, cb;
  ESP_ERROR_CHECK(pcnt_new_channel(unit, &a, &ca));
  ESP_ERROR_CHECK(pcnt_new_channel(unit, &b, &cb));
  pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  gpio_pullup_en(static_cast<gpio_num_t>(pins::kEncA));
  gpio_pullup_en(static_cast<gpio_num_t>(pins::kEncB));

  ESP_ERROR_CHECK(pcnt_unit_enable(unit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
  ESP_ERROR_CHECK(pcnt_unit_start(unit));
}

// EC11: 4 counts per detent. Keeps the sub-detent remainder.
int readDetents() {
  int c = 0;
  pcnt_unit_get_count(unit, &c);
  const int d = (c - encAcc) / 4;
  encAcc += d * 4;
  if (encAcc > 800 || encAcc < -800) {  // well before the +-1000 limit: restart at 0, keep the remainder
    pcnt_unit_clear_count(unit);
    encAcc = -(c - encAcc);
  }
  return d;
}

void task(void*) {
  for (;;) {
    // While the UI is busy the turns stay in the counter (one larger turn later) instead of
    // filling the queue the button events need.
    const int d = uxQueueSpacesAvailable(queue) > kKeyRoom ? readDetents() : 0;
    if (d) emit(InputType::EncTurn, static_cast<int8_t>(d > 127 ? 127 : (d < -127 ? -127 : d)));

    const int s = shiftBtn.update();
    if (s == 1) {
      shiftHeld = true;
      emit(InputType::ShiftDown);
    } else if (s == -1) {
      shiftHeld = false;
      emit(InputType::ShiftUp);
    }

    const int pl = playBtn.update();
    if (pl == 1) emit(InputType::PlayPress);
    else if (pl == -1) emit(InputType::PlayRelease);

    const int e = encBtn.update();
    const uint32_t now = millis();
    if (e == 1) {
      encDownAt = now;
      longSent = false;
    }
    if (encBtn.state && !longSent && now - encDownAt >= kLongPressMs) {
      longSent = true;
      emit(InputType::EncLong);
    }
    if (e == -1 && !longSent) emit(InputType::EncClick);

    vTaskDelay(1);
  }
}

}  // namespace

void inputBegin() {
  queue = xQueueCreate(32, sizeof(InputEvent));
  setupEncoder();
  xTaskCreatePinnedToCore(task, "input", 3072, nullptr, 5, nullptr, 1);
}

bool inputPoll(InputEvent& ev, TickType_t wait) { return xQueueReceive(queue, &ev, wait) == pdTRUE; }

void inputPush(InputType t, int8_t delta) {
  if (queue) emit(t, delta);
}

}  // namespace hw
