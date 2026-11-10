#include "synth_preview.h"
#include <string.h>
#include "fastmath.h"
namespace mt {
bool SynPreviewClock::update(uint32_t nowMs, bool live, bool changed) {
  if (!initialized_) {
    initialized_ = true;
    lastSeen_ = lastFrame_ = nowMs;
    live_ = live;
    return true;
  }
  if (live_) ageMs_ = live ? ageMs_ + nowMs - lastSeen_ : frameAgeMs_;
  lastSeen_ = nowMs;
  live_ = live;
  if (!changed && !live) return false;  // HOLD retains the last visible capture.
  if (nowMs - lastFrame_ < 100) return false;
  lastFrame_ = nowMs;
  frameAgeMs_ = ageMs_;
  return true;
}
uint32_t SynPreviewClock::ageSamples() const {
  return static_cast<uint32_t>(static_cast<uint64_t>(ageMs_) * kSynthRate / 1000);
}
void SynPreviewClock::reset() { *this = SynPreviewClock(); }

namespace {
float fit(float peak) {
  float scale = 1.f;
  while (peak * scale > 1.f) scale *= .5f;
  return scale;
}
}  // namespace

void SynPreviewModel::render(const SynParams& p, bool advance, SynPreviewFrame& out) {
  const uint8_t dryMask = (p.mix < 1.f || p.sync ? 1u : 0u) | (p.mix > 0.f ? 2u : 0u);
  const uint8_t active = dryMask;
  // Production voices skip inaudible oscillators. If a modulation makes one audible
  // again, restart the local model coherently rather than overlay mismatched phases.
  if (active & ~active_) advance = false;
  active_ = active;
  if (!advance || !initialized_)
    for (auto& voice : voices_) {
      voice = SynVoice();  // Includes the deterministic noise seed for a frozen model.
      voice.trigger();
    }
  initialized_ = true;
  out.missing[0] = p.mode[0] == static_cast<uint8_t>(SynOsc::Wt) && !p.wt[0];
  out.missing[1] = p.mode[1] == static_cast<uint8_t>(SynOsc::Wt) && !p.wt[1];
  SynParams dry = p;
  dry.sub = dry.noise = 0;
  float peakMix = 0, peakMod = 0;
  for (int offset = 0; offset < kSynPreviewPoints * kSynPreviewStride; offset += 32) {
    // At most128samples of temporary work, not a full waveform/WT table on the UI stack.
    float block[4][32] = {};
    for (int trace = 0; trace < 4; ++trace) {
      SynParams params = trace == 3 ? p : dry;
      if (trace < 2) params.mix = static_cast<float>(trace);
      voices_[trace].control(params, 32);
      voices_[trace].render(block[trace], 32, 1.f);
    }
    for (int j = 0; j < 32; ++j) {
      const float samples[] = {block[0][j], block[1][j], block[2][j]};
      for (float x : samples) {
        const float a = x < 0 ? -x : x;
        if (a > peakMix) peakMix = a;
      }
      const float a = block[3][j] < 0 ? -block[3][j] : block[3][j];
      if (a > peakMod) peakMod = a;
    }
    for (int j = kSynPreviewStride - 1; j < 32; j += kSynPreviewStride) {
      const int point = (offset + j) / kSynPreviewStride;
      out.osc[0][point] = block[0][j];
      out.osc[1][point] = block[1][j];
      out.mix[point] = block[2][j];
      out.mod[point] = block[3][j];
      float mixLo = block[2][j], mixHi = mixLo, modLo = block[3][j], modHi = modLo;
      for (int a = j - kSynPreviewStride + 1; a < j; ++a) {
        if (block[2][a] < mixLo) mixLo = block[2][a];
        if (block[2][a] > mixHi) mixHi = block[2][a];
        if (block[3][a] < modLo) modLo = block[3][a];
        if (block[3][a] > modHi) modHi = block[3][a];
      }
      out.mixLo[point] = mixLo; out.mixHi[point] = mixHi;
      out.modLo[point] = modLo; out.modHi[point] = modHi;
    }
  }
  out.mixScale = fit(peakMix);
  out.modScale = fit(peakMod);
  // Keep oscillator state for LIVE, but borrow bank storage only during this call.
  // A bank import may remap/compact before the next UI frame.
  SynParams detached = p;
  detached.wt[0] = detached.wt[1] = nullptr;
  for (auto& voice : voices_) voice.control(detached, 32);
}

void SynPreviewModel::reset() {
  for (auto& voice : voices_) voice = SynVoice();  // Discard any cached bank pointers too.
  initialized_ = false;
  active_ = 0;
}

void synPreviewParams(const Instrument& m, uint32_t age, SynParams& p) {
  p = SynParams();
  const float depth = (fclamp(m.macro[kMacSenv], 0.f, 127.f) - 64.f) * (1.f / 64.f);
  const float env = depth != 0 ? depth * filterEnv(age, m.synEAtk, m.synEDec) : 0;
  const float pitch = 60.f + fclamp(m.transpose,-24.f,24.f) + fclamp(m.fine,-50.f,50.f)*.01f;
  const float det = (fclamp(m.macro[kMacDet],0.f,127.f)-64.f) * (50.f / 6400.f);
  p.hz[0] = noteHz(pitch);
  p.hz[1] = noteHz(pitch + fclamp(m.synSemi,-24.f,24.f) + det);
  for (int k=0; k<2; ++k) {
    p.mode[k] = m.synOsc[k] < uint8_t(SynOsc::Count) ? m.synOsc[k] : 0;
    p.shape[k] = fclamp(m.macro[kMacShp1+k] * (1.f / 127.f) + env, 0.f, 1.f);
  }
  p.mix = fclamp(m.macro[kMacMix],0.f,127.f) * (1.f / 127.f);
  p.sync = m.synSync;
  p.sub = fclamp(m.synSub,0.f,127.f) * (1.f / 127.f);
  p.subOct = m.synSubOct ? 2 : 1;
  p.noise = fclamp(m.synNoise,0.f,127.f) * (1.f / 127.f);
}
}  // namespace mt
