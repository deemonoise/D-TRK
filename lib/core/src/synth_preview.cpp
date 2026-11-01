#include "synth_preview.h"
#include <string.h>
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

void SynPreviewModel::render(const SynParams& p, const SynPreviewPhase& phase, bool advance, SynPreviewFrame& out) {
  const uint8_t dryMask = (p.mix < 1.f || p.sync ? 1u : 0u) | (p.mix > 0.f ? 2u : 0u);
  const uint8_t modMask = dryMask | (p.ring > 0.f ? 3u : 0u);
  const uint8_t active = dryMask | (modMask << 2);
  // Production voices skip inaudible oscillators. If a modulation makes one audible
  // again, restart the local model coherently rather than overlay mismatched phases.
  if (active & ~active_) advance = false;
  active_ = active;
  if (!advance || !initialized_)
    for (auto& voice : voices_) {
      voice = SynVoice();  // Includes the deterministic noise seed for a frozen model.
      voice.trigger(phase.mode, phase.phase[0], phase.phase[1], phase.seed, false);
    }
  initialized_ = true;
  out.missing[0] = p.mode[0] == static_cast<uint8_t>(SynOsc::Wt) && !p.wt[0];
  out.missing[1] = p.mode[1] == static_cast<uint8_t>(SynOsc::Wt) && !p.wt[1];
  SynParams dry = p;
  dry.fold = dry.ring = dry.sub = dry.noise = 0;
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
}

void synPreviewTable(const int8_t* frame, int16_t* out) {
  for (int level = 0; level < kWtLevels; ++level) {
    const int width = 1 << level, len = wtLevelLen(level);
    int16_t* dst = out + wtLevelOff(level);
    for (int i = 0; i < len; ++i) {
      int sum = 0;
      for (int j = 0; j < width; ++j) {
        const int index = (i * kWtFrameLen / len + j - width / 2) & (kWtFrameLen - 1);
        sum += frame[index];
      }
      // WtFrameRep transmits the bank's int16 points >> 8, not a normalized +/-127 wave.
      dst[i] = static_cast<int16_t>(sum * 256 / width);
    }
  }
  for (int f = 1; f < kWtFrames; ++f)
    memcpy(out + f * kWtFramePts, out, kWtFramePts * sizeof(int16_t));
}
}  // namespace mt
