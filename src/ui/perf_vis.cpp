#include "perf_vis.h"
#include <Arduino.h>
#include <string.h>
#include "perf_look.h"
#include "theme.h"

namespace ui {

namespace {
static_assert(kScreenW == mt::kPerfLookW && kScreenH == mt::kPerfLookH, "perf look size");
constexpr uint8_t kIdleOn = 0x39, kIdleOff = 0x38;  // ST7796 IDMON / IDMOFF
// Ping-pong: row y is built while row y - 1 is still going out by DMA.
alignas(4) uint16_t line[2][kScreenW];
}  // namespace

void PerfVis::push(LGFX& lcd, LGFX_Sprite& spr, const mt::PerfStack& stack, float steps, uint32_t stepUs) {
  const uint32_t now = millis();
  mt::PerfFx fx[mt::kPerfButtons];
  uint32_t at[mt::kPerfButtons];
  mt::PerfVisIn in[mt::kPerfButtons];
  const int n = stack.count();
  for (int i = 0; i < n; ++i) {
    fx[i] = stack.at(i);
    at[i] = now;
    for (int j = 0; j < n_; ++j)
      if (fx_[j] == fx[i]) at[i] = at_[j];
    const float held = stepUs ? (now - at[i]) * 1000.f / stepUs : 0.f;
    in[i] = {fx[i], stack.depth(fx[i]), held};
  }
  memcpy(fx_, fx, sizeof(fx[0]) * n);
  memcpy(at_, at, sizeof(at[0]) * n);
  n_ = n;
  engaged_ = true;

  const mt::PerfLook look = mt::perfLook(in, n, steps);
  panel(lcd, look.invert, look.idle);
  if (look.freeze && pushed_) return;
  const auto* fb = static_cast<const uint16_t*>(spr.getBuffer());
  if (!fb) return;
  lcd.startWrite();
  for (int y = 0; y < kScreenH; ++y) {
    uint16_t* buf = line[y & 1];
    const int sy = mt::perfLookSrcRow(look, y);
    if (sy < 0) memset(buf, 0, sizeof(line[0]));
    else mt::perfLookRow(look, fb + sy * kScreenW, buf, kScreenW, mt::perfLookShift(look, y));
    lcd.pushImageDMA(0, y, kScreenW, 1, reinterpret_cast<const lgfx::swap565_t*>(buf));
  }
  lcd.endWrite();
  pushed_ = true;
}

void PerfVis::reset(LGFX& lcd) {
  if (!engaged_) return;
  panel(lcd, false, false);
  engaged_ = pushed_ = false;
  n_ = 0;
}

void PerfVis::panel(LGFX& lcd, bool invert, bool idle) {
  if (invert != invert_) {
    lcd.invertDisplay(invert);
    invert_ = invert;
  }
  if (idle != idle_) {
    lcd.startWrite();
    lcd.writeCommand(idle ? kIdleOn : kIdleOff);
    lcd.endWrite();
    idle_ = idle;
  }
}

}  // namespace ui
