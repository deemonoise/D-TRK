#pragma once
// DRUM machines BDA, SDA, BDS, SDS, CPX, RSX, BDF, HHR: a separate engine (Drum2Voice) from the
// parametric DrumVoice. Pure C++17, no allocation; render is the hot path. Algorithms and licences:
// see the header of synth_drum2.cpp.
#include <stdint.h>
#include "hot.h"
#include "model.h"

namespace mt {

enum class Drum2Kind : uint8_t { Bda, Sda, Bds, Sds, Cpx, Rsx, Bdf, Hhr, Count };
static_assert(static_cast<int>(DrumMachine::Count) - kDrum2First == static_cast<int>(Drum2Kind::Count),
              "DrumMachine BDA..HHR = Drum2Kind");
// DRUM machine (>= kDrum2First) -> engine kind.
inline Drum2Kind drum2Kind(uint8_t machine) { return static_cast<Drum2Kind>(machine - kDrum2First); }

struct Drum2Params {  // control rate
  float hz = 50;        // machine base frequency x 2^((pitch - 60) / 12); CPX / HHR: a multiplier
  float mac[5] = {};    // the five knobs (DECAY .. CONTOUR), 0..1
};

// Base frequency at C-4: BDA 55, SDA 200, BDS 50, SDS 180, CPX 1 (filter multiplier), RSX 470, BDF 45,
// HHR 1 (frequency multiplier).
float drum2BaseHz(Drum2Kind k);
// CPX: start of clap i (0..3) of pattern p (0..15) at SPREAD s (0..1), ms after the trigger.
float drum2ClapBurstMs(int p, float spread, int i);

class Drum2Voice {
 public:
  // accent = velocity / 127. keepTail: choke, the voice's last output fades out over ~2 ms under the
  // new hit.
  void trigger(Drum2Kind k, float accent, bool keepTail);
  // Targets for the next span samples (coefficients are set here, envelopes run per sample).
  void control(const Drum2Params& p, int span);
  // out += amp x sound.
  void render(float* __restrict out, int n, float amp);
  // Below -80 dB, no claps pending, choke tail over (also before the first trigger).
  bool done() const;

  struct Svf {  // TPT state-variable filter
    float g, r, h, s1, s2;
  };
  struct OnePole {  // TPT one-pole (low / high pass)
    float g, gi, s;
  };

 private:
  struct Bda {
    int pulseLeft, fmPulseLeft;
    float pulse, pulseH, pulseLp, fmPulseLp, retrig, lpOut, toneLp;
    float f0, q, scale, toneF, leak, atk, self, gain;
    Svf res;
  };
  struct Sda {
    int pulseLeft;
    float pulse, pulseH, pulseLp, noiseEnv;
    float gain[5], leak, snappy, noiseK;
    Svf res[5], noise;
  };
  struct Bds {
    int bodyPw, fmPw;
    float phase, phaseNoise, fm, fmLp, bodyEnv, bodyEnvLp, trEnv, trEnvLp, toneLp, dc;
    float clickLp, clickHp, noiseLp, noiseHp;
    float f0, dirt, drive, fmAmt, fmK, bodyK, trK, toneF, trLevel;
    Svf click;
  };
  struct Sds {
    int hold;
    float phase[2], drumAmp, snareAmp, fm, dc;
    float f0, fmAmt, drumK, snareK, fmK, drumLevel, snareLevel;
    OnePole drumLp, snareHp;
    Svf snareLp;
  };
  struct Cpx {
    int timer[5];  // samples to claps 0..3 and the tail, 0 = fired
    float e1, e2, a1, a2, amp[4];
    bool att2;
    float k1, k2, att2K, gain;
    Svf bp1, bp2;
  };
  struct Rsx {
    int pulseLeft;
    uint32_t oscPh, oscInc;
    float pulse, pulseH, pulseLp, env, envK, base, noise, gain;
    Svf res[3], hp;
  };
  struct Bdf {
    uint32_t ph[2];
    float prev0, prev1, ampEnv, modEnv, freqEnv;
    float ampK, modK, freqK, fb, carHz, sweepHz, ratio, index, gain;
  };
  struct Hhr {
    float ph[6], inc[6];
    float env, noiseClock, noiseSample;
    float envK, cutK, noiseF, noisiness;
    uint8_t pairs;
    Svf bp, hp;
  };
  union State {
    Bda bda;
    Sda sda;
    Bds bds;
    Sds sds;
    Cpx cpx;
    Rsx rsx;
    Bdf bdf;
    Hhr hhr;
  };

  void renderBda(float* o, int n);
  void renderSda(float* o, int n);
  void renderBds(float* o, int n);
  void renderSds(float* o, int n);
  void renderCpx(float* o, int n);
  void renderRsx(float* o, int n);
  void renderBdf(float* o, int n);
  void renderHhr(float* o, int n);
  float rnd();  // uniform 0..1

  State s_;
  Drum2Kind kind_ = Drum2Kind::Bda;
  bool fresh_ = false;    // triggered, nothing rendered yet
  bool pending_ = false;  // triggered, the first control() latches the hit's parameters
  bool live_ = false;     // triggered at least once
  float accent_ = 0;
  uint32_t rng_ = 0;
  float last_ = 0, tailV_ = 0;  // last output (amp included); choke tail
  float pk_ = 0;                // decaying peak of the sound (done)
};

}  // namespace mt
