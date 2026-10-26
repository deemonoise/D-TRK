#pragma once
#include <stdint.h>
#include "fastmath.h"
#include "hot.h"
#include "synth_filter.h"
#include "synth_lines.h"

namespace mt {

// A voice's filter: FltMode minus Off, in the same order (FltKind = FltMode - 1).
enum class FltKind : uint8_t { Lp, Bp, Hp, Ladder, Ms20, Comb, Formant, Sid };
// LP / BP / HP pass straight to Svf::set by value.
static_assert(static_cast<int>(FltKind::Lp) == static_cast<int>(Svf::Mode::Lp) &&
                  static_cast<int>(FltKind::Bp) == static_cast<int>(Svf::Mode::Bp) &&
                  static_cast<int>(FltKind::Hp) == static_cast<int>(Svf::Mode::Hp),
              "FltKind LP / BP / HP must match Svf::Mode");

// The filters' saturator: tanh-like (slope 1 at 0, odd, monotonic), a clamped cubic reaching +-1 at
// +-1.5 with zero slope. 5 ops: fastTanh (exp2 polynomial + three Newton steps) would double the
// LADDER loop. |sat(x)| <= 1 bounds every loop it sits in.
MT_INLINE float vfSat(float x) {
  x = fclamp(x, -1.5f, 1.5f);  // VMINNM / VMAXNM on the Teensy: no compare, VMRS, branch
  return x - ((4.f / 27.f) * x) * (x * x);  // two independent products: a shorter chain
}

// One filter per voice, the type picked per instrument: a mode plus a union of the per-mode states
// (only one lives at a time, so a Voice pays for the largest). LP / BP / HP are today's Svf,
// bit-exact. set() at control rate, processAdd() per block; one loop per mode (no per-sample switch).
// line: the voice's delay line (voiceLine(slot), kLineLen samples), read and written by COMB only;
// COMB is silent without one.
class VoiceFilter {
 public:
  // hz: the cutoff (clamped to 20 Hz .. 0.45 x the rate; COMB's pitch; FORMANT's vowel: 20 Hz A ..
  // 14 kHz U, the cutoff knob's range); reso127: the resonance 0..127 (the new modes map it
  // themselves); q: resoQ(reso127), cached by Synth, used by LP / BP / HP.
  // Switching to another state family (LP / BP / HP share the Svf state, as Svf::set does) resets
  // the state.
  void set(FltKind k, float hz, float reso127, float q);
  // Clears the state; COMB clears its line before the next read (a new note on the slot does not
  // hear the last one's).
  void reset();
  // Still ringing with no input: state above -80 dB of full scale, as Svf (Synth keeps the voice
  // for it). A LADDER / MS20 / SID tail ends when its fade does.
  MT_INLINE bool ringing() const {
    switch (kind_) {
      case FltKind::Ladder: return tail_ >= kTailOff && above(st_.ladder.s, 4);
      case FltKind::Ms20: return tail_ >= kTailOff && above(st_.ms20.s, 3);
      case FltKind::Comb: return st_.comb.peak > 1e-4f;
      case FltKind::Formant: return above(&st_.formant.ic[0][0], 6);
      case FltKind::Sid: return tail_ >= kTailOff && above(st_.sid.s, 3);
      default: return st_.svf.ringing();
    }
  }
  // out[i] += filter(in[i]), in = nullptr for silence (a ringing tail). Synth applies the amp
  // envelope before the filter, so a self-oscillating LADDER / MS20 would ring forever on a released
  // voice: its tail fades out (-60 dB in 50 ms) at once, any other LADDER / MS20 / SID tail after 2 s.
  // LP / BP / HP tails do not fade (bit-exact to Svf).
  void processAdd(const float* __restrict in, float* __restrict out, int n, int16_t* line);

#ifdef PIO_UNIT_TESTING
  static uint32_t formantComputes;  // setFormant() calls (tests: set() caches)
#endif

 private:
  // 4-pole (24 dB/oct) Moog-style ladder: four TPT one-poles, the feedback solved without a unit
  // delay (Zavalishin, "The Art of VA Filter Design", ch. 5), vfSat on the loop's input.
  struct Ladder {
    float s[4];               // one-pole states
    float G, k, comp;         // g / (1 + g), feedback, input gain (passband compensation)
    float gi, c[4];           // y4 = gi * x + sum c[i] s[i]: the zero-delay estimate of the output
  };
  // 2-pole (12 dB/oct) Korg-35 Sallen-Key low-pass, as in the MS-20: two TPT one-poles in series, the
  // output fed back with gain K through a TPT one-pole high-pass into the second one's input, solved
  // without a unit delay (Zavalishin ch. 5), vfSat on that input. Linear: 1 / (s^2 + (2 - K) s + 1),
  // unity at DC for every K.
  struct Ms20 {
    float s[3];               // states: low-pass 1, low-pass 2, the feedback high-pass
    float G, K, a0, f2, b;    // a0 = 1 / (1 - K G (1 - G)), f2 = K (1 - G)^2, b = 1 - G
  };
  // Feedback comb y = x + fb s[n - d] on the voice's int16 line, s = 2 vfSat(y / 2): saturated before
  // the write, so |y| <= 1 + 0.97 x 2 < 3 keeps s off vfSat's clamp for |x| <= 1 and the line (s x
  // 16383) never hard-clips. d = rate / hz, read with linear interpolation (slight HF damping), held
  // at kLineLen - 1 (86 Hz) below, ramped over the block on a change (no zipper). Out s / (1 + fb):
  // unity at r 0, a tuned saw stays near full scale at r 1 (peak gain 1 / (1 - fb) before saturation).
  struct Comb {
    float d, dT;      // delay now / target, samples
    float fb, gain;   // feedback 0.97 r; output 2 / (1 + fb) (s = 2 u)
    float peak;       // |s| per block, held over the line's length (ringing)
    uint16_t w;       // write index
    bool ready;       // line cleared (reset() / a switch to COMB clear it on the next processAdd)
  };
  // Vowel formants: three Svf band-passes (Svf's math, one shared q, no mode / cache fields) in
  // parallel, F1 F2 F3 at 1, 0.5, 0.25, each at unity peak (x k).
  struct Formant {
    float ic[3][2];  // per band: Svf's ic1, ic2
    float a[3][3];   // per band: Svf's a1, a2, a3
    float k;         // 1 / q
  };
  // SID 6581-style: Svf's TPT state-variable low-pass (12 dB/oct, as the chip), Q capped at 2 (the chip
  // does not self-oscillate), the cutoff mapped into 200 Hz .. 12 kHz (setSid). Dirty: an asymmetric
  // soft clip on the input (x + 0.15 x^2, then vfSat: even harmonics), vfSat on the band-pass state;
  // a ~3.5 Hz DC blocker takes the x^2 term's offset back out. Our own model: no reSID code or data.
  struct Sid {
    float s[3];          // Svf's ic1 (band-pass), ic2 (low-pass), the DC blocker's estimate
    float a1, a2, a3;    // Svf's: a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2
  };
  union State {
    Svf svf;  // LP / BP / HP
    Ladder ladder;
    Ms20 ms20;
    Comb comb;
    Formant formant;
    Sid sid;
    State() : svf() {}
  };

  // Any |s[i]| above -80 dB of full scale.
  MT_INLINE static bool above(const float* s, int n) {
    constexpr float kOff = 1e-4f;
    for (int i = 0; i < n; ++i)
      if (s[i] > kOff || s[i] < -kOff) return true;
    return false;
  }
  static constexpr float kTailOff = 1e-3f;  // the fade's end (-60 dB)
  void setLadder(float hz, float r);
  void setMs20(float hz, float r);
  // One loop per case, no per-sample branch: kIn (input, else a tail), kFade (a fading tail).
  template <bool kIn, bool kFade>
  void ladderLoop(const float* __restrict in, float* __restrict out, int n);
  template <bool kIn, bool kFade>
  void ms20Loop(const float* __restrict in, float* __restrict out, int n);
  void setSid(float hz, float r);
  template <bool kIn, bool kFade>
  void sidLoop(const float* __restrict in, float* __restrict out, int n);
  void setComb(float hz, float r);
  void setFormant(float hz, float r);
  template <bool kIn>
  void formantLoop(const float* __restrict in, float* __restrict out, int n);
  template <bool kIn, bool kRamp>
  void combLoop(const float* __restrict in, float* __restrict out, int n, int16_t* __restrict line);

  FltKind kind_ = FltKind::Lp;
  bool osc_ = false;          // LADDER / MS20 over the self-oscillation threshold (never SID)
  float hz_ = -1, res_ = -1;  // last set() arguments of the non-Svf modes (Svf caches its own)
  float tail_ = 1;            // the tail fade's gain, 1 while there is input
  uint32_t tailN_ = 0;        // samples since the tail started
  State st_;
};

}  // namespace mt
