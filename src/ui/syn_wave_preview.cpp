#include "syn_wave_preview.h"
#include "theme.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "fastmath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ui {
namespace {
void trace(LGFX_Sprite& s, const float* points, int x, int y, int w, int h, float scale, uint16_t color) {
  const int mid = y + h / 2;
  int lastX = x, lastY = mid;
  for (int i = 0; i < mt::kSynPreviewPoints; ++i) {
    const float v = mt::fclamp(points[i] * scale, -1.f, 1.f);
    const int nx = x + i * (w - 1) / (mt::kSynPreviewPoints - 1);
    const int ny = mid - static_cast<int>(v * (h / 2 - 2));
    if (i) s.drawLine(lastX, lastY, nx, ny, color);
    lastX = nx; lastY = ny;
  }
}
void ranges(LGFX_Sprite& s, const float* lo, const float* hi, int x, int y, int w, int h,
            float scale, uint16_t color) {
  const int mid = y + h / 2;
  for (int i = 0; i < mt::kSynPreviewPoints; ++i) {
    const int nx = x + i * (w - 1) / (mt::kSynPreviewPoints - 1);
    const int top = mid - static_cast<int>(mt::fclamp(hi[i] * scale, -1.f, 1.f) * (h / 2 - 2));
    const int bot = mid - static_cast<int>(mt::fclamp(lo[i] * scale, -1.f, 1.f) * (h / 2 - 2));
    s.drawFastVLine(nx, top, bot - top + 1, color);
  }
}
}  // namespace

void SynWavePreview::reset() {
  clock_.reset(); model_.reset();
  keyValid_ = pending_ = false;
  memset(&frame_,0,sizeof frame_);
  frame_.mixScale = frame_.modScale = 1;
  lastMissingTry_ = lastFrameMs_ = bankStamp_ = 0;
}

void SynWavePreview::draw(LGFX_Sprite& s, const mt::Instrument& m, int x, int y, int w, int h,
                          bool modPage, uint32_t nowMs) {
  uint8_t key[sizeof(key_)];
  mt::packSyn(m, key);
  memcpy(key + mt::kSynRecSize, m.macro, mt::kFmMacros);
  key[mt::kSynRecSize + mt::kFmMacros] = static_cast<uint8_t>(m.transpose);
  key[mt::kSynRecSize + mt::kFmMacros + 1] = static_cast<uint8_t>(m.fine);
  const bool wt = m.synOsc[0] == static_cast<uint8_t>(mt::SynOsc::Wt) ||
                  m.synOsc[1] == static_cast<uint8_t>(mt::SynOsc::Wt);
  const uint32_t bankStamp = audio::bankMounted() ? audio::bank().generation() : 0;
  const bool changed = !keyValid_ || memcmp(key,key_,sizeof key_) != 0 || bankStamp != bankStamp_;
  const bool retry = keyValid_ && (frame_.missing[0] || frame_.missing[1]) && nowMs-lastMissingTry_ >= 500;
  pending_ = pending_ || changed || retry;
  if (clock_.update(nowMs,live_,pending_)) {
    mt::SynParams p;
    mt::synPreviewParams(m,clock_.ageSamples(),p);
    const mt::WtSource* source = audio::wavetableSource();
    for (int k=0; k<2; ++k)
      if (m.synOsc[k] == uint8_t(mt::SynOsc::Wt) && source) p.wt[k] = source->findWt(m.synWt[k]);
    model_.render(p,live_ && !changed,frame_);
    lastMissingTry_ = lastFrameMs_ = nowMs;
    memcpy(key_,key,sizeof key_); keyValid_ = true; pending_ = false;
    bankStamp_ = bankStamp;
  }
  s.fillRect(x, y, w, h, kBg);
  s.setTextColor(kDim);
  s.drawString("MODEL C4", x, y);
  const float scale = modPage ? frame_.modScale : frame_.mixScale;
  char gain[12]; snprintf(gain, sizeof(gain), "x%.2g", static_cast<double>(scale));
  s.drawString(gain, x + w - static_cast<int>(strlen(gain)) * kCharW, y);
  const int plotY = y + kCharH + 5, plotH = h - kCharH - 5 - 44;
  s.drawFastHLine(x, plotY + plotH / 2, w, kBeatBg);
  if (modPage) {
    ranges(s, frame_.modLo, frame_.modHi, x, plotY, w, plotH, scale, kDim);
    trace(s, frame_.mod, x, plotY, w, plotH, scale, kCursor);
  }
  else {
    ranges(s, frame_.mixLo, frame_.mixHi, x, plotY, w, plotH, scale, kBeatBg);
    trace(s, frame_.osc[0], x, plotY, w, plotH, scale, kDim);
    trace(s, frame_.osc[1], x, plotY, w, plotH, scale, (kCyan & 0xf7deu) >> 1);
    trace(s, frame_.mix, x, plotY, w, plotH, scale, kCursor);
  }
  const bool missing = frame_.missing[0] || frame_.missing[1];
  if (missing) {
    s.setTextColor(kRed);
    s.drawString("MISSING WT", x, y + h - 43);
  } else {
    s.setTextColor(kDim); s.drawString(modPage ? "OUT" : "1", x, y + h - 43);
    if (!modPage) {
      s.setTextColor((kCyan & 0xf7deu) >> 1); s.drawString("2", x + 16, y + h - 43);
      s.setTextColor(kCursor); s.drawString("MIX", x + 32, y + h - 43);
    }
    const char* label = wt ? "WT 16ms" : "16 ms";
    s.setTextColor(kDim); s.drawString(label, x + w - 7 * kCharW, y + h - 43);
  }
  s.fillRect(x, y + h - 24, w, 22, kBeatBg);
  s.setTextColor(live_ ? kCursor : kText);
  const char* mode = live_ ? "LIVE" : "HOLD";
  s.drawString(mode, x + (w - 4 * kCharW) / 2, y + h - 21);
}
}  // namespace ui
