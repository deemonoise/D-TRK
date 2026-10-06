#include "synth_drum_machines.h"
#include <math.h>
#include "synth_osc.h"

namespace mt {
namespace {

float u(float v) { return v <= 0 ? 0 : (v >= 127 ? 1 : v * (1.f / 127.f)); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float expMap(float lo, float hi, float t) { return lo * powf(hi / lo, t); }

// TR-808 metal oscillators; the 909 set is an inharmonic stand-in (the originals are samples),
// design table (263 / 400 / 421 / 474 / 587 / 845 Hz) x 2.
constexpr float kMetal808[kDrumMetal] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
constexpr float kMetal909[kDrumMetal] = {526.f, 800.f, 842.f, 948.f, 1174.f, 1690.f};

struct Names {
  const char* machine;
  const char* mac[kFmMacros];
};
constexpr Names kNames[static_cast<int>(DrumMachine::Count)] = {
    {"BD8", {"DECAY", "TONE", "DRIVE", "SWEEP", "TIME"}},
    {"SD8", {"DECAY", "TONE", "SNAPPY", "SWEEP", "N.DEC"}},
    {"TOM8", {"DECAY", "NOISE", "DRIVE", "SWEEP", "TIME"}},
    {"CP8", {"DECAY", "FREQ", "SPREAD", "Q", "TAIL"}},
    {"RS8", {"DECAY", "TONE", "NOISE", "SWEEP", ""}},
    {"CL8", {"DECAY", "TONE", "DRIVE", "SWEEP", ""}},
    {"CB8", {"DECAY", "FREQ", "BAL", "ACCENT", ""}},
    {"HH8", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"CY8", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"BD9", {"DECAY", "ATTACK", "DRIVE", "SWEEP", "TIME"}},
    {"SD9", {"DECAY", "TONE", "SNAPPY", "SWEEP", "N.DEC"}},
    {"TOM9", {"DECAY", "NOISE", "DRIVE", "SWEEP", "TIME"}},
    {"CP9", {"DECAY", "FREQ", "SPREAD", "Q", "TAIL"}},
    {"RS9", {"DECAY", "TONE", "NOISE", "SWEEP", ""}},
    {"HH9", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"CY9", {"DECAY", "HP", "RIDE/CR", "SPREAD", "N.DEC"}},
};

// Macros as 0..1, DECAY also as ms (fmDecayMs range, fractional).
struct Mac {
  float dec, dMs, col, shp, swp, con;
};

// spread stretches the set around its centre: lowest x 1 / spread, highest x spread.
void metal(DrumParams& o, const float* set, float scale, float spread) {
  for (int k = 0; k < kDrumMetal; ++k) o.metalHz[k] = set[k] * scale * powf(spread, (k - 2.5f) / 2.5f);
}

void kick(DrumParams& o, const Mac& m, float hz, float click, float clickMs, float sweep, float tLo,
          float tHi) {
  o.toneHz[0] = hz;
  o.toneLvl[0] = 1;
  o.toneMs = m.dMs;
  o.click = click;
  o.clickMs = clickMs;
  o.drive = m.shp;
  o.pitchEnv = sweep * m.swp;
  o.pitchMs = expMap(tLo, tHi, m.con);
}

void tom(DrumParams& o, const Mac& m, float hz, float noiseHz, float sweep) {
  o.toneHz[0] = hz;
  o.toneLvl[0] = 1;
  o.toneMs = m.dMs;
  o.noiseLvl = 0.3f * m.col;
  o.noiseMs = m.dMs * 0.3f;
  o.noiseMode = Svf::Mode::Lp;
  o.noiseHz = noiseHz;
  o.drive = m.shp;
  o.pitchEnv = sweep * m.swp;
  o.pitchMs = expMap(10, 200, m.con);
}

void snare(DrumParams& o, const Mac& m, float lo, float hi, float noiseHz) {
  o.toneHz[0] = lo;
  o.toneHz[1] = hi;
  o.toneLvl[0] = 0.6f * (0.9f * (1 - m.col) + 0.1f);
  o.toneLvl[1] = 0.6f * (0.9f * m.col + 0.1f);
  o.toneMs = m.dMs;
  o.pitchEnv = 7.f * m.swp;
  o.pitchMs = 15;
  o.noiseLvl = m.shp;
  o.noiseMs = m.dMs * expMap(0.3f, 2.f, m.con);
  o.noiseMode = Svf::Mode::Hp;
  o.noiseHz = noiseHz;
}

void clap(DrumParams& o, const Mac& m, float lo, float hi, uint8_t bursts) {
  o.noiseLvl = 1;
  o.noiseMs = m.dMs;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = expMap(lo, hi, m.col);
  o.bursts = bursts;
  o.burstMs = lerp(6, 14, m.shp);
  o.noiseQ = expMap(1, 6, m.swp);
  o.tail = m.con;
}

void rim(DrumParams& o, const Mac& m, float lo, float hi, float decHi, float noiseMs, float noiseHz,
         float pitchMs) {
  o.toneHz[0] = lo;
  o.toneHz[1] = hi;
  o.toneLvl[0] = 0.7f * (1 - m.col) + 0.15f;
  o.toneLvl[1] = 0.7f * m.col + 0.15f;
  o.toneMs = expMap(10, decHi, m.dec);
  o.noiseLvl = 0.5f * m.shp;
  o.noiseMs = noiseMs;
  o.noiseHz = noiseHz;
  o.pitchEnv = 5.f * m.swp;
  o.pitchMs = pitchMs;
}

void hat(DrumParams& o, const Mac& m, const float* set, float scale, float decLo, float decHi,
         float hpLo, float hpHi, float bp1, float bp2) {
  metal(o, set, scale, expMap(0.8f, 1.25f, m.swp));
  o.metalLvl = 1.f - 0.7f * m.shp;
  o.metalMs = expMap(decLo, decHi, m.dec);
  o.metalBp1 = bp1;
  o.metalBp2 = bp2;
  o.metalHp = expMap(hpLo, hpHi, m.col);
  o.noiseLvl = m.shp;
  o.noiseMs = o.metalMs * expMap(0.3f, 1.5f, m.con);
  o.noiseMode = Svf::Mode::Hp;
  o.noiseHz = o.metalHp;
}

}  // namespace

const char* drumMachineName(uint8_t m) {
  return kNames[m < static_cast<int>(DrumMachine::Count) ? m : 0].machine;
}

const char* drumMacroName(uint8_t m, int k) {
  if (k < 0 || k >= kFmMacros) return "";
  return kNames[m < static_cast<int>(DrumMachine::Count) ? m : 0].mac[k];
}

void drumMachine(uint8_t machine, const float mac[kFmMacros], float pitch, DrumParams& o) {
  o = DrumParams();
  const float r = exp2f((pitch - 60.f) * (1.f / 12.f));
  Mac m;
  m.dec = u(mac[kMacDec]);
  m.dMs = expMap(5, 4000, m.dec);  // as fmDecayMs, fractional
  m.col = u(mac[kMacCol]);
  m.shp = u(mac[kMacShp]);
  m.swp = u(mac[kMacSwp]);
  m.con = u(mac[kMacCon]);
  if (machine >= static_cast<int>(DrumMachine::Count)) machine = 0;
  switch (static_cast<DrumMachine>(machine)) {
    case DrumMachine::Sd8: snare(o, m, 180 * r, 330 * r, 1800); break;
    case DrumMachine::Tom8: tom(o, m, 110 * r, 3000, 7); break;
    case DrumMachine::Cp8: clap(o, m, 600, 2500, 3); break;
    case DrumMachine::Rs8: rim(o, m, 500 * r, 1700 * r, 120, 5, 5000, 3); break;
    case DrumMachine::Cl8:
      o.toneHz[0] = 2500.f * r * exp2f((m.col - 0.5f) * 2.f);  // TONE: +-1 octave
      o.toneLvl[0] = 1;
      o.toneMs = expMap(10, 120, m.dec);
      o.drive = m.shp;
      o.pitchEnv = 3.f * m.swp;
      o.pitchMs = 2;
      break;
    case DrumMachine::Cb8:
      o.metalHz[0] = 540.f * r;
      o.metalHz[1] = 800.f * r;
      o.metalW[0] = 1.f - 0.8f * m.shp;
      o.metalW[1] = 0.2f + 0.8f * m.shp;
      for (int k = 2; k < kDrumMetal; ++k) o.metalW[k] = 0;  // two squares
      o.metalLvl = 1;
      o.metalMs = m.dMs;
      o.metalAccent = 2.f * m.swp;
      o.metalBp1 = expMap(1500, 4000, m.col);  // one band-pass
      o.metalQ = 3;
      break;
    case DrumMachine::Hh8: hat(o, m, kMetal808, r, 20, 2000, 4000, 12000, 3440, 7100); break;
    case DrumMachine::Cy8: hat(o, m, kMetal808, 0.7f * r, 300, 4000, 2000, 8000, 3000, 6000); break;
    case DrumMachine::Bd9: kick(o, m, 50 * r, m.col, 3, 24, 10, 80); break;
    case DrumMachine::Sd9: snare(o, m, 190 * r, 345 * r, 1000); break;
    case DrumMachine::Tom9: tom(o, m, 120 * r, 4000, 12); break;
    case DrumMachine::Cp9: clap(o, m, 800, 3000, 4); break;
    case DrumMachine::Rs9: rim(o, m, 1700 * r, 3400 * r, 80, 4, 6000, 2); break;
    case DrumMachine::Hh9: hat(o, m, kMetal909, r, 20, 2000, 6000, 14000, 6000, 9000); break;
    case DrumMachine::Cy9:
      hat(o, m, kMetal909, 0.6f * r, 400, 4000, 3000, 10000, 4000, 7000);
      // SHAPE: ride (metal, band-passed lower) -> crash (noise, brighter).
      o.noiseLvl = 0.2f + 0.6f * m.shp;
      o.metalLvl = 1.f - 0.6f * m.shp;
      o.metalBp1 = expMap(2500, 5000, m.shp);
      o.metalBp2 = o.metalBp1 * 1.8f;
      break;
    default: kick(o, m, 55 * r, 0.3f * m.col, 1.5f, 12, 5, 100); break;  // Bd8
  }
  // Everything below the Nyquist margin (high notes).
  const float lim = kSynthRate * 0.45f;
  for (float& h : o.toneHz) h = h < lim ? h : lim * 0.99f;
  for (float& h : o.metalHz) h = h < lim ? h : 0;  // a square above the limit drops out
  if (o.metalBp1 >= lim) o.metalBp1 = lim * 0.99f;
  if (o.metalBp2 >= lim) o.metalBp2 = lim * 0.99f;
  if (o.metalHp >= lim) o.metalHp = lim * 0.99f;
  if (o.noiseHz >= lim) o.noiseHz = lim * 0.99f;
}

}  // namespace mt
