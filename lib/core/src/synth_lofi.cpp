#include "synth_lofi.h"
#include <math.h>
#include "fastmath.h"
#include "hot.h"
#include "synth_osc.h"

namespace mt {

namespace {

constexpr float kDcR = 0.995f;              // DC blocker pole: ~25 Hz at 32 kHz
constexpr int32_t kDriftStep = kSynthRate / 10;  // a new drift target every 100 ms
constexpr float kDriftK = 1.f / (0.15f * kSynthRate);  // smoothing towards it, ~150 ms per sample
constexpr float kDriftCents = 50;
constexpr float kApcWidthMin = 0.1f, kApcWidthOct = 6.3219281f;  // 0.1 .. 8 periods: log2(80)

MT_INLINE float clamp1(float x) { return x > 1.f ? 1.f : (x < -1.f ? -1.f : x); }
// Denormals: a decaying filter tail would crawl through them on the S3's FPU.
MT_INLINE float flush(float x) { return fabsf(x) < 1e-20f ? 0.f : x; }
MT_INLINE int u7(float m) {
  const int v = static_cast<int>(m * 127.f + 0.5f);
  return v < 0 ? 0 : (v > 127 ? 127 : v);
}

}  // namespace

MT_HOT void LofiVoice::trigger(uint32_t seed, bool sounding) {  // note-on: IRAM
  t_ = frac_ = 0;
  prime_ = true;
  if (!sounding) dcy_ = 0;
  last_ = 0xFFFFFFFFu;
  evals_ = 0;
  ph_ = 1;  // the first sample starts a pulse
  q_ = 0;
  on_ = false;
  rng_ = seed * 2654435761u | 1u;
  dTarget_ = dVal_ = 0;
  dLeft_ = 0;
}

void LofiVoice::control(const LofiParams& p, int n) {
  machine_ = p.machine;
  const float hz = p.hz > 0 ? p.hz : 0.f;
  if (machine_ == static_cast<uint8_t>(LofiMachine::Apc)) {
    // DRIFT: n samples of the walk.
    float hzD = hz;
    if (n > 0) {
      dLeft_ -= n;
      if (dLeft_ <= 0) {
        dLeft_ += kDriftStep;
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        dTarget_ = static_cast<int32_t>(rng_) * (1.f / 2147483648.f);
      }
      const float k = n * kDriftK;
      dVal_ += (dTarget_ - dVal_) * (k < 1.f ? k : 1.f);
    }
    const float drift = p.mac[2] > 0 ? (p.mac[2] < 1 ? p.mac[2] : 1.f) : 0.f;
    if (drift > 0) hzD *= fastExp2(drift * dVal_ * (kDriftCents / 1200.f));
    float inc = hzD * (1.f / kSynthRate);
    inc = inc < 0.49f ? inc : 0.49f;
    const float w = p.mac[0] > 0 ? (p.mac[0] < 1 ? p.mac[0] : 1.f) : 0.f;
    if (inc != inc_ || w != w_) {  // the divisions only when the pitch or WIDTH moved
      inc_ = inc, w_ = w;
      qInc_ = inc_ / (kApcWidthMin * fastExp2(w * kApcWidthOct));
      inv_ = inc_ > 0 ? 1.f / inc_ : 0.f;
      qInv_ = qInc_ > 0 ? 1.f / qInc_ : 0.f;
    }
    const float tone = p.mac[1] > 0 ? (p.mac[1] < 1 ? p.mac[1] : 1.f) : 0.f;
    g_ = fastExp2(-6.f * (1.f - tone));  // 127 = open
    smooth_ = p.mac[3] > 0 ? (p.mac[3] < 1 ? p.mac[3] : 1.f) : 0.f;
    return;
  }
  code_ = p.code;
  float r = hz * (kByteRatePerHz / kSynthRate);  // t per sample
  r = r < 65535.f ? r : 65535.f;
  incI_ = static_cast<uint32_t>(r);
  incF_ = static_cast<uint32_t>((r - static_cast<float>(incI_)) * 4294967296.f);
  a_ = u7(p.mac[0]);
  b_ = u7(p.mac[1]);
  c_ = u7(p.mac[2]);
  const float tone = p.mac[3] > 0 ? (p.mac[3] < 1 ? p.mac[3] : 1.f) : 0.f;
  interp_ = tone > 0;
  g_ = fastExp2(-5.f * tone);
  // LOOP 0 = off, 1..127 = t & (2^10 .. 2^20 - 1).
  const int lv = u7(p.mac[4]);
  mask_ = lv ? (1u << lofiLoopBits(lv)) - 1u : 0xFFFFFFFFu;
}

MT_HOT void LofiVoice::render(float* out, int n, float amp) {
  if (machine_ == static_cast<uint8_t>(LofiMachine::Apc)) renderApc(out, n, amp);
  else renderByte(out, n, amp);
}

// Per sample: the formula only when the (looped) t moved, so at most once; then TONE and the
// DC blocker.
MT_HOT void LofiVoice::renderByte(float* out, int n, float amp) {
  float lp = lp_, dcx = dcx_, dcy = dcy_, prev = prev_, cur = cur_;
  uint32_t t = t_, frac = frac_, last = last_;
  const ByteCode* code = code_;
  const uint8_t mode = code ? code->mode : kByteUnsigned;
  const float g = g_;
  for (int i = 0; i < n; ++i) {
    const uint32_t tt = t & mask_;
    if (tt != last) {
      last = tt;
      prev = cur;
      cur = code ? byteSample(byteEval(*code, tt, a_, b_, c_), mode) : 0.f;
      ++evals_;
      if (prime_) {  // the blocker's output stays where it is (dcy): no step at the note-on
        prime_ = false;
        prev = lp = cur;
        dcx = cur - (1.f - kDcR) * dcy;
      }
    }
    const float x = interp_ ? prev + (cur - prev) * (frac * (1.f / 4294967296.f)) : cur;
    lp += g * (x - lp);
    const float y = lp - dcx + kDcR * dcy;
    dcx = lp;
    dcy = y;
    out[i] += amp * clamp1(y);
    const uint32_t f = frac + incF_;
    t += incI_ + (f < frac);
    frac = f;
  }
  lp_ = flush(lp), dcx_ = flush(dcx), dcy_ = flush(dcy), prev_ = prev, cur_ = cur;
  t_ = t, frac_ = frac, last_ = last;
}

// Oscillator edge -> one-shot of WIDTH periods; edges while it runs are swallowed. SMOOTH scales
// the PolyBLEP corrections of both pulse edges; the output runs one sample late for them.
MT_HOT void LofiVoice::renderApc(float* out, int n, float amp) {
  float lp = lp_, dcx = dcx_, dcy = dcy_, ph = ph_, q = q_;
  bool on = on_;
  const float inc = inc_, qInc = qInc_, sm = smooth_, g = g_, inv = inv_, qInv = qInv_;
  for (int i = 0; i < n; ++i) {
    if (on) {
      q += qInc;
      if (q >= 1.f) {
        on = false;
        if (sm > 0) blep(-sm, (q - 1.f) * qInv);
      }
    }
    ph += inc;
    if (ph >= 1.f) {
      ph -= 1.f;
      if (!on) {
        const float x = ph * inv;  // samples since the edge
        on = true;
        q = x * qInc;
        if (sm > 0) blep(sm, x > 1.f ? 1.f : x);
        if (q >= 1.f) {  // shorter than this sample
          on = false;
          if (sm > 0) blep(-sm, (q - 1.f) * qInv);
        }
      }
    }
    const float s = bPrev_;
    bPrev_ = (on ? 1.f : 0.f) + bCorr_;
    bCorr_ = 0;
    lp += g * (s - lp);
    const float y = lp - dcx + kDcR * dcy;
    dcx = lp;
    dcy = y;
    out[i] += amp * clamp1(2.f * y);  // the 0 / 1 pulse at BYTE's full scale (-1 .. +1)
  }
  lp_ = flush(lp), dcx_ = flush(dcx), dcy_ = flush(dcy), ph_ = ph, q_ = q;
  on_ = on;
}

}  // namespace mt
