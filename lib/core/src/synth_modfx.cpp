#include "synth_modfx.h"
#include <string.h>
#include "fastmath.h"
#include "hot.h"

namespace mt {
namespace {
constexpr float kMs = kFxRate / 1000.f;  // samples per ms
constexpr float kQuiet = 1e-5f;          // -100 dB
constexpr float kSmooth = 0.15f;         // depth / fb smoothing per segment (~7 ms)

// BBD voicing adapted from Deluge BRUNO CHORUS (bribes-v1.39, bruno106_chorus.cpp, GPL-3.0-or-later).
// Portions Copyright (C) 2026 Karl LaRocca (Ultramaster KR-106, GPL-3.0).
// Deluge integration Copyright (C) 2026 the Deluge community.
// D-TRK shares the mono send's delay line and adds only the wet signal.
constexpr float kBbdCenter = 3.30f;
constexpr float kBbdClockTrim = 0.015f;
constexpr float kBbdMinMs = 0.1f;
constexpr float kBbdDepthI = 2.13f;
static_assert(kFxRate == 44100.f, "BBD filters are tuned at 44.1 kHz");
static_assert(kBbdMinMs * kMs >= 2.f, "cubic read needs two newer samples");
static_assert((kBbdCenter + kBbdDepthI * 127.f / 64.f) * (1.f + kBbdClockTrim) * kMs <= ModFx::kLineLen - 3,
              "deepest BBD tap fits in the MOD line");

MT_INLINE bool isBbd(ModFxType type) { return type >= ModFxType::JunoI && type <= ModFxType::JunoBoth; }

MT_CONTROL __attribute__((noinline)) float bbdRateScale(uint8_t rate) {
  return fastExp2((static_cast<float>(rate > 127 ? 127 : rate) - 64.f) * (1.f / 32.f));
}

// Line write, truncated toward zero: a feedback loop of 0.9 cannot hold 1 LSB forever.
MT_INLINE int16_t lineWrite(float x) {
  x *= ModFx::kScale;
  x = x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : x);
  return static_cast<int16_t>(x);
}

// Fractional BBD tap in the same int16 ring as the existing modulation effects.
MT_INLINE float bbdRead(const int16_t* line, uint32_t write, float offset) {
  constexpr uint32_t mask = ModFx::kLineLen - 1;
  const auto whole = static_cast<uint32_t>(offset);
  const float fraction = offset - static_cast<float>(whole);
  const uint32_t at = write - whole;
  const float previous = line[(at + 1u) & mask], near = line[at & mask];
  const float far = line[(at - 1u) & mask], next = line[(at - 2u) & mask];
  const float c1 = 0.5f * (far - previous);
  const float c2 = previous - 2.5f * near + 2.f * far - 0.5f * next;
  const float c3 = 0.5f * (next - previous) + 1.5f * (near - far);
  return (((c3 * fraction + c2) * fraction + c1) * fraction + near) * (1.f / ModFx::kScale);
}

// MN3009 charge transfer loses a little more gain at slower clock rates.
MT_INLINE float bbdClockGain(float offset) {
  constexpr float loss = 4468.f;
  constexpr float invNominalClock = 1.f / 40000.f;
  constexpr float invClockPerSample = 0.002f / (256.f * kMs);
  return (1.f + loss * invNominalClock) - (loss * invClockPerSample) * offset;
}

MT_INLINE int16_t bbdLineWrite(float x) {
  constexpr float drive = 0.12f;
  return lineWrite(tanhSoft(drive * x) * (1.f / drive));
}

// Phaser allpass coefficient for an LFO value s (-1..1) and depth d (0..1).
MT_INLINE float apCoef(float s, float d) {
  const float u = 0.5f + 0.5f * d * s;                     // 0..1
  const float hz = 200.f * fastExp2(u * 4.3219281f);       // 200 .. 4000 Hz
  // (tan w - 1) / (tan w + 1) = (sin w - cos w) / (sin w + cos w): fastTan's series, one reciprocal
  // and no division (w <= 0.4).
  const float w = 3.14159265f / kFxRate * hz, w2 = w * w;
  const float sn =
      w * (1.f - w2 * (1.f / 6 - w2 * (1.f / 120 - w2 * (1.f / 5040 - w2 * (1.f / 362880 - w2 * (1.f / 39916800))))));
  const float cs = 1.f - w2 * (0.5f - w2 * (1.f / 24 - w2 * (1.f / 720 - w2 * (1.f / 40320 -
                                                                              w2 * (1.f / 3628800 - w2 * (1.f / 479001600))))));
  return (sn - cs) * fastRcp(sn + cs);
}
}  // namespace

MT_INLINE float ModFx::BbdLowPass::tick(float input) {
  // 9.66 kHz Butterworth and one pole at 0.45 Fs, at the fixed 44.1 kHz synth rate.
  constexpr float g = 0.82236413f;
  constexpr float a1 = 1.f / (1.f + g / 0.7071f + g * g);
  constexpr float a2 = g * a1, a3 = g * a2;
  const float v3 = input - s2;
  const float v1 = a1 * s1 + a2 * v3;
  const float v2 = s2 + a2 * s1 + a3 * v3;
  s1 = 2.f * v1 - s1;
  s2 = 2.f * v2 - s2;
  const float v = (v2 - pole) * 0.86327126f;
  const float low = pole + v;
  pole = low + v;
  return low;
}

MT_INLINE void ModFx::BbdLowPass::flush() {
  s1 = fabsf(s1) < 1e-15f ? 0.f : s1;
  s2 = fabsf(s2) < 1e-15f ? 0.f : s2;
  pole = fabsf(pole) < 1e-15f ? 0.f : pole;
}

void ModFx::clearState() {
  if (line_) memset(line_, 0, kLineLen * sizeof(int16_t));
  w_ = 0;
  for (int c = 0; c < 2; ++c) {
    for (int s = 0; s < kStages; ++s) ap_[c][s] = 0;
    apY_[c] = 0;
  }
  flY_ = 0;
  bbdPre_ = bbdPost_[0] = bbdPost_[1] = {};
  first_ = true;
  quiet_ = 0;
}

void ModFx::reset() {
  clearState();
  lfo_.ph = 0;
  fast_.ph = 0;
  depth_ = fb_ = 0;
  lv_ = 0;
  xf_ = 1;
  idle_ = true;
  type_ = want_ = 0;
}

// Level zero keeps a short history for the next wet ramp. This branch returns no audio.
MT_CONTROL __attribute__((noinline)) bool ModFx::primeBbdLine(const float* in, int n, uint32_t& write) {
  constexpr uint32_t mask = kLineLen - 1;
  uint32_t w = write;
  bool input = false;
  for (int i = 0; i < n; ++i, w = (w + 1) & mask) {
    line_[w] = bbdLineWrite(bbdPre_.tick(in[i]));
    input = input || in[i] != 0.f;
  }
  bbdPre_.flush();
  write = w;
  return input;
}

MT_HOT void ModFx::process(const float* in, float* mixL, float* mixR, int n, uint8_t type, uint8_t rate,
                           uint8_t depth, uint8_t fb, uint8_t level) {
  if (n <= 0) return;
  if (type >= static_cast<uint8_t>(ModFxType::Count)) type = 0;
  if (!line_ && type != static_cast<uint8_t>(ModFxType::Phaser)) type = 0;  // no line: off
  if (level > 127) level = 127;
  want_ = type;
  const float lvT = want_ == 0 ? 0.f : level * (1.f / 127.f);
  if (type_ != want_ && (type_ == 0 || idle_ || lv_ == 0.f)) {
    // Nothing audible to fade out: switch at once.
    clearState();
    type_ = want_;
    xf_ = 1;
    first_ = true;
  }
  if (type_ == 0 || (lv_ == 0.f && lvT == 0.f)) {
    if (type_ != static_cast<uint8_t>(ModFxType::Phaser) && type_ != 0 && line_) {
      // Level 0 on a line effect: keep writing the line, so a level rise reads the bus's recent past
      // instead of a cut-in (a step the size of the signal once the tap reaches it).
      constexpr uint32_t kMask = kLineLen - 1;
      uint32_t w = w_;
      bool input = false;
      if (isBbd(static_cast<ModFxType>(type_))) {
        input = primeBbdLine(in, n, w);
      } else {
        for (int i = 0; i < n; ++i, w = (w + 1) & kMask) {
          line_[w] = lineWrite(in[i]);
          input = input || in[i] != 0.f;
        }
      }
      w_ = w;
      // Idle once the line holds only silence (as after clearState).
      if (input) quiet_ = 0;
      else if (quiet_ < kQuietIdle) quiet_ += n;
      idle_ = quiet_ >= kQuietIdle;
      lv_ = 0;
      return;
    }
    if (!idle_) clearState();
    idle_ = true;
    lv_ = 0;
    return;
  }
  bool input = false;
  for (int i = 0; i < n && !input; ++i) input = in[i] != 0.f;
  if (idle_ && !input && type_ == want_) {
    lv_ = lvT;  // nothing to hear: the level jumps
    return;
  }
  idle_ = false;
  if (rate != rate_) {
    rate_ = rate;
    hz_ = fxRateHz(rate);
    bbdRateScale_ = bbdRateScale(rate);
  }
  const float dT = (depth > 127 ? 127 : depth) * (1.f / 127.f);
  const float fT = (fb > 127 ? 127 : fb) * (1.f / 127.f);
  const float dLv = (lvT - lv_) / n;
  float lv = lv_;
  bool loud = false;
  for (int i = 0; i < n; i += kFxSeg) {
    const int k = n - i < kFxSeg ? n - i : kFxSeg;
    depth_ += (dT - depth_) * kSmooth;
    fb_ += (fT - fb_) * kSmooth;
    if (fabsf(depth_ - dT) < 1e-6f) depth_ = dT;
    if (fabsf(fb_ - fT) < 1e-6f) fb_ = fT;
    const float pk = segment(in + i, mixL + i, mixR + i, k, lv, dLv);
    lv += dLv * k;
    if (pk > kQuiet) loud = true;
    // Type change: fade out done -> clear, take the new type, fade in.
    if (type_ != want_ && xf_ <= 0.f) {
      clearState();
      type_ = want_;
      xf_ = 0;
      if (type_ == 0) {
        idle_ = true;
        lv_ = lvT;
        return;
      }
    }
  }
  lv_ = lvT;
  if (input || loud) {
    quiet_ = 0;
  } else if ((quiet_ += n) >= kQuietIdle) {
    clearState();
    idle_ = true;
  }
}

namespace {
// The return's gain per sample of a segment (level ramp x type fade), apart from the DSP loops: the
// clamp's compares stay out of them.
MT_INLINE float segGains(float* gg, int k, float lv, float dLv, float xf, float fadeStep) {
  for (int j = 0; j < k; ++j) {
    lv += dLv;
    xf += fadeStep;
    xf = xf < 0.f ? 0.f : (xf > 1.f ? 1.f : xf);
    gg[j] = lv * xf;
  }
  return xf;
}

// The line effects' inner loop, 2 or 3 taps (one IRAM copy: the tap-count branch per sample is
// cheaper than a second copy of the loop). Returns the peak |return| (before the gain).
MT_INLINE float lineLoop(int kTaps, int16_t* line, uint32_t& wRef, const float* in, float* mixL, float* mixR,
                         int k, const float* gg, float* o, const float* dO, float g, float& fyRef) {
  constexpr float kIn = 1.f / ModFx::kScale;
  constexpr uint32_t kMask = ModFx::kLineLen - 1;
  uint32_t w = wRef;
  float fy = fyRef, pk = 0;
  float o0 = o[0], o1 = o[1], o2 = kTaps == 3 ? o[2] : 0.f;
  const float d0 = dO[0], d1 = dO[1], d2 = kTaps == 3 ? dO[2] : 0.f;
  for (int j = 0; j < k; ++j) {
    // Write first (offsets >= 1 read older samples), feedback from the previous output.
    line[w] = lineWrite(in[j] + g * fy);
    o0 += d0;
    o1 += d1;
    const float t0 = fracReadMask(line, kMask, w, o0) * kIn;
    const float t1 = fracReadMask(line, kMask, w, o1) * kIn;
    float yl, yr;
    if (kTaps == 3) {
      o2 += d2;
      const float t2 = fracReadMask(line, kMask, w, o2) * kIn;
      yl = (t0 + 0.5f * t1) * 0.67f;
      yr = (t2 + 0.5f * t1) * 0.67f;
    } else {
      yl = t0;
      yr = t1;
    }
    fy = 0.5f * (t0 + t1);
    w = (w + 1) & kMask;
    mixL[j] += yl * gg[j];
    mixR[j] += yr * gg[j];
    pk = maxf(pk, maxf(fabsf(yl), fabsf(yr)));
  }
  wRef = w;
  fyRef = fy;
  return pk;
}
}  // namespace

MT_CONTROL __attribute__((noinline)) void ModFx::bbdTargets(int k, float* t) {
  constexpr float rates[] = {0.514f, 0.842f, 7.85f};
  constexpr float depths[] = {kBbdDepthI, 1.71f, 0.236f};
  const auto index = type_ - static_cast<uint8_t>(ModFxType::JunoI);
  lfo_.advance(rates[index] * bbdRateScale_, k);
  const auto mode = static_cast<ModFxType>(type_);
  const float wave = mode == ModFxType::JunoBoth ? lfo_.at(0.f) : 1.f - 4.f * fabsf(lfo_.ph - 0.5f);
  const float sweep = wave * depths[index] * (127.f / 64.f) * depth_;
  t[0] = maxf(kBbdMinMs, (kBbdCenter + sweep) * (1.f - kBbdClockTrim)) * kMs;
  t[1] = maxf(kBbdMinMs, (kBbdCenter - sweep) * (1.f + kBbdClockTrim)) * kMs;
}

// One segment of k <= kFxSeg samples: LFO targets at the segment end, offsets / coefficients ramped.
// The segment's targets at its end (per segment of kFxSeg samples, ~1 kHz: hot): the LFOs move,
// then the phaser's two coefficients (t[0..1]) or the line taps' offsets (t[0..taps)). Returns the
// tap count (2 for the phaser).
MT_HOT int ModFx::targets(int k, float* t) {
  const ModFxType ty = static_cast<ModFxType>(type_);
  const float d = depth_;
  if (isBbd(ty)) {
    bbdTargets(k, t);
    return 2;
  }
  lfo_.advance(hz_, k);
  if (ty == ModFxType::Phaser) {
    t[0] = apCoef(lfo_.at(0.f), d);
    t[1] = apCoef(lfo_.at(0.25f), d);
    return 2;
  }
  if (ty == ModFxType::Chorus) {
    t[0] = 12.f * kMs + d * 6.f * kMs * lfo_.at(0.f);
    t[1] = 12.f * kMs + d * 6.f * kMs * lfo_.at(0.25f);
    return 2;
  }
  if (ty == ModFxType::Ensemble) {
    fast_.advance(6.3f, k);
    for (int q = 0; q < 3; ++q) {
      const float o = q * (1.f / 3.f);
      t[q] = 10.f * kMs + d * (5.f * kMs * lfo_.at(o) + 0.75f * kMs * fast_.at(o + 0.1f));
    }
    return 3;
  }
  // Flanger
  t[0] = 0.3f * kMs + d * 7.7f * kMs * (0.5f + 0.5f * lfo_.at(0.f));
  t[1] = 0.3f * kMs + d * 7.7f * kMs * (0.5f + 0.5f * lfo_.at(0.25f));
  return 2;
}

MT_HOT float ModFx::bbdLoop(const float* in, float* mixL, float* mixR, int k, const float* gg, const float* o,
                           const float* dO) {
  constexpr uint32_t mask = kLineLen - 1;
  uint32_t w = w_;
  BbdLowPass pre = bbdPre_, left = bbdPost_[0], right = bbdPost_[1];
  float oL = o[0], oR = o[1], peak = 0.f;
  for (int j = 0; j < k; ++j) {
    line_[w] = bbdLineWrite(pre.tick(in[j]));
    oL += dO[0];
    oR += dO[1];
    const float l = left.tick(bbdRead(line_, w, oL)) * (1.04f * bbdClockGain(oL));
    const float r = right.tick(bbdRead(line_, w, oR)) * (0.96f * bbdClockGain(oR));
    mixL[j] += l * gg[j];
    mixR[j] += r * gg[j];
    peak = maxf(peak, maxf(fabsf(l), fabsf(r)));
    w = (w + 1) & mask;
  }
  pre.flush();
  left.flush();
  right.flush();
  bbdPre_ = pre;
  bbdPost_[0] = left;
  bbdPost_[1] = right;
  w_ = w;
  return peak;
}

MT_HOT float ModFx::segment(const float* in, float* mixL, float* mixR, int k, float lv, float dLv) {
  float oT[3];
  const int taps = targets(k, oT);
  const float kInv = k == kFxSeg ? 1.f / kFxSeg : fastRcp(static_cast<float>(k));
  const float fadeStep = type_ != want_ ? -1.f / kFade : 1.f / kFade;
  float gg[kFxSeg];
  xf_ = segGains(gg, k, lv, dLv, xf_, fadeStep);
  float pk = 0;
  const ModFxType t = static_cast<ModFxType>(type_);
  if (t == ModFxType::Phaser) {
    const float aT[2] = {oT[0], oT[1]};
    if (first_) apA_[0] = aT[0], apA_[1] = aT[1], first_ = false;
    const float dA0 = (aT[0] - apA_[0]) * kInv, dA1 = (aT[1] - apA_[1]) * kInv;
    float a0 = apA_[0], a1 = apA_[1];
    const float g = fb_ * 0.85f;
    float y0 = apY_[0], y1 = apY_[1];
    // The allpass states in registers for the segment.
    float s00 = ap_[0][0], s01 = ap_[0][1], s02 = ap_[0][2], s03 = ap_[0][3], s04 = ap_[0][4], s05 = ap_[0][5];
    float s10 = ap_[1][0], s11 = ap_[1][1], s12 = ap_[1][2], s13 = ap_[1][3], s14 = ap_[1][4], s15 = ap_[1][5];
    static_assert(kStages == 6, "phaser stages unrolled");
#define MT_AP(x, a, st)          \
  {                              \
    const float o_ = a * x + st; \
    st = x - a * o_;             \
    x = o_;                      \
  }
    for (int j = 0; j < k; ++j) {
      a0 += dA0;
      a1 += dA1;
      float x0 = in[j] + g * y0, x1 = in[j] + g * y1;
      MT_AP(x0, a0, s00) MT_AP(x1, a1, s10)
      MT_AP(x0, a0, s01) MT_AP(x1, a1, s11)
      MT_AP(x0, a0, s02) MT_AP(x1, a1, s12)
      MT_AP(x0, a0, s03) MT_AP(x1, a1, s13)
      MT_AP(x0, a0, s04) MT_AP(x1, a1, s14)
      MT_AP(x0, a0, s05) MT_AP(x1, a1, s15)
      y0 = x0;
      y1 = x1;
      mixL[j] += y0 * gg[j];
      mixR[j] += y1 * gg[j];
      pk = maxf(pk, maxf(fabsf(y0), fabsf(y1)));
    }
#undef MT_AP
    const float st[2][kStages] = {{s00, s01, s02, s03, s04, s05}, {s10, s11, s12, s13, s14, s15}};
    for (int c = 0; c < 2; ++c)
      for (int s = 0; s < kStages; ++s) ap_[c][s] = fabsf(st[c][s]) < 1e-15f ? 0.f : st[c][s];
    apY_[0] = fabsf(y0) < 1e-15f ? 0.f : y0;
    apY_[1] = fabsf(y1) < 1e-15f ? 0.f : y1;
    apA_[0] = aT[0];
    apA_[1] = aT[1];
    return pk;
  }

  // Line effects: the target offsets (oT) at the segment end.
  if (first_) {
    for (int q = 0; q < taps; ++q) off_[q] = oT[q];
    first_ = false;
  }
  float o[3] = {}, dO[3] = {};
  for (int q = 0; q < taps; ++q) o[q] = off_[q], dO[q] = (oT[q] - off_[q]) * kInv;
  if (isBbd(t)) {
    pk = bbdLoop(in, mixL, mixR, k, gg, o, dO);
  } else {
    const float g = t == ModFxType::Flanger ? fb_ * 0.9f : 0.f;
    float fy = flY_;
    pk = lineLoop(taps, line_, w_, in, mixL, mixR, k, gg, o, dO, g, fy);
    flY_ = fabsf(fy) < 1e-15f ? 0.f : fy;
  }
  for (int q = 0; q < taps; ++q) off_[q] = oT[q];
  return pk;
}

}  // namespace mt
