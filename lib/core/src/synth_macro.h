// Derived from the macro oscillator in github.com/pichenettes/eurorack, MIT License: a float
// re-implementation at 44.1 kHz. The vowel and consonant formant records (synth_macro.cpp) are its
// data, verbatim.
//
// Copyright 2012 Emilie Gillet.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
#pragma once
#include <stdint.h>
#include "hot.h"
#include "model.h"

namespace mt {

// Control-rate inputs of a MACRO voice (Synth::controlMacro).
struct MacroParams {
  uint8_t model = 0;   // MacroModel
  float hz = 220;
  float timbre = 0.5f;  // 0..1
  float color = 0.5f;   // 0..1
};

// One macro oscillator: a model (MacroModel) shaped by TIMBRE and COLOR. Trivially copyable (lives in
// the voice-engine union). Output about as loud as a SYNTH saw at amp 1, no DC; render() adds into out.
class MacroVoice {
 public:
  // Phases, filter state and ramps from zero (not legato). seed: the note's random choices (VOWEL's
  // consonant and its noise): the same seed renders the same note.
  void trigger(uint32_t seed = 0);
  // Next control period of n samples: TIMBRE and COLOR ramp to p over it. A model change (or the first
  // control after trigger) starts the model's state from zero.
  void control(const MacroParams& p, int n);
  void render(float* out, int n, float amp);

 private:
  // One loop per model or model pair (no per-sample switch); t_ / c_ ramp per sample.
  void renderSawPlus(float* out, int n, float g);
  void renderSawSqr(float* out, int n, float g);
  void renderMorph(float* out, int n, float g);
  void renderFold(float* out, int n, float g);
  void renderVosim(float* out, int n, float g);
  void renderVowel(float* out, int n, float g);
  void renderFm(float* out, int n, float g, bool fb);
  void controlVowel(float t, float c, int n);

  MacroParams p_;
  float t_ = 0, tStep_ = 0, c_ = 0, cStep_ = 0;
  bool snap_ = true;    // next control() jumps to its values (after trigger)
  bool fresh_ = true;   // the next render starts the model's state (edges, ADAA)
  uint8_t model_ = 0xFF;  // the model the state belongs to (0xFF: none, after trigger)
  float dt_ = 0, inv_ = 0;  // phase step per sample, 1 / dt
  float note_ = 57;         // MIDI note of hz (control rate)
  float ph_ = 0;            // main phase 0..1
  uint32_t rng_ = 1;        // xorshift32, from the trigger's seed

  // Per-model state: only one model plays at a time.
  struct Analog {  // SAW+, SAW/SQR, MORPH
    float prev, corr;  // PolyBLEP: the pending sample, the correction due for the next
    float e[2];        // moving edges of the last sample (SAW+ notch; SAW/SQR saw, square; MORPH square)
    float lp, lpK, fuzzOff, fuzzDc;  // MORPH: low-pass, its coefficient, overdrive offset, its DC
  };
  struct Fold {
    float lim;            // pitch limit of the amount (0..1)
    float u1, F1, s1;     // triangle fold ADAA input / antiderivative, last sine fold sample
  };
  struct Formant {  // VOSIM (2 formants), VOWEL (3)
    float ph[3], inc[3], amp[3];
    float dc;             // tracked DC
    int32_t consonant;    // VOWEL: samples of consonant left
    uint8_t phon;         // VOWEL: the consonant (0..7)
    uint8_t noisy;        // VOWEL: this control period is a noisy consonant's
  };
  struct Fm {  // FM, FB FM
    float ph, dt;         // modulator
    float depth;          // index attenuation (pitch x ratio)
    float prev;           // FB FM: last output
    float dc, kdc;        // FB FM: tracked DC, the tracker's rate
  };
  union {
    Analog an;
    Fold fd;
    Formant fo;
    Fm fm;
  } s_;
};

}  // namespace mt
