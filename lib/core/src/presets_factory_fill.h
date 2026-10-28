#pragma once
// Internal to the factory preset files (presets_factory*.cpp): the tables and the fill helpers.
#include <stdio.h>
#include "presets_factory.h"

namespace mt {

// One instrument type's presets, in browser order.
struct FactoryTable {
  const FactoryPreset* presets;
  int count;
};
extern const FactoryTable kFactoryKit, kFactoryChip, kFactoryFm, kFactoryDrum, kFactorySynth, kFactoryString,
    kFactoryLofi;

namespace pf {

// Times: envTimeMs (40 ~ 18 ms, 60 ~ 80 ms, 80 ~ 330 ms, 100 ~ 1.4 s); cutoff: cutoffHz
// (40 ~ 160 Hz, 70 ~ 740 Hz, 90 ~ 2.1 kHz, 110 ~ 5.8 kHz). Starting points, tuned by ear.
using I = Instrument;
constexpr uint8_t W(Wave w) { return static_cast<uint8_t>(w); }
constexpr uint8_t F(FltMode f) { return static_cast<uint8_t>(f); }
constexpr uint8_t D(LfoDest d) { return static_cast<uint8_t>(d); }
constexpr uint8_t Wt(int n) { return static_cast<uint8_t>(static_cast<int>(Wave::Wt1) + n); }  // wavetables.cpp
inline void fm(I& m, FmMachine mc) { fmSetMachine(m, static_cast<uint8_t>(mc)); }
inline void dr(I& m, DrumMachine mc) { drumSetMachine(m, static_cast<uint8_t>(mc)); }
// DECAY, COLOR, SHAPE, SWEEP, CONTOUR.
inline void mac(I& m, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
  m.macro[0] = a; m.macro[1] = b; m.macro[2] = c; m.macro[3] = d; m.macro[4] = e;
}

constexpr uint8_t LW(LfoWave w) { return static_cast<uint8_t>(w); }
// SYNTH: type with its default macros, oscillators 1, 2 and their tables (built-in "*NAME" or nullptr).
inline void sy(I& m, SynOsc o1, const char* wt1, SynOsc o2, const char* wt2) {
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(o1);
  m.synOsc[1] = static_cast<uint8_t>(o2);
  snprintf(m.synWt[0], sizeof(m.synWt[0]), "%s", wt1 ? wt1 : "");
  snprintf(m.synWt[1], sizeof(m.synWt[1]), "%s", wt2 ? wt2 : "");
}
// LP filter: cutoff, resonance, env amount, env decay.
inline void lp(I& m, uint8_t cut, uint8_t res, int8_t env, uint8_t dec) {
  m.fltMode = F(FltMode::Lp); m.cutoff = cut; m.reso = res; m.fenv = env; m.fDec = dec;
}

// Helpers for the library below.
inline void env(I& m, uint8_t a, uint8_t d, uint8_t s, uint8_t r) { m.attack = a; m.decay = d; m.sustain = s; m.release = r; }
inline void hp(I& m, uint8_t cut, uint8_t res) { m.fltMode = F(FltMode::Hp); m.cutoff = cut; m.reso = res; }
inline void bp(I& m, uint8_t cut, uint8_t res) { m.fltMode = F(FltMode::Bp); m.cutoff = cut; m.reso = res; }
inline void lfo1(I& m, LfoWave w, LfoDest d, uint8_t rate, int8_t depth) {
  m.lfoWave = LW(w); m.lfoDest = D(d); m.lfoRate = rate; m.lfoDepth = depth;
}
inline void lfoN(I& m, int i, LfoWave w, LfoDest d, uint8_t rate, int8_t depth) {  // LFO 2..4 (i = 1..3)
  LfoCfg& l = m.lfo[i - 1];
  l.wave = LW(w); l.dest = D(d); l.rate = rate; l.depth = depth; l.sync = 0;
}
inline void fx(I& m, uint8_t send, uint8_t rsend) { m.send = send; m.rsend = rsend; }
// STRING: type with its defaults, the model, DAMP (T60 0.05 s x 200^(v/127)), BRIGHT, STRUCT, POS, EXC (0 = click).
inline void st(I& m, StringModel md, uint8_t damp, uint8_t bright, uint8_t structure, uint8_t pos, uint8_t exc) {
  m.machine = static_cast<uint8_t>(md);
  instrSetType(m, InstrType::String);
  m.macro[kMacDamp] = damp; m.macro[kMacBright] = bright; m.macro[kMacStruct] = structure;
  m.macro[kMacPos] = pos; m.macro[kMacExc] = exc;
}

// LOFI BYTE: library formula (bytebeat_lib.cpp) with its default A B C, then tl() sets TONE LOOP
// (mac() all five); APC: WIDTH TONE DRIFT SMOOTH (slot 5 stays 0). A held gate: the formula runs as
// long as the note.
inline void bb(I& m, uint8_t formula) {
  m.lofiFormula = formula;
  lofiSetMachine(m, static_cast<uint8_t>(LofiMachine::Byte));
  env(m, 0, 60, 127, 40);
}
inline void tl(I& m, uint8_t tone, uint8_t loop) { m.macro[3] = tone; m.macro[4] = loop; }
inline void apc(I& m) {
  lofiSetMachine(m, static_cast<uint8_t>(LofiMachine::Apc));
  env(m, 0, 60, 127, 40);
}

}  // namespace pf
}  // namespace mt
