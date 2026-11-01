#include "syn_wave_preview.h"
#include "theme.h"
#include "audio/audio.h"
#include "link/bank_client.h"
#include "synth_params.h"
#include "fastmath.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ui {
namespace {
void params(const mt::Instrument& m, uint32_t age, mt::SynParams& p) {
  const float depth = (m.macro[mt::kMacSenv] - 64.f) * (1.f / 64.f);
  const float env = depth != 0 ? depth * mt::filterEnv(age, m.synEAtk, m.synEDec) : 0;
  const float pitch = 60.f + m.transpose + m.fine * .01f;
  const float det = (m.macro[mt::kMacDet] - 64.f) * (50.f / 6400.f);
  p.hz[0] = mt::noteHz(pitch);
  p.hz[1] = mt::noteHz(pitch + m.synSemi + det + mt::synEnvPitch2(m.synEnvDest, env));
  for (int k = 0; k < 2; ++k) {
    p.mode[k] = m.synOsc[k];
    p.shape[k] = m.macro[mt::kMacShp1 + k] * (1.f / 127.f);
  }
  p.mix = m.macro[mt::kMacMix] * (1.f / 127.f);
  p.sync = m.synSync;
  p.sub = m.synSub * (1.f / 127.f);
  p.subOct = m.synSubOct ? 2 : 1;
  p.subWave = m.synSubWave;
  p.noise = m.synNoise * (1.f / 127.f);
  p.fold = m.synFold * (1.f / 127.f);
  p.ring = m.synRing * (1.f / 127.f);
  mt::synApplyEnv(p, m.synEnvDest, env);
}
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
  for (auto& t : tables_) {
    if (t.data) heap_caps_free(t.data);
    t = Table();
  }
  clock_.reset(); model_.reset();
  keyValid_ = false; noMemory_ = false;
  lastMissingTry_ = 0;
  bankStamp_ = parkStamp_ = 0;
}

const int16_t* SynWavePreview::table(int k, const mt::Instrument& m, float shape) {
  Table& t = tables_[k];
  if (m.synOsc[k] != static_cast<uint8_t>(mt::SynOsc::Wt)) {
    if (t.data) heap_caps_free(t.data);
    t = Table();
    return nullptr;
  }
  const int f = static_cast<int>(mt::fclamp(shape, 0.f, 1.f) * (mt::kWtFrames - 1) + .5f);
  int8_t source[mt::kWtFrameLen];
  if (!audio::wtFrame(m.synWt[k], f, source)) return nullptr;
  uint32_t hash = 2166136261u;
  for (int8_t v : source) hash = (hash ^ static_cast<uint8_t>(v)) * 16777619u;
  if (!t.data) {
    t.data = static_cast<int16_t*>(heap_caps_malloc(mt::kWtTableSamples * sizeof(int16_t),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!t.data) { noMemory_ = true; return nullptr; }
  }
  if (t.frame != f || t.hash != hash || strncmp(t.name, m.synWt[k], sizeof(t.name)) != 0) {
    mt::synPreviewTable(source, t.data);
    snprintf(t.name, sizeof(t.name), "%s", m.synWt[k]);
    t.frame = f; t.hash = hash;
  }
  return t.data;
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
  uint32_t bankStamp = 0, parkStamp = 0;
  if (wt) {
    const auto& bank = audio::bankView();
    bankStamp = bank.gen;
    bankStamp = (bankStamp ^ bank.builtins) * 16777619u;
    for (uint8_t v : bank.wavetables) bankStamp = (bankStamp ^ v) * 16777619u;
    bankStamp = (bankStamp ^ (bank.mounted ? 1u : 0u) ^ (bank.ok ? 2u : 0u)) * 16777619u;
    parkStamp = audio::parkCount();
  }
  const bool changed = !keyValid_ || memcmp(key, key_, sizeof(key_)) != 0 ||
                       bankStamp != bankStamp_ || parkStamp != parkStamp_;
  // A table can finish loading without changing the instrument's settings. Retry a
  // missing frame/allocation at a bounded rate even while the model is held.
  const bool retry = keyValid_ && (frame_.missing[0] || frame_.missing[1] || noMemory_) &&
                     nowMs - lastMissingTry_ >= 500;
  if (clock_.update(nowMs, live_, changed || retry)) {
    mt::SynParams p;
    params(m, clock_.ageSamples(), p);
    noMemory_ = false;
    for (int k = 0; k < 2; ++k) p.wt[k] = table(k, m, p.shape[k]);
    mt::SynPreviewPhase phase;
    phase.mode = m.synPhaseMode; phase.phase[0] = m.synPhase[0]; phase.phase[1] = m.synPhase[1];
    model_.render(p, phase, live_ && !changed, frame_);
    lastMissingTry_ = nowMs;
    memcpy(key_, key, sizeof(key_)); keyValid_ = true;
    bankStamp_ = bankStamp; parkStamp_ = parkStamp;
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
    s.drawString(noMemory_ ? "NO WT RAM" : "MISSING WT", x, y + h - 43);
  } else {
    s.setTextColor(kDim); s.drawString(modPage ? "OUT" : "1", x, y + h - 43);
    if (!modPage) {
      s.setTextColor((kCyan & 0xf7deu) >> 1); s.drawString("2", x + 16, y + h - 43);
      s.setTextColor(kCursor); s.drawString("MIX", x + 32, y + h - 43);
    }
    const char* label = wt ? "WT SNAP" : "11.6 ms";
    s.setTextColor(kDim); s.drawString(label, x + w - 7 * kCharW, y + h - 43);
  }
  s.fillRect(x, y + h - 24, w, 22, kBeatBg);
  s.setTextColor(live_ ? kCursor : kText);
  const char* mode = live_ ? "LIVE" : "HOLD";
  s.drawString(mode, x + (w - 4 * kCharW) / 2, y + h - 21);
}
}  // namespace ui
