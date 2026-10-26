#include "synth_vfilter.h"
#include <string.h>

namespace mt {

namespace {

// State families: a switch inside one keeps the state, across them resets it.
MT_INLINE int family(FltKind k) { return k <= FltKind::Hp ? 0 : static_cast<int>(k); }

// LADDER feedback at resonance 1: the linear loop self-oscillates from k = 4 (each pole -3 dB and
// -45 degrees at the cutoff); a little over it starts and holds the oscillation, vfSat sets its level.
constexpr float kLadderK = 4.2f;
// MS20 feedback at resonance 1: K = 2 is the linear loop's edge (zero damping, it only sustains);
// a little over it screams on its own, vfSat holds the level.
constexpr float kMs20K = 2.1f;

// SID: the 6581's resonance never reaches self-oscillation, k = 1 / Q from sqrt 2 (Q 0.707, r 0) to
// 0.5 (Q 2, r 1); the input's x^2 term (even harmonics, +-1 in -> +1.15 / -0.85 before vfSat); the DC
// blocker's one-pole coefficient (2 pi 3.5 Hz / rate).
constexpr float kSidK0 = 1.4142136f, kSidK1 = 0.5f;
constexpr float kSidEven = 0.15f;
constexpr float kSidDc = 0.0005f;

// Tail fade: -60 dB in 50 ms per sample; the cap: a tail longer than 2 s fades too (slow decays just
// under the self-oscillation threshold).
constexpr float kTailFade = 0.996872133f;  // 10^(-3 / (0.05 x 44100))
constexpr uint32_t kTailCap = 2 * kSynthRate;

// COMB: the line holds u = vfSat(y / 2) in -1..1 as u x 32766 (s = 2 u); the peak hold per block
// (0.9^16 = 0.19 over the longest delay of 16 blocks: still ringing between the echoes of an impulse).
constexpr float kLineScale = 32766.f;
constexpr float kCombHold = 0.9f;

// max(m, |u|): VMAXNM on the Teensy (no compare, VMRS, branch).
MT_INLINE float maxAbs(float m, float u) {
#if defined(__IMXRT1062__)
  return __builtin_fmaxf(m, __builtin_fabsf(u));
#else
  const float au = u < 0 ? -u : u;
  return au > m ? au : m;
#endif
}

// FORMANT: male vowel formants, log2 Hz (interpolated in log), A E I O U.
MT_DATA const float kVowels[5][3] = {
    {9.511753f, 10.090112f, 11.252665f},  // A 730 / 1090 / 2440
    {9.049849f, 10.845490f, 11.276124f},  // E 530 / 1840 / 2480
    {8.076816f, 11.161132f, 11.555548f},  // I 270 / 2290 / 3010
    {9.154818f, 9.714246f, 11.234817f},   // O 570 / 840 / 2410
    {8.228819f, 9.764872f, 11.129283f},   // U 300 / 870 / 2240
};

// TPT one-pole gain G = g / (1 + g), g = tan(pi hz / rate), hz clamped as the Svf's.
MT_INLINE float onePoleG(float hz) {
  const float maxHz = kSynthRate * 0.45f;
  hz = hz < 20.f ? 20.f : (hz > maxHz ? maxHz : hz);
  const float g = fastTan(hz * (3.14159265f / kSynthRate));
  return g * fastRcp(1.f + g);
}

}  // namespace

#ifdef PIO_UNIT_TESTING
uint32_t VoiceFilter::formantComputes = 0;
#endif

MT_HOT void VoiceFilter::set(FltKind k, float hz, float reso127, float q) {
  if (family(k) != family(kind_)) {
    if (k <= FltKind::Hp)
      st_.svf = Svf();
    else if (k == FltKind::Ladder)
      st_.ladder = Ladder();
    else if (k == FltKind::Ms20)
      st_.ms20 = Ms20();
    else if (k == FltKind::Comb)
      st_.comb = Comb();
    else if (k == FltKind::Sid)
      st_.sid = Sid();
    else
      st_.formant = Formant();
    hz_ = res_ = -1;
    tail_ = 1;
    tailN_ = 0;
  }
  kind_ = k;
  if (k <= FltKind::Hp) {
    st_.svf.set(static_cast<Svf::Mode>(k), hz, q);
    return;
  }
  if (hz == hz_ && reso127 == res_) return;  // Synth sets every control tick
  hz_ = hz;
  res_ = reso127;
  const float r = (reso127 < 0 ? 0 : (reso127 > 127 ? 127 : reso127)) * (1.f / 127.f);
  if (k == FltKind::Ladder)
    setLadder(hz, r);
  else if (k == FltKind::Ms20)
    setMs20(hz, r);
  else if (k == FltKind::Comb)
    setComb(hz, r);
  else if (k == FltKind::Sid)
    setSid(hz, r);
  else
    setFormant(hz, r);
}

void VoiceFilter::reset() {
  tail_ = 1;
  tailN_ = 0;
  switch (kind_) {
    case FltKind::Ladder:
      for (float& s : st_.ladder.s) s = 0;
      break;
    case FltKind::Ms20:
      for (float& s : st_.ms20.s) s = 0;
      break;
    case FltKind::Comb:
      st_.comb.w = 0;
      st_.comb.peak = 0;
      st_.comb.ready = false;
      break;
    case FltKind::Formant:
      for (auto& b : st_.formant.ic) b[0] = b[1] = 0;
      break;
    case FltKind::Sid:
      for (float& s : st_.sid.s) s = 0;
      break;
    default: st_.svf.reset(); break;
  }
}

MT_HOT void VoiceFilter::setLadder(float hz, float r) {
  Ladder& l = st_.ladder;
  const float G = onePoleG(hz), b = 1.f - G;
  l.G = G;
  l.k = kLadderK * r;
  l.comp = 1.f + 0.5f * l.k;
  osc_ = l.k > 4.f;
  // y_i = G x_i + (1 - G) s_i per pole, so y4 = G^4 u + sum G^(4-i) (1 - G) s_i, u = x - k y4:
  // y4 = (G^4 x + sum) / (1 + k G^4).
  const float G2 = G * G, G4 = G2 * G2;
  const float inv = fastRcp(1.f + l.k * G4);  // >= 1: no __divsf3
  l.gi = G4 * inv;
  l.c[0] = G2 * G * b * inv;
  l.c[1] = G2 * b * inv;
  l.c[2] = G * b * inv;
  l.c[3] = b * inv;
}

MT_HOT void VoiceFilter::setMs20(float hz, float r) {
  Ms20& m = st_.ms20;
  const float G = onePoleG(hz), b = 1.f - G;
  m.G = G;
  m.K = kMs20K * r;
  m.b = b;
  osc_ = m.K > 2.f;
  // u = y1 + hp(K lp2(u)): lp2(u) = G u + b s2, hp(y) = b y - b s3, so
  // u (1 - K G b) = y1 + K b^2 s2 - b s3.
  m.a0 = fastRcp(1.f - m.K * G * b);  // K G b <= 2 x 0.25: >= 0.5, no __divsf3
  m.f2 = m.K * b * b;
}

MT_HOT void VoiceFilter::setComb(float hz, float r) {
  Comb& c = st_.comb;
  const float maxHz = kSynthRate * 0.45f;
  hz = hz < 20.f ? 20.f : (hz > maxHz ? maxHz : hz);
  const float d = kSynthRate * fastRcp(hz);  // >= 2.2
  c.dT = d > kLineLen - 1 ? static_cast<float>(kLineLen - 1) : d;
  c.fb = 0.97f * r;
  c.gain = 2.f * fastRcp(1.f + c.fb);
}

// The 6581's cutoff range, uniform in octaves: the knob's 20 Hz x 700^(v / 127) (cutoffHz, key
// tracking, envelope) to 200 Hz x 60^(v / 127), 200 Hz .. 12 kHz (log2 60 / log2 700 = 0.6254).
// Control rate, only on a change: not MT_HOT (flash via the .ld list, ITCM is short).
void VoiceFilter::setSid(float hz, float r) {
  Sid& s = st_.sid;
  hz = hz < 20.f ? 20.f : (hz > 14000.f ? 14000.f : hz);
  const float g = fastTan(200.f * fastPow(hz * (1.f / 20.f), 0.6254f) * (3.14159265f / kSynthRate));
  const float k = kSidK0 - (kSidK0 - kSidK1) * r;
  s.a1 = fastRcp(1.f + g * (g + k));  // >= 1: no __divsf3
  s.a2 = g * s.a1;
  s.a3 = g * s.a2;
  osc_ = false;
}

template <bool kIn, bool kRamp>
MT_INLINE void VoiceFilter::combLoop(const float* __restrict in, float* __restrict out, int n,
                                     int16_t* __restrict line) {
  Comb& c = st_.comb;
  if (!c.ready) {  // a new note: a clean line, the delay at its target
    memset(line, 0, kLineLen * sizeof(int16_t));
    c.ready = true;
    c.d = c.dT;
  }
  constexpr uint32_t kMask = kLineLen - 1;
  float d = c.d;
  const float dStep = kRamp ? (c.dT - d) * fastRcp(static_cast<float>(n)) : 0.f;
  int di = static_cast<int>(d);
  float fr = d - static_cast<float>(di);
  const float fb = c.fb * (2.f / kLineScale), gain = c.gain;
  uint32_t w = c.w;
  float pk = 0;
  for (int i = 0; i < n; ++i, ++w) {
    if (kRamp) {
      d += dStep;
      di = static_cast<int>(d);
      fr = d - static_cast<float>(di);
    }
    const uint32_t r = w - static_cast<uint32_t>(di);  // y[n - di], y[n - di - 1]
    const float a = line[r & kMask], b = line[(r - 1) & kMask];
    const float y = (kIn ? in[i] : 0.f) + fb * (a + (b - a) * fr);
    const float u = vfSat(0.5f * y);
    line[w & kMask] = static_cast<int16_t>(u * kLineScale);  // truncation: a tail decays to 0
    pk = maxAbs(pk, u);
    out[i] += u * gain;
  }
  c.d = kRamp ? c.dT : d;
  c.w = static_cast<uint16_t>(w & kMask);
  const float held = c.peak * kCombHold;
  c.peak = 2.f * pk > held ? 2.f * pk : held;
}

// Position p = log2(hz / 20) / 9.45 x 4 over A E I O U (the cutoff knob's octaves: 0 A, 127 U).
MT_HOT void VoiceFilter::setFormant(float hz, float r) {
#ifdef PIO_UNIT_TESTING
  ++formantComputes;
#endif
  Formant& f = st_.formant;
  float p = hz > 20.f ? fastLog2(hz * (1.f / 20.f)) * (4.f / 9.451211f) : 0.f;
  p = p > 4.f ? 4.f : p;
  const int i = p >= 4.f ? 3 : static_cast<int>(p);
  const float t = p - static_cast<float>(i);
  const float k = fastRcp(2.f + 18.f * r);  // q 2 .. 20
  f.k = k;
  for (int j = 0; j < 3; ++j) {
    const float l = kVowels[i][j] + (kVowels[i + 1][j] - kVowels[i][j]) * t;
    const float g = fastTan(fastExp2(l) * (3.14159265f / kSynthRate));  // F3 <= 3010 Hz
    const float a1 = fastRcp(1.f + g * (g + k));
    f.a[j][0] = a1;
    f.a[j][1] = g * a1;
    f.a[j][2] = g * g * a1;
  }
}

template <bool kIn>
MT_INLINE void VoiceFilter::formantLoop(const float* __restrict in, float* __restrict out, int n) {
  Formant& f = st_.formant;
  float p1 = f.ic[0][0], p2 = f.ic[0][1], q1 = f.ic[1][0], q2 = f.ic[1][1], r1 = f.ic[2][0], r2 = f.ic[2][1];
  const float pa1 = f.a[0][0], pa2 = f.a[0][1], pa3 = f.a[0][2];
  const float qa1 = f.a[1][0], qa2 = f.a[1][1], qa3 = f.a[1][2];
  const float ra1 = f.a[2][0], ra2 = f.a[2][1], ra3 = f.a[2][2];
  const float k = f.k;
  for (int i = 0; i < n; ++i) {
    const float x = kIn ? in[i] : 0.f;
    // Svf::processAdd per band, the band-pass output v1.
    float v3 = x - p2;
    const float pv1 = pa1 * p1 + pa2 * v3, pv2 = p2 + pa2 * p1 + pa3 * v3;
    p1 = 2.f * pv1 - p1;
    p2 = 2.f * pv2 - p2;
    v3 = x - q2;
    const float qv1 = qa1 * q1 + qa2 * v3, qv2 = q2 + qa2 * q1 + qa3 * v3;
    q1 = 2.f * qv1 - q1;
    q2 = 2.f * qv2 - q2;
    v3 = x - r2;
    const float rv1 = ra1 * r1 + ra2 * v3, rv2 = r2 + ra2 * r1 + ra3 * v3;
    r1 = 2.f * rv1 - r1;
    r2 = 2.f * rv2 - r2;
    out[i] += k * (pv1 + 0.5f * qv1 + 0.25f * rv1);
  }
  f.ic[0][0] = p1;
  f.ic[0][1] = p2;
  f.ic[1][0] = q1;
  f.ic[1][1] = q2;
  f.ic[2][0] = r1;
  f.ic[2][1] = r2;
}

// The per-sample loops are latency-bound (the M7 issues in order): each TPT one-pole is written
// y = G x + b s (b = 1 - G, b s off the chain) and s' = 2 y - s, one FMA on the loop's path instead
// of a subtract, a multiply and an add.
template <bool kIn, bool kFade>
MT_INLINE void VoiceFilter::ladderLoop(const float* __restrict in, float* __restrict out, int n) {
  Ladder& l = st_.ladder;
  float s0 = l.s[0], s1 = l.s[1], s2 = l.s[2], s3 = l.s[3], tail = tail_;
  const float G = l.G, b = 1.f - G, k = l.k, comp = l.comp, gi = l.gi;
  const float c0 = l.c[0], c1 = l.c[1], c2 = l.c[2], c3 = l.c[3];
  for (int i = 0; i < n; ++i) {
    const float x = kIn ? in[i] * comp : 0.f;
    // The linear zero-delay solve, as a tree (s3, the last state written, joins last).
    const float y4 = ((kIn ? gi * x : 0.f) + c0 * s0 + c1 * s1) + (c2 * s2 + c3 * s3);
    const float u = vfSat(x - k * y4);
    const float y1 = G * u + b * s0;
    s0 = 2.f * y1 - s0;
    const float y2 = G * y1 + b * s1;
    s1 = 2.f * y2 - s1;
    const float y3 = G * y2 + b * s2;
    s2 = 2.f * y3 - s2;
    const float y = G * y3 + b * s3;
    s3 = 2.f * y - s3;
    if (kFade) {
      tail *= kTailFade;
      out[i] += y * tail;
    } else {
      out[i] += y;
    }
  }
  l.s[0] = s0;
  l.s[1] = s1;
  l.s[2] = s2;
  l.s[3] = s3;
  tail_ = tail;
}

template <bool kIn, bool kFade>
MT_INLINE void VoiceFilter::ms20Loop(const float* __restrict in, float* __restrict out, int n) {
  Ms20& m = st_.ms20;
  float s0 = m.s[0], s1 = m.s[1], s2 = m.s[2], tail = tail_;
  const float G = m.G, K = m.K, a0 = m.a0, f2 = m.f2, b = m.b, KG = K * G;
  for (int i = 0; i < n; ++i) {
    const float x = kIn ? in[i] : 0.f;
    const float y1 = (kIn ? G * x : 0.f) + b * s0;  // low-pass 1
    s0 = 2.f * y1 - s0;
    const float u = vfSat(a0 * (y1 + (f2 * s1 - b * s2)));
    const float y = G * u + b * s1;  // low-pass 2: the output (the loop's K y over K)
    s1 = 2.f * y - s1;
    s2 += 2.f * (KG * y - G * s2);  // the feedback high-pass's low-pass state
    if (kFade) {
      tail *= kTailFade;
      out[i] += y * tail;
    } else {
      out[i] += y;
    }
  }
  m.s[0] = s0;
  m.s[1] = s1;
  m.s[2] = s2;
  tail_ = tail;
}

// Svf::processAdd's low-pass, its update lines as they are, plus the dirt: the asymmetric input clip,
// vfSat on the band-pass state (|ic1| <= 1: bounded at any Q) and the DC blocker after.
template <bool kIn, bool kFade>
MT_INLINE void VoiceFilter::sidLoop(const float* __restrict in, float* __restrict out, int n) {
  Sid& s = st_.sid;
  float ic1 = s.s[0], ic2 = s.s[1], dc = s.s[2], tail = tail_;
  const float a1 = s.a1, a2 = s.a2, a3 = s.a3;
  for (int i = 0; i < n; ++i) {
    float v0 = 0.f;
    if (kIn) {
      const float x = in[i];
      v0 = vfSat(x + kSidEven * x * x);
    }
    const float v3 = v0 - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = vfSat(2.f * v1 - ic1);
    ic2 = 2.f * v2 - ic2;
    dc += kSidDc * (v2 - dc);
    const float y = v2 - dc;
    if (kFade) {
      tail *= kTailFade;
      out[i] += y * tail;
    } else {
      out[i] += y;
    }
  }
  s.s[0] = ic1;
  s.s[1] = ic2;
  s.s[2] = dc;
  tail_ = tail;
}

MT_HOT void VoiceFilter::processAdd(const float* __restrict in, float* __restrict out, int n, int16_t* line) {
  if (kind_ <= FltKind::Hp) {
    st_.svf.processAdd(in, out, n);
    return;
  }
  if (kind_ == FltKind::Formant) {
    if (in)
      formantLoop<true>(in, out, n);
    else
      formantLoop<false>(nullptr, out, n);
    return;
  }
  if (kind_ == FltKind::Comb) {
    if (!line) return;
    Comb& c = st_.comb;
    const bool ramp = c.ready && c.d != c.dT;
    if (in)
      ramp ? combLoop<true, true>(in, out, n, line) : combLoop<true, false>(in, out, n, line);
    else
      ramp ? combLoop<false, true>(nullptr, out, n, line) : combLoop<false, false>(nullptr, out, n, line);
    return;
  }
  const bool ladder = kind_ == FltKind::Ladder, sid = kind_ == FltKind::Sid;
  if (in) {
    tail_ = 1;
    tailN_ = 0;
    if (ladder)
      ladderLoop<true, false>(in, out, n);
    else if (sid)
      sidLoop<true, false>(in, out, n);
    else
      ms20Loop<true, false>(in, out, n);
    return;
  }
  // A tail: fades when self-oscillating or past the cap (checked per block).
  const bool fade = osc_ || tailN_ >= kTailCap;
  if (tailN_ < kTailCap) tailN_ += static_cast<uint32_t>(n);
  if (ladder) {
    if (fade)
      ladderLoop<false, true>(nullptr, out, n);
    else
      ladderLoop<false, false>(nullptr, out, n);
  } else if (sid) {  // never self-oscillates: fades only past the cap
    if (fade)
      sidLoop<false, true>(nullptr, out, n);
    else
      sidLoop<false, false>(nullptr, out, n);
  } else {
    if (fade)
      ms20Loop<false, true>(nullptr, out, n);
    else
      ms20Loop<false, false>(nullptr, out, n);
  }
}

}  // namespace mt
