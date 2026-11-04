#include "synth_fm.h"
#include "model.h"
#include "fastmath.h"
#include "hot.h"
#include <math.h>

namespace mt {
namespace {
MT_CONTROL float bound(float x, float lo, float hi) {
  return !isfinite(x) ? lo : x < lo ? lo : x > hi ? hi : x;
}
MT_CONTROL float unit(float x) { return bound(x, 0, 127) * (1.f / 127.f); }
MT_CONTROL float mix(float a, float b, float u) { return a + (b - a) * u; }
MT_CONTROL float expRange(float lo, float hi, float u) { return lo * fastPow(hi / lo, u); }
// Log interpolation through measured raw DEC knots. Times are -60 dB, not T20.
MT_CONTROL float decay(const float (&ms)[5], float raw) {
  const float x = bound(raw, 0, 127);
  int k = static_cast<int>(x) / 32;
  if (k > 3) k = 3;
  const float width = k == 3 ? 31.f : 32.f;
  return expRange(ms[k], ms[k + 1], (x - k * 32.f) / width);
}
MT_CONTROL float noteRatio(float note, float tune, float scale = 1) {
  const float semis = bound(note, -48, 160) - 60 + bound(tune - 64, -24, 24);
  return fastExp2(semis * (scale / 12.f));
}
MT_CONTROL void carrier(FmParams& p, int k, float ratio, float level, float ms = 0) {
  p.op[k].ratio = ratio;
  p.op[k].level = level;
  p.op[k].decayMs = ms;
}
MT_CONTROL void modulator(FmParams& p, int k, float ratio, float index, float ms, float sustain) {
  carrier(p, k, ratio, index, ms);
  p.op[k].output = false;
  p.op[k].sustain = sustain;
}
MT_CONTROL void punch(FmParams& p, float raw, float amount) {
  const bool on = raw >= .5f;
  p.attackMs = on ? .12f : .36f;
  p.outputGain *= on ? amount : 1.f;
}
MT_CONTROL void bassDrum(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {150, 1110, 3030, 7200, 14940};
  // The middle raw TMOD region is bright; both lower values and 127 are much darker.
  static constexpr float kTimbre[] = {.10f, .005f, .008f, 6.f, .32f};
  const float shape = unit(r[4]), env = unit(r[6]);
  const float tm = bound(r[5], 0, 127);
  int k = static_cast<int>(tm) / 32;
  if (k > 3) k = 3;
  const float timbre = mix(kTimbre[k], kTimbre[k + 1], (tm - k * 32.f) / (k == 3 ? 31.f : 32.f));
  p.hz = 65.46f * noteRatio(note, r[0]);
  p.ampMs = decay(kDecay, r[3]);
  p.outputGain = .6f;
  carrier(p, 0, 1, 1);
  p.op[0].mod = 4;
  p.op[0].fb = .16f * shape * shape;
  modulator(p, 4, mix(1.5f, 30.f, timbre / 6.f) * mix(1.f, .55f, shape),
            timbre * mix(1.f, .65f, shape), mix(50.f, 300.f, env), env * env);
  const float sw = unit(r[1]);
  p.pitchEnv = 36.f * sinf(3.14159265f * sw);
  p.pitchMs = mix(22.f, 70.f, sw);
  punch(p, r[2], 1.45f);
}
MT_CONTROL void snareBasic(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {30, 120, 270, 660, 1080};
  const float inharm = unit(r[4]), feedback = unit(r[5]), env = unit(r[6]);
  p.hz = 261.55f * noteRatio(note, r[0]);
  p.ampMs = decay(kDecay, r[3]);
  p.outputGain = .55f;
  carrier(p, 0, 1, .72f);
  carrier(p, 1, mix(3.f, 6.f, inharm), .6f, p.ampMs * .8f);
  p.op[0].mod = p.op[1].mod = 4;
  modulator(p, 4, mix(2.1f, 4.7f, inharm), .35f + 3.5f * feedback * feedback,
            p.ampMs * .55f, .025f + .9f * env * env);
  p.op[4].fb = 4.6f * feedback;
  p.noise = bound(.015f + 11.f * feedback * feedback, 0, 8);
  p.noiseMs = p.ampMs * .35f;
  p.noiseSustain = .004f + .996f * sqrtf(env);
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = mix(2300.f, 7500.f, feedback);
  p.filterQ = .65f;
  p.pitchEnv = 20.f * unit(r[1]) * unit(r[1]);
  p.pitchMs = 45;
  punch(p, r[2], 1.3f);
}
MT_CONTROL void snareVintage(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {40, 280, 750, 1200, 1800};
  const float inharm = unit(r[4]), comp = unit(r[5]), env = unit(r[6]);
  const float shorten = .035f + .965f * (1.f - inharm * inharm * inharm * inharm * inharm);
  const float pitch = noteRatio(note, r[0]);
  p.hz = 261.55f * pitch;
  p.ampMs = decay(kDecay, r[3]) * shorten;
  p.outputGain = .5f;
  // A noisy short excitation and a lower resonant body are separate from BASIC's PM-feedback path.
  p.noise = .65f * (1.f - .6f * inharm);
  p.noiseMs = p.ampMs;
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = bound(1700.f * pitch, 100, 15000);
  p.filterQ = .55f;
  p.noise2 = .32f;
  p.noise2Ms = mix(35.f, 90.f, env) * shorten;
  p.noise2FilterMode = Svf::Mode::Bp;
  p.noise2FilterHz = bound(4300.f * pitch, 180, 17000);
  p.noise2FilterQ = mix(.55f, 1.1f, comp);
  carrier(p, 0, mix(4.f, 2.f, inharm), .2f, p.ampMs * .7f);
  carrier(p, 1, mix(2.02f, 2.7f, inharm), .11f, p.ampMs * .6f);
  p.op[0].mod = 4;
  modulator(p, 4, mix(1.8f, 3.3f, comp), .15f + .5f * comp * inharm, p.ampMs * .25f, .02f);
  p.op[4].fb = .7f * comp;
  p.pitchEnv = 5.f * unit(r[1]);
  p.pitchMs = 25;
  punch(p, r[2], 1.4f);
}
MT_CONTROL void carbon(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {60, 660, 2730, 7500, 14190};
  const float inharm = unit(r[4]), depth = unit(r[5]), env = unit(r[6]);
  p.hz = 261.55f * noteRatio(note, r[0]);
  p.ampMs = decay(kDecay, r[3]);
  p.outputGain = .6f;
  carrier(p, 0, 1, 1);
  carrier(p, 1, mix(2.4f, 3.8f, inharm), .85f * inharm, p.ampMs * .8f);
  p.op[0].mod = 4;
  // At INHM0 MOD is spectrally ineffective in the measured extreme pair.
  modulator(p, 4, mix(1.f, 3.4f, inharm), 2.8f * inharm * inharm * depth * depth,
            mix(90.f, 1200.f, env), .02f + .98f * env * env);
  p.pitchEnv = 12.f * sinf(3.14159265f * unit(r[1]));
  p.pitchMs = 65;
  punch(p, r[2], 1.5f);
}
MT_CONTROL void clapVintage(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {270, 1080, 2160, 5400, 16200};
  const float balance = unit(r[4]), body = unit(r[5]), env = unit(r[6]);
  const float pitch = noteRatio(note, r[0]);
  const float spread = fabsf(bound(r[1], 0, 127) - 64.f);
  // Measured SPCR is a signed excitation rate, not a period increasing towards the edges.
  const float rate = fabsf(roundf((2.f * bound(r[1], 0, 127) - 127.f) * (1.f / 3.f)));
  const bool compact = rate == 0;
  const float bodyGate = .5f + .5f * bound(spread / 16.f, 0, 1);
  p.hz = 261.55f * pitch;
  p.ampMs = decay(kDecay, r[3]);
  p.outputGain = .5f;
  p.noise = 1.4f * (1.f - balance) * (compact ? .45f : 1.f);
  p.noiseMs = compact ? 100.f : 55.f;
  p.bursts = compact ? 0 : static_cast<uint8_t>(1 + bound(spread / 10.f, 0, 7));
  p.burstMs = compact ? 0 : 1000.f / rate;
  p.tail = .3f;
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = bound(1900.f * pitch, 120, 16000);
  p.filterQ = .7f;
  // Independent noise2 is essential: BODY must be inaudible at BAL0.
  p.noise2 = 1.5f * balance * bodyGate;
  p.noise2Ms = mix(324.f, 5040.f, env) * mix(.3f, 1.f, unit(r[3])) * (compact ? .15f : 1.f);
  p.noise2FilterMode = Svf::Mode::Lp;
  p.noise2FilterHz = bound(expRange(900.f, 19500.f, body) * pitch, 80, 19500);
  p.noise2FilterQ = .707f;
  punch(p, r[2], .8f);
}
MT_CONTROL void alloy(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {150, 450, 1050, 2400, 6000};
  static constexpr float kRatios[] = {12.5f, 18.5f, 23.2f};
  const float radio = unit(r[4]), depth = unit(r[5]), env = unit(r[6]);
  const float shimmer = unit(r[1]);
  p.hz = 261.55f * noteRatio(note, r[0]);
  p.ampMs = decay(kDecay, r[3]);
  p.outputGain = .4f;
  for (int k = 0; k < 3; ++k) {
    carrier(p, k, kRatios[k] * mix(.96f, 1.06f, shimmer), .34f - .06f * k,
            p.ampMs * (1.f - .12f * k));
    p.op[k].mod = 4;
  }
  carrier(p, 3, mix(50.f, 72.f, shimmer), .16f + .21f * sqrtf(depth), p.ampMs * .55f);
  modulator(p, 4, mix(8.f, 27.f, radio), (.2f + 1.9f * depth * depth) * mix(.6f, 1.9f, env),
            p.ampMs * .45f, mix(.05f, 1.f, env));
  p.op[4].fb = .12f * shimmer;
  punch(p, r[2], 1.3f);
}
MT_CONTROL void hatBasic(const float* r, float note, FmParams& p) {
  static constexpr float kDecay[] = {45, 570, 1545, 2520, 4155};
  static constexpr float kRatios[] = {.956f, 1.f, 1.029f, 1.083f, 1.817f, 2.029f};
  static constexpr float kWeights[] = {.082f, .16f, .107f, .092f, .02f, .02f};
  const float rawTone = bound(r[1], 0, 127), transient = unit(r[2]);
  const auto curve = [rawTone](const float (&values)[5]) {
    int k = static_cast<int>(rawTone) / 32;
    if (k > 3) k = 3;
    return mix(values[k], values[k + 1], (rawTone - k * 32.f) / (k == 3 ? 31.f : 32.f));
  };
  static constexpr float kMetalScale[] = {.6f, .8f, 1.f, 1.2f, 1.55f};
  static constexpr float kMetalLevel[] = {.3f, .5f, 1.f, 1.f, 1.f};
  static constexpr float kBodyLevel[] = {.85f, .9f, 1.f, 1.f, .92f};
  static constexpr float kWideLevel[] = {.34f, .40f, .48f, .54f, .50f};
  static constexpr float kWideHz[] = {11800, 12000, 12000, 13500, 15500};
  static constexpr float kTailLevel[] = {.27f, .35f, .08f, .04f, .02f};
  static constexpr float kTailHz[] = {3800, 5200, 11000, 14000, 16000};
  const float body = curve(kBodyLevel), metal = .9f * body * curve(kMetalLevel);
  p.hz = 6902.3f * noteRatio(note, r[0], .125f);
  p.ampMs = decay(kDecay, r[3]);
  p.outputGain = .35f;
  // TONE changes the dominant metallic band and the broadband balance, not only a high-pass.
  for (int k = 0; k < 6; ++k) carrier(p, k, kRatios[k] * curve(kMetalScale), kWeights[k] * metal);
  p.noise = curve(kWideLevel) * body;
  p.noiseMs = 0;
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = curve(kWideHz);
  p.filterQ = 1;
  p.filterAll = false;
  p.noise2 = mix(1.f, 2.8f, sqrtf(transient));
  p.noise2Ms = mix(3.f, 40.f, transient);
  // Steady colored noise is independent of TDEC; that control changes only the excitation.
  p.noise2Sustain = curve(kTailLevel) * body / p.noise2;
  p.noise2FilterMode = Svf::Mode::Bp;
  p.noise2FilterHz = curve(kTailHz);
  p.noise2FilterQ = .9f;
  p.resetPhase = r[6] >= .5f;
}
}

MT_CONTROL bool fmPercussionMachine(uint8_t machine, const float raw[8], float note, FmParams& out) {
  float r[8];
  for (int k = 0; k < 8; ++k) r[k] = bound(raw[k], 0, 127);
  FmParams p;
  p.oneShot = true;
  switch (static_cast<FmMachine>(machine)) {
    case FmMachine::BdModern: bassDrum(r, note, p); break;
    case FmMachine::SdBasic: snareBasic(r, note, p); break;
    case FmMachine::SdVintage: snareVintage(r, note, p); break;
    case FmMachine::PcCarbon: carbon(r, note, p); break;
    case FmMachine::CpVintage: clapVintage(r, note, p); break;
    case FmMachine::CyAlloy: alloy(r, note, p); break;
    case FmMachine::HhBasic: hatBasic(r, note, p); break;
    default: return false;
  }
  p.hz = bound(p.hz, .1f, 19000);
  p.sat = unit(r[7]);
  // Baseline calibration relative to BD, applied after OVER so its drive law is unchanged.
  static constexpr float kPostGain[] = {1.f, .44014938f, 3.91534063f, .61664994f,
                                        6.51172334f, 2.73838447f, 1.f, 1.f, 1.f, .54f};
  static_assert(sizeof(kPostGain) / sizeof(kPostGain[0]) == static_cast<int>(FmMachine::Count),
                "calibration for each machine ID");
  p.postGain = kPostGain[machine];
  out = p;
  return true;
}
}  // namespace mt
