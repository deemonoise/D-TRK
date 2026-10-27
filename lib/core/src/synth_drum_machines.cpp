#include "synth_drum_machines.h"
#include <math.h>
#include "fastmath.h"
#include "synth_osc.h"

namespace mt {
namespace {

float u(float v) { return v <= 0 ? 0 : (v >= 127 ? 1 : v * (1.f / 127.f)); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float expMap(float lo, float hi, float t) { return lo * fastPow(hi / lo, t); }

// TR-808 metal oscillators, measured on the hats and cymbal (the set explains 84 .. 87 % of their
// spectral lines, as odd harmonics). The 909 set is an inharmonic stand-in (the originals are 6-bit
// samples: no set explains their lines above chance), design table (263 / 400 / 421 / 474 / 587 /
// 845 Hz) x 2.
constexpr float kMetal808[kDrumMetal] = {348.2f, 458.5f, 564.f, 831.f, 845.2f, 982.2f};
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
    {"SD9", {"DECAY", "TUNE", "SNAPPY", "SWEEP", "N.DEC"}},
    {"TOM9", {"DECAY", "TUNE", "DRIVE", "SWEEP", "TIME"}},
    {"CP9", {"DECAY", "FREQ", "SPREAD", "Q", "TAIL"}},
    {"RS9", {"DECAY", "TONE", "NOISE", "SWEEP", ""}},
    {"HH9", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"CY9", {"DECAY", "TUNE", "RIDE/CR", "SPREAD", "N.DEC"}},
};

// Macros as 0..1, DECAY also as ms (fmDecayMs range, fractional).
struct Mac {
  float dec, dMs, col, shp, swp, con;
};

// spread stretches the set around its centre: lowest x 1 / spread, highest x spread.
void metal(DrumParams& o, const float* set, float scale, float spread) {
  for (int k = 0; k < kDrumMetal; ++k) o.metalHz[k] = set[k] * scale * fastPow(spread, (k - 2.5f) / 2.5f);
}

// -60 dB time (DrumParams) of a time constant tau, ms.
constexpr float kT60 = 6.9077553f;

// A knob measured at 0, 3, 7 and 10 (the 909 sample names): linear between, t 0..1.
float knob(const float y[4], float t) {
  if (t < 0.3f) return lerp(y[0], y[1], t * (1.f / 0.3f));
  if (t < 0.7f) return lerp(y[1], y[2], (t - 0.3f) * (1.f / 0.4f));
  return lerp(y[2], y[3], (t - 0.7f) * (1.f / 0.3f));
}

// TR-909 kick, measured on samples (knobs Tune / Attack / Decay at 0 / 3 / 7 / 10). One sine swept
// from ~250 Hz by one exponential (TIME = Tune: tau 8.7 .. 31 ms), full level until the pitch is near
// 90 Hz, then a -6 dB step; DECAY: tau 15 .. 96 ms after a hold. h3 -20, h5 -34 dB. A -16 dB DC
// step 1.3 ms ahead of the body; ATTACK = high-passed noise fading in ~30 ms.
void kick9(DrumParams& o, const Mac& m, float hz) {
  static const float kTune[4] = {8.7f, 11.5f, 19.f, 31.f};  // sweep tau, ms
  static const float kDec[4] = {15.f, 30.f, 58.f, 96.f};    // decay tau after the hold
  static const float kHold[4] = {55.f, 64.f, 68.f, 70.f};
  constexpr float kDelay = 1.3f;  // the click step ahead of the body
  const float tune = knob(kTune, m.con);
  o.toneHz[0] = hz;
  o.toneLvl[0] = 1;
  o.h3 = -0.1f;
  o.h5 = 0.02f;
  // SWEEP 60: +200 Hz at the click (250 Hz), so a little less where the body starts.
  o.sweepA = 8.6f * m.swp * fastExp(-1.45f * fastRcp(tune));
  o.sweepAMs = tune * kT60;
  o.fastLvl = 0.485f;  // the step: this part goes once the pitch is down near 90 Hz
  o.fastHoldMs = 1.2f * tune;
  o.fastMs = 10.f * kT60;
  o.slowLvl = 0.4f;
  o.holdMs = knob(kHold, m.dec);
  o.toneMs = knob(kDec, m.dec) * kT60;
  o.toneDelayMs = kDelay;
  o.clickStep = 0.12f;
  o.clickStepMs = kDelay;
  o.noiseLvl = 0.04f * (0.47f + 0.53f * m.col);  // ATTACK 0 keeps some (the real one's -45 dB)
  o.noiseMs = 12.f * kT60;
  o.noiseMode = Svf::Mode::Hp;
  o.noiseHz = 3000;
  o.drive = m.shp * m.shp * m.shp;
}

// TR-808 kick, measured: a near-sine at ~45 Hz, swept by +215 Hz (tau ~5 ms, TIME) and a slow
// +22 Hz (tau 90 ms); at short DECAY a faster fast part and +59 Hz, tau 20 ms (the samples settle
// lower, ~34 Hz, kept at 45 for the tuning). SWEEP scales both. Starts with a short positive spike,
// then the (inverted) sine rising ~3 dB over 25 ms; hold ~25 .. 60 ms to -1 dB, then tau 20 .. 230 ms
// (DECAY; the default ~150, the long samples' median). TONE: the spike and the click impulse.
void kick8(DrumParams& o, const Mac& m, float hz) {
  const float sw = m.swp * (1.f / 0.315f);  // SWEEP 40 = the measured sweep
  const float lng = m.dec < 0.7f ? m.dec * (1.f / 0.7f) : 1.f;
  constexpr float kLvl = 1.1f;
  o.toneHz[0] = hz;
  o.toneLvl[0] = -1;  // the first half-wave negative, after the spike
  o.h3 = -0.03f;
  o.sweepA = 5.1f * sw;
  o.sweepAMs = expMap(2.f, 12, m.con) * lerp(0.75f, 1, lng) * kT60;
  o.sweepB = lerp(59, 22, lng) * (1.f / 45.f) * sw;
  o.sweepBMs = lerp(20, 88, lng) * kT60;
  o.slowLvl = kLvl;
  o.fastLvl = -0.35f * kLvl;  // the rise
  o.fastMs = 10.f * kT60;
  o.holdMs = lerp(20, 37, m.dec);
  o.toneMs = expMap(24, 230, fastPow(m.dec > 1e-4f ? m.dec : 1e-4f, 0.6f)) * kT60;
  o.clickStep = 0.15f + 0.4f * m.col;
  o.clickStepMs = 0.7f;
  o.click = 0.3f * m.col;
  o.clickMs = 1.f;
  o.drive = m.shp * m.shp * m.shp;
}

// TR-808 toms and congas, measured on samples: one sine, no noise (NOISE adds some); the note picks
// the drum (60 = MT 127 Hz; LT 86, HT 171, congas 191 / 264 / 367 Hz). A first-cycle blip of +9.5
// semitones (SWEEP 40) decaying in ~0.3 cycles (TIME 50: tau 300 / hz ms, 3.5 ms on LT .. 0.8 on HC).
// Decay tau 62 ms at MT (DECAY 75), longer below (LT 83), shorter above (HT 55, MC 47, HC 42); a short
// hold, longer on low notes.
void tom8(DrumParams& o, const Mac& m, float r) {
  constexpr float kLvl = 1.07f;  // the soft clip's -1 dBFS
  const float hz = 127.f * r;
  o.toneHz[0] = hz;
  o.toneLvl[0] = 1;
  o.sweepA = 0.73f * m.swp * (1.f / 0.315f);
  o.sweepAMs = 290.f * fastRcp(hz) * fastExp2(2.5f * (m.con - 0.394f)) * kT60;
  const float tau = 62.f * fastPow(r, r < 1 ? -0.75f : -0.38f) * fastExp(1.5f * (m.dec - 0.59f));
  o.slowLvl = kLvl;
  o.toneMs = tau * kT60;
  const float hold = 4.f * fastPow(r, -3.f);
  o.holdMs = hold < 20 ? hold : 20;
  o.noiseLvl = 0.3f * kLvl * m.col * m.col;
  o.noiseMs = 0.3f * o.toneMs;
  o.noiseMode = Svf::Mode::Lp;
  o.noiseHz = 3000;
  o.drive = m.shp * m.shp * m.shp;
}

// TR-909 toms, measured on samples (LT / MT / HT, knobs Tune / Decay at 0 / 3 / 7 / 10); the note
// picks the drum (60 = MT; LT ~-4.4, HT ~+2.6 semitones), TUNE its base: MT 81 .. 152 Hz. A slow sweep
// of +5 semitones (SWEEP 50; tau 40 ms, TIME 50) and +1.5 in 3 ms, both partials along. Body tau
// 47 .. 85 ms after a short hold (DECAY), ~1.6 x on LT (notes below 60 ring longer); h3 -14, h5 -28
// dB. A lower partial x 0.64 at -6.5 dB, held at high DECAY, fading at low; an upper one x 1.78 from
// 0 dB, tau 25 ms; an LP-ed noise burst (~-26 dB, tau 17 ms).
void tom9(DrumParams& o, const Mac& m, float r) {
  static const float kTune[4] = {81.f, 97.f, 131.f, 152.f};  // MT base, Hz
  static const float kDec[4] = {47.f, 60.f, 77.f, 82.f};     // body tau after the hold, MT / HT
  static const float kHold[4] = {22.f, 35.f, 38.f, 36.f};
  static const float kLoRate[4] = {1.f / 65, 1.f / 500, 0, 0};       // the lower partial's own decay, 1 / ms
  constexpr float kLvl = 0.52f;
  const float hz = knob(kTune, m.col) * r;
  const float lt = r < 1 ? fastPow(r, -1.85f) : 1.f;  // LT (r 0.78): 1.6 x
  const float sw = m.swp * (1.f / 0.394f), time = fastExp2(2.5f * (m.con - 0.394f));
  const float lo = knob(kLoRate, m.dec);
  o.toneHz[0] = hz;
  o.toneLvl[0] = 1;
  o.toneHz[1] = 0.64f * hz;
  o.toneLvl[1] = 0.47f;
  o.toneDecMs[1] = lo > 0 ? kT60 * fastRcp(lo) : 0;
  o.toneHz[2] = 1.78f * hz;
  o.toneLvl[2] = 0.94f;
  o.toneDecMs[2] = 30.f * kT60;
  o.h3 = -0.19f;
  o.h5 = 0.04f;
  o.sweepA = 0.1f * sw;
  o.sweepAMs = 4.f * time * kT60;
  o.sweepB = 0.36f * sw;
  o.sweepBMs = 38.f * time * kT60;
  o.slowLvl = kLvl;
  o.holdMs = knob(kHold, m.dec);
  o.toneMs = knob(kDec, m.dec) * (lt < 2.5f ? lt : 2.5f) * kT60;
  o.noiseLvl = 0.25f * kLvl;
  o.noiseMs = 32.f * kT60;
  o.noiseMode = Svf::Mode::Lp;
  o.noiseHz = 4500;
  o.drive = m.shp * m.shp * m.shp;
}

// TR-808 snare, measured on samples: two sines, F1 165 Hz (tau 30 ms) and F2 1.95 x F1 (tau 9 ms),
// no pitch envelope; noise band-passed at ~2.8 kHz (-3 dB over ~2 .. 4 kHz), tau 43 ms (-60 dB at
// 0.3 s); tone and noise about equal in energy. DECAY scales both tone decays, TONE sets the F2 level
// (-20 .. 0 dB re F1 over the first 40 ms), SNAPPY the noise level, N.DEC its decay (tau 14 .. 130
// ms), SWEEP a pitch drop (cubic: ~0 at the default 30, 16 semitones at 127).
void snare8(DrumParams& o, const Mac& m, float hz) {
  const float dec = fastExp(2.12f * (m.dec - 0.433f));  // 1 at the default DECAY
  constexpr float kLvl = 0.29f;
  o.toneHz[0] = hz;
  o.toneHz[1] = 1.95f * hz;
  o.toneLvl[0] = kLvl;
  // F2's start level for its spectral level re F1 over 3 .. 43 ms (its shorter decay: -13 dB there).
  o.toneLvl[1] = kLvl * 4.47f * fastExp2((-20.f + 20.f * fastPow(m.col > 1e-4f ? m.col : 1e-4f, 0.5f)) *
                                         (1.f / 6.0206f));
  o.toneDecMs[0] = 30.f * dec * kT60;
  o.toneDecMs[1] = 9.f * dec * kT60;
  o.pitchEnv = 16.f * m.swp * m.swp * m.swp;
  o.pitchMs = 4.f * kT60;
  o.noiseLvl = kLvl * 3.3f * m.shp * (1.f / 0.551f);  // SNAPPY 70: about the tone's energy
  o.noiseMs = 43.f * fastPow(3.f, 2.f * (m.con - 0.504f)) * kT60;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = 3000;
  o.noiseQ = 1.f;
}

// TR-909 snare, measured on samples (knobs Tune / Tone / Snappy at 0 / 3 / 7 / 10). Two sines, F2 =
// 1.455 x F1 at -7 .. -10 dB, one decay (tau ~28 ms, DECAY); TUNE: F1 120 .. 233 Hz; both swept
// by a fixed +260 Hz (tau 7 ms, SWEEP scales it). Noise broad and tilted up (a resonant low-pass,
// -3 dB over ~4 .. 8 kHz); SNAPPY: its level (-16 .. -6 dB re the tone); N.DEC (the 909's Tone): a
// ~50 ms plateau, then a fall that speeds up, -3 dB at 70 .. 130 ms. A short click.
void snare9(DrumParams& o, const Mac& m, float r) {
  static const float kTune[4] = {120.f, 146.f, 205.f, 233.f};  // F1, Hz
  static const float kF2[4] = {-6.5f, -6.5f, -8.7f, -9.4f};    // F2 re F1, dB (sample fits -7 .. -10)
  static const float kSnap[4] = {-40.f, -16.f, -7.5f, -5.5f};  // noise re tone, dB
  static const float kFall[4] = {15.f, 38.f, 70.f, 78.f};      // ms from the plateau's end to -3 dB
  constexpr float kHold = 50, kAcc = 2;
  constexpr float kLvl = 0.7f;
  const float f1 = knob(kTune, m.col);
  o.toneHz[0] = f1 * r;
  o.toneHz[1] = 1.455f * f1 * r;
  o.toneLvl[0] = kLvl;
  o.toneLvl[1] = kLvl * fastExp2(knob(kF2, m.col) * (1.f / 6.0206f));
  o.toneMs = 28.f * fastExp(2.12f * (m.dec - 0.433f)) * kT60;
  o.sweepA = 260.f * m.swp * (1.f / 0.236f) * fastRcp(f1);  // Hz at the default SWEEP, any TUNE
  o.sweepAMs = 7.f * kT60;
  o.click = 0.25f;
  o.clickMs = 3;
  o.noiseLvl = m.shp > 0 ? kLvl * fastExp2(knob(kSnap, m.shp) * (1.f / 6.0206f)) : 0;
  // After the plateau the fall is b (2 acc s + s^2) dB at s ms: -3 dB kFall ms in, -60 dB time of
  // its start rate 30 / (acc b).
  const float s3 = knob(kFall, m.con), b = 3.f * fastRcp(s3 * (s3 + 2.f * kAcc));
  o.noiseHoldMs = kHold;
  o.noiseAccMs = kAcc;
  o.noiseMs = 30.f * fastRcp(kAcc * b);
  o.noiseMode = Svf::Mode::Lp;
  o.noiseHz = 9000;
  o.noiseQ = 1.2f;
  o.noiseHpHz = 2000;
}

// Macros measured around their default (64): t - kMid, -0.5 .. 0.5.
constexpr float kMid = 64.f / 127.f;

// dB to an amplitude.
float dbAmp(float d) { return fastExp2(d * (1.f / 6.0206f)); }

// TR-808 / 909 clap, measured on samples: three noise hits (SPREAD scales their spacing), each fading
// with its own tau, band-passed (FREQ: +-1 octave, Q: x 0.5 .. 2) with some broadband noise alongside;
// then the tail at TAIL x its level (0 .. 2 x), at a lower q, a fall that slows (start tau, then start
// rate / (1 + t / bend), down to 1 / 8); DECAY scales both x 0.35 .. 2.8. The filter q's are set so the
// analysis' reading (1/6-octave smoothed spectra of a few noise hits) lands on the samples'.
struct ClapSpec {
  float hz, q, tailQ, hit[3], tau, tail, tailTau, bend, wide, lvl;
};
void clap(DrumParams& o, const Mac& m, const ClapSpec& c) {
  const float sp = fastExp2(1.2f * (m.shp - kMid)), dec = fastExp2(3.f * (m.dec - kMid));
  const float q = fastExp2(2.f * (m.swp - kMid));
  o.noiseLvl = c.lvl;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = c.hz * fastExp2(2.f * (m.col - kMid));
  o.noiseQ = c.q * q;
  o.tailQ = c.tailQ * q;
  o.bursts = 3;
  o.burstMs = c.hit[0] * sp;
  for (int k = 0; k < 3; ++k) o.burstLenMs[k] = c.hit[k] * sp;
  o.burstTauMs = c.tau;
  o.noiseWide = c.wide;
  o.noiseHoldMs = (c.hit[0] + c.hit[1] + c.hit[2]) * sp;  // the tail's slowing runs from its start
  const float t = c.tail * m.con * (1.f / kMid);
  o.tail = t < 2 ? t : 2;
  o.noiseMs = c.tailTau * dec * kT60;
  o.noiseAccMs = -c.bend * dec;
}
// 808: hits 8.5 / 10.5 / 9.5 ms apart (the samples' onsets), tau ~4 ms, 1.1 kHz q ~5 read; the tail
// from 28.5 ms at the hits' peak level, q ~3.5 read; its mean envelope as the samples' within ~3 dB to
// 1 s (they are gated at ~1.2 s, at about -45 dB).
constexpr ClapSpec kClap8 = {1100, 6, 1.6f, {8.5f, 10.5f, 9.5f}, 4, 1.f, 33, 60, 0.22f, 1.32f};
// 909: hits 11 / 12.5 / 9 ms apart, tau ~3.3 ms, ~1.0 kHz; the tail from 32.5 ms, its mean envelope as
// the samples' (to their end at ~0.4 s).
constexpr ClapSpec kClap9 = {1000, 5, 1.8f, {11, 12.5f, 9}, 3.3f, 0.62f, 14, 20, 0.22f, 1.95f};

// TR-808 rimshot, measured on samples: a 448 Hz sine from 1.5 ms, at its peak ~4 ms in, then tau 4 ms
// (DECAY: x 0.5 .. 2), its pitch +5.5 % at the start falling in tau 4 ms (SWEEP: 0 .. 11 %), its 2nd
// harmonic (900 Hz) at -12 dB; a 1.75 kHz partial loud at the hit (+6 dB over the sine's peak), tau 2
// ms (TONE: both partials -13 .. +13 dB); a click of noise band-passed at ~5.5 kHz, -10 dB (NOISE: -15
// .. +15 dB).
void rim8(DrumParams& o, const Mac& m, float r) {
  constexpr float kLvl = 0.3f;
  const float dec = fastExp2(2.f * (m.dec - kMid)), up = dbAmp(26.f * (m.col - kMid));
  o.toneHz[0] = 448 * r;
  o.toneLvl[0] = kLvl;
  o.toneHz[2] = 896 * r;
  o.toneLvl[2] = kLvl * dbAmp(-12.f) * up;
  // After 1.5 ms (the stick's click first), e^(-t / 4) - e^(-t / 2): from 0 to its peak 0.25 at 2.8 ms,
  // then tau 4 ms.
  o.toneDelayMs = 1.5f;
  o.slowLvl = 4;
  o.toneMs = 3.6f * dec * kT60;
  o.fastLvl = -4;
  o.fastMs = 1.8f * dec * kT60;
  o.sweepA = 0.055f * m.swp * (1.f / kMid);
  o.sweepAMs = 4.f * kT60;
  // The 1.75 kHz partial: one square band-passed to near a sine, its own decay, from the hit.
  o.metalHz[0] = 1750 * r;
  for (int k = 1; k < kDrumMetal; ++k) o.metalW[k] = 0;
  o.metalLvl = 4.5f * kLvl * up;
  o.metalMs = 1.9f * dec * kT60;
  o.metalBp1 = 1750 * r;
  o.metalQ = 3;
  o.noiseLvl = 8.3f * kLvl * dbAmp(30.f * (m.shp - kMid));
  o.noiseMs = 0.7f * kT60;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = 5500;
  o.noiseQ = 1;
}

// TR-909 rimshot (a sample in the original), measured: 220 Hz, tau 10 ms (DECAY: x 0.5 .. 2), with
// 460 Hz at -7 dB, tau 9.4 ms and a little 900 Hz (-22 dB, tau 6 ms; TONE: both -13 .. +13 dB); a short
// click of high-passed noise at -9 dB (NOISE: -15 .. +15 dB); SWEEP: +4 % at the hit, tau 4 ms (0 .. 8 %).
void rim9(DrumParams& o, const Mac& m, float r) {
  constexpr float kLvl = 0.64f;
  const float dec = fastExp2(2.f * (m.dec - kMid));
  const float up = dbAmp(26.f * (m.col - kMid));
  o.toneHz[0] = 220 * r;
  o.toneLvl[0] = kLvl;
  o.toneDecMs[0] = 10.f * dec * kT60;
  o.toneHz[1] = 459 * r;
  o.toneLvl[1] = kLvl * dbAmp(-6.f) * up;
  o.toneDecMs[1] = 9.4f * dec * kT60;
  o.toneHz[2] = 899 * r;
  o.toneLvl[2] = kLvl * dbAmp(-15.f) * up;
  o.toneDecMs[2] = 6.f * dec * kT60;
  o.sweepA = 0.04f * m.swp * (1.f / kMid);
  o.sweepAMs = 4.f * kT60;
  o.noiseLvl = 1.2f * kLvl * dbAmp(30.f * (m.shp - kMid));
  o.noiseMs = 0.5f * kT60;
  o.noiseMode = Svf::Mode::Hp;
  o.noiseHz = 3500;
}

// TR-808 claves, measured: a 2614 Hz sine (TONE: +-1 octave), tau 5 ms (DECAY: x 0.33 .. 3), no pitch
// envelope (SWEEP: up to +6 semitones, 0 by default); a click of noise band-passed at ~8 kHz, -15 dB
// above 5 kHz re the hit, ~1.5 ms.
void clave8(DrumParams& o, const Mac& m, float r) {
  constexpr float kLvl = 0.95f;
  o.toneHz[0] = 2614.f * r * fastExp2(2.f * (m.col - kMid));
  o.toneLvl[0] = kLvl;
  o.toneDecMs[0] = 5.f * fastExp2(3.2f * (m.dec - kMid)) * kT60;
  o.pitchEnv = 6.f * m.swp;
  o.pitchMs = 2.f * kT60;
  o.noiseLvl = 2.2f * kLvl;
  o.noiseMs = 0.6f * kT60;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = 8000;
  o.noiseQ = 0.7f;
  o.drive = m.shp;
}

// TR-808 cowbell, measured: squares at 422 and 632 Hz (x 1.5) band-passed at 675 Hz (FREQ: +-1 octave)
// q 2; BAL: the lower square's weight, -8.5 dB (-21.5 .. +4.5): -15 dB re the upper in the output, as the
// samples' median. Level 0.75 tau 20 ms (ACCENT: 0 .. 1.5) + 0.25 tau 130 ms (DECAY: x 0.33 .. 3), -60
// dB at ~0.78 s; a sine at 843 Hz (the lower square's 2nd) at -20 dB re the upper.
void cowbell8(DrumParams& o, const Mac& m, float r) {
  constexpr float kLvl = 1.85f;
  const float dec = fastExp2(3.2f * (m.dec - kMid)), acc = 0.75f * m.swp * (1.f / kMid);
  o.metalHz[0] = 422.f * r;
  o.metalHz[1] = 632.f * r;
  o.metalW[0] = dbAmp(-8.5f + 26.f * (m.shp - kMid));
  o.metalW[1] = 1;
  for (int k = 2; k < kDrumMetal; ++k) o.metalW[k] = 0;  // two squares
  o.metalLvl = 0.25f * kLvl;
  o.metalMs = 130.f * dec * kT60;
  o.metalAccent = acc * kLvl;
  o.metalAccMs = 20.f * kT60;
  o.metalBp1 = 675.f * fastExp2(2.f * (m.col - kMid));
  o.metalQ = 2;
  o.toneHz[0] = 843.f * r;
  o.toneLvl[0] = 0.06f * kLvl;
  o.slowLvl = 0.25f;
  o.toneMs = o.metalMs;
  o.fastLvl = acc;
  o.fastMs = o.metalAccMs;
}

// knob() on a log scale (times, frequencies): exponential between the points.
float knobExp(const float y[4], float t) {
  if (t < 0.3f) return expMap(y[0], y[1], t * (1.f / 0.3f));
  if (t < 0.7f) return expMap(y[1], y[2], (t - 0.3f) * (1.f / 0.4f));
  return expMap(y[2], y[3], (t - 0.7f) * (1.f / 0.3f));
}

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// SPREAD: x 0.8 .. 1.25, exactly 1 at the default 64 (the measured set).
float hatSpread(const Mac& m) { return fastExp2(0.6439f * (m.swp - kMid)); }

// A hat's envelope as measured: a plateau of hold x tau, then a fall from tau start x tau that speeds up
// (acc: its rate grows by the start rate every acc x tau ms; 0 = plain), so the analysis' tau (a fit
// over -3 .. -20 dB) reads ~tau. Both metal bands; the noise the same, its fall x nDec.
void hatEnv(DrumParams& o, float tau, float hold, float start, float acc, float nDec) {
  o.metalHoldMs = o.noiseHoldMs = hold * tau;
  o.metalMs = o.metal2Ms = start * tau * kT60;
  o.metalAccelMs = o.metal2AccelMs = o.noiseAccMs = acc * tau;
  o.noiseMs = o.metalMs * nDec;
}

// TR-808 hats, measured on samples: the 808 metal set band-passed at 7.1 kHz (q 3) plus an air band
// at 11 kHz (the band-pass high-passed at ~5 kHz, HP: x 0.5 .. 2), nearly all metal (NOISE: band-passed
// noise, < 10 % of the energy at the default). DECAY: closed tau 5 .. 40 ms (DECAY 0 .. 51, default 18
// at 38), a plain exponential; open 160 .. 660 ms (DECAY 76 .. 127): 7.2 kHz, less air, a short plateau,
// a fall that speeds up, cut at 0.4 .. 0.8 s. N.DEC: the noise's fall x 0.25 .. 4.
void hat8(DrumParams& o, const Mac& m, float r) {
  static const float kTau[4] = {4.5f, 18, 454, 660};  // DECAY 0 / 38 / 89 / 127, set for the taus read
  constexpr float kLvl = 3.f;
  const float tau = knobExp(kTau, m.dec), open = clamp01((tau - 40.f) * (1.f / 120));
  const float lvl = kLvl * lerp(1.2f, 1.7f, open);
  metal(o, kMetal808, r, hatSpread(m));
  o.metalLvl = lvl;
  o.metalBp1 = lerp(7100, 7200, open);
  o.metalQ = 3;
  o.metalBp2 = 11000;
  o.metalQ2 = 3;
  o.metal2Lvl = lvl * lerp(1.8f, 0.3f, open);
  o.metalHp = 5000.f * fastExp2(2.f * (m.col - kMid));
  const float acc = open * (1.f / 1.16f);  // the speed-up's rate, 1 / (acc x tau)
  hatEnv(o, tau, open * (10.f / 330), lerp(1.f, 1.53f, open), acc > 0 ? 1.f / acc : 0,
         fastExp2(4.f * (m.con - kMid)));
  o.cutMs = tau < 300 ? maxf(400.f + 1.43f * (tau - 160), 400.f) : 600.f + 0.3f * (tau - 300);
  o.cutFallMs = 220;
  o.noiseLvl = 0.3f * lvl * m.shp;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = 7500;
  o.noiseQ = 1;
}

// TR-808 cymbal, measured: the 808 set into a fast band at 7.3 kHz (q 2.3: 0.6 tau 60 ms + 0.4 tau
// 540 ms) and a slow one at 2.9 kHz (q 3, -12 dB at the hit; ~flat for 0.8 s, then a fall that speeds
// up), both cut at 3.1 s (-60 dB in 1.3 s): -60 dB at ~3.6 .. 3.9 s. DECAY scales all times (x 0.5 .. 2, 1 at the default 90), HP the
// fast band's high-pass (5 kHz, x 0.5 .. 2), NOISE a little band-passed noise, N.DEC its fall.
void cym8(DrumParams& o, const Mac& m, float r) {
  constexpr float kLvl = 5.9f, kDef = 90.f / 127.f;
  const float dec = fastExp2(2.f * (m.dec - kDef));
  metal(o, kMetal808, r, hatSpread(m));
  o.metalLvl = 0.4f * kLvl;
  o.metalMs = 540.f * dec * kT60;
  o.metalAccent = 0.6f * kLvl;
  o.metalAccMs = 60.f * dec * kT60;
  o.metalBp1 = 7300;
  o.metalQ = 2.3f;
  o.metalBp2 = 2900;
  o.metalQ2 = 3;
  o.metal2Lvl = 0.11f * kLvl;
  o.metal2Ms = 6400.f * dec * kT60;
  o.metal2AccelMs = 300.f * dec;
  o.metalHp = 5000.f * fastExp2(2.f * (m.col - kMid));
  o.cutMs = 3100.f * dec;
  o.cutFallMs = 1300.f * dec;
  o.noiseLvl = 0.1f * kLvl * m.shp;
  o.noiseMs = 300.f * dec * kT60 * fastExp2(4.f * (m.con - kMid));
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = 7300;
  o.noiseQ = 1;
}

// TR-909 hats, measured on samples: mostly noise (NOISE: 65 % of the energy at the default 59), a band
// over ~5 .. 8.7 kHz (6.5 kHz q 0.7, tilted by a 1.8 kHz one-pole high-pass), with the 909 metal set
// alongside (7 kHz q 1.5, high-passed at 4.5 kHz; HP: x 0.5 .. 2, all of them). DECAY: closed tau 9 .. 44 ms (a ~10 ms plateau, then a fall that speeds up) to open 58 ..
// 151 ms (~exponential). N.DEC: the noise's fall x 0.25 .. 4.
void hat9(DrumParams& o, const Mac& m, float r) {
  static const float kTau[4] = {11, 25, 75, 160};  // DECAY 0 / 38 / 89 / 127, set for the taus read
  constexpr float kLvl = 4.4f;
  const float tau = knobExp(kTau, m.dec), open = clamp01((tau - 30.f) * (1.f / 40));
  const float hp = fastExp2(2.f * (m.col - kMid)), nz = m.shp;
  metal(o, kMetal909, r, hatSpread(m));
  o.metalLvl = kLvl * (1.f - 0.8f * nz);
  o.metalBp1 = 7000.f * hp;
  o.metalQ = 1.5f;
  o.metalHp = 4500.f * hp;
  const float inv = lerp(1.f / 3.2f, 0, open);
  hatEnv(o, tau, lerp(0.32f, 0.1f, open), lerp(1.27f, 0.9f, open), inv > 0 ? 1.f / inv : 0,
         fastExp2(4.f * (m.con - kMid)));
  o.noiseLvl = kLvl * nz;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = 6500.f * hp;
  o.noiseQ = 0.7f;
  o.noiseHpHz = 1800.f * hp;
}

// TR-909 crash and ride (6-bit samples in the original), measured: noise and the 909 metal set in a
// band that TUNE moves up 1.2 octaves (crash 3.4 .. 7.8 kHz, ride 5.3 .. 12 kHz: the spectra's centroid
// x ~1.7, the samples') as it shortens the decay (crash tau 430 .. 205 ms, ride 337 .. 183; DECAY x 0.5 .. 2, 1 at the default 95). RIDE/CR
// blends ride (0) to crash (127): the crash swells over ~50 ms and has a low clang (bp2 at ~0.5 ..
// 0.8 kHz, tau ~50 ms); the ride holds ~50 ms. N.DEC: the noise's fall x 0.25 .. 4.
void cym9(DrumParams& o, const Mac& m, float r) {
  // Set so the taus read 433 / 387 / 263 / 204 (crash) and 337 / 290 / 233 / 183 (ride) at TUNE 0 / 3 / 7 / 10.
  static const float kCrash[4] = {395, 360, 250, 212}, kRide[4] = {314, 275, 225, 183};
  constexpr float kLvl = 2.2f, kDef = 95.f / 127.f;
  const float c = m.shp, t = m.col, oct = fastExp2(1.2f * t);
  const float tau = lerp(knob(kRide, t), knob(kCrash, t), c) * fastExp2(2.f * (m.dec - kDef));
  const float base = lerp(5300, 3400, c), band = base * oct, hp = base * fastPow(oct, 0.5f);
  const float lvl = kLvl * dbAmp(3.5f * (t - 0.5f) + 1.5f * (1 - c));  // even over TUNE and RIDE/CR
  metal(o, kMetal909, r, hatSpread(m));
  o.metalLvl = lvl;
  o.metalAccent = -0.85f * lvl * c;  // the swell
  o.metalAccMs = 25.f * kT60;
  o.metalBp1 = band;
  o.metalQ = 1.5f;
  o.metalHp = lerp(0.8f, 0.6f, c) * hp;  // bp1 only: the clang stays
  hatEnv(o, tau, lerp(0.15f, 0.f, c), 1.f, 0, fastExp2(4.f * (m.con - kMid)));
  if (c > 0) {  // the clang: bp2 on its own (off for the ride)
    o.metalBp2 = 474.f * fastPow(1.73f, t);
    o.metalQ2 = 2;
    o.metal2Lvl = 0.42f * lvl * c * dbAmp(5.8f - 10.6f * t);  // even over TUNE (bp2 meets the squares)
    o.metal2Ms = 50.f * kT60;
  }
  o.noiseLvl = lvl;
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = band;
  o.noiseQ = lerp(0.6f, 0.7f, c);
  o.noiseHpHz = lerp(0.5f, 0.6f, c) * hp;
  o.noiseRise = 0.65f * c;
  o.noiseRiseMs = 20;
}

using D = DrumMachine;
struct Group {
  const char* name;
  uint8_t count;
  const char* model[2];
  D machine[2];
};
constexpr Group kGroups[] = {
    {"KICK", 2, {"808", "909"}, {D::Bd8, D::Bd9}},
    {"SNARE", 2, {"808", "909"}, {D::Sd8, D::Sd9}},
    {"TOM", 2, {"808", "909"}, {D::Tom8, D::Tom9}},
    {"CLAP", 2, {"808", "909"}, {D::Cp8, D::Cp9}},
    {"RIM", 2, {"808", "909"}, {D::Rs8, D::Rs9}},
    {"HAT", 2, {"808", "909"}, {D::Hh8, D::Hh9}},
    {"CYMBL", 2, {"808", "909"}, {D::Cy8, D::Cy9}},
    {"PERC", 2, {"CLAVE", "BELL"}, {D::Cl8, D::Cb8}},
};
constexpr int kGroupCount = sizeof(kGroups) / sizeof(kGroups[0]);

const Group& group(int g) { return kGroups[g >= 0 && g < kGroupCount ? g : 0]; }

// (group, model) of a machine; unknown ones as BD8 (group 0, model 0).
void locate(uint8_t m, int& g, int& k) {
  for (g = 0; g < kGroupCount; ++g)
    for (k = 0; k < kGroups[g].count; ++k)
      if (static_cast<uint8_t>(kGroups[g].machine[k]) == m) return;
  g = k = 0;
}

}  // namespace

int drumGroupCount() { return kGroupCount; }
const char* drumGroupName(int g) { return group(g).name; }
int drumModelCount(int g) { return group(g).count; }

const char* drumModelName(int g, int k) {
  const Group& gr = group(g);
  return gr.model[k >= 0 && k < gr.count ? k : 0];
}

uint8_t drumMachineAt(int g, int k) {
  const Group& gr = group(g);
  return static_cast<uint8_t>(gr.machine[k >= 0 && k < gr.count ? k : 0]);
}

int drumGroupOf(uint8_t machine) {
  int g, k;
  locate(machine, g, k);
  return g;
}

int drumModelOf(uint8_t machine) {
  int g, k;
  locate(machine, g, k);
  return k;
}

const char* drumMachineName(uint8_t m) {
  return kNames[m < static_cast<int>(DrumMachine::Count) ? m : 0].machine;
}

const char* drumMacroName(uint8_t m, int k) {
  if (k < 0 || k >= kFmMacros) return "";
  return kNames[m < static_cast<int>(DrumMachine::Count) ? m : 0].mac[k];
}

void drumMachine(uint8_t machine, const float mac[kFmMacros], float pitch, DrumParams& o) {
  o = DrumParams();
  const float r = fastExp2((pitch - 60.f) * (1.f / 12.f));
  Mac m;
  m.dec = u(mac[kMacDec]);
  m.dMs = expMap(5, 4000, m.dec);  // as fmDecayMs, fractional
  m.col = u(mac[kMacCol]);
  m.shp = u(mac[kMacShp]);
  m.swp = u(mac[kMacSwp]);
  m.con = u(mac[kMacCon]);
  if (machine >= static_cast<int>(DrumMachine::Count)) machine = 0;
  switch (static_cast<DrumMachine>(machine)) {
    case DrumMachine::Sd8: snare8(o, m, 165 * r); break;
    case DrumMachine::Tom8: tom8(o, m, r); break;
    case DrumMachine::Cp8: clap(o, m, kClap8); break;
    case DrumMachine::Rs8: rim8(o, m, r); break;
    case DrumMachine::Cl8: clave8(o, m, r); break;
    case DrumMachine::Cb8: cowbell8(o, m, r); break;
    case DrumMachine::Hh8: hat8(o, m, r); break;
    case DrumMachine::Cy8: cym8(o, m, r); break;
    case DrumMachine::Bd9: kick9(o, m, 49.5f * r); break;
    case DrumMachine::Sd9: snare9(o, m, r); break;
    case DrumMachine::Tom9: tom9(o, m, r); break;
    case DrumMachine::Cp9: clap(o, m, kClap9); break;
    case DrumMachine::Rs9: rim9(o, m, r); break;
    case DrumMachine::Hh9: hat9(o, m, r); break;
    case DrumMachine::Cy9: cym9(o, m, r); break;
    default: kick8(o, m, 45 * r); break;  // Bd8
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
