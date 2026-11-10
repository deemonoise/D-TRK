#include "tempo_detect.h"
#include <math.h>
#include "hot.h"

namespace mt {
namespace {

// This is a bounded event-grid analysis, not the code from a reference implementation.
// Positive onset evidence across frequency bands and metrical ambiguity are discussed in
// Dixon, DAFx 2006, and Ellis, JNMR 2007. Confidence below is our heuristic, not a probability.
// https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf
struct Evidence {
  float time[kMaxOnsets], weight[kMaxOnsets];
  int count = 0;
  float total = 0, span = 0;
};

float gridScore(const Evidence& evidence, float bpm) {
  float cosine[3] = {}, sine[3] = {};
  for (int i = 0; i < evidence.count; ++i) {
    const float angle = evidence.time[i] * bpm * (6.28318530718f / 60.f);
    const float c = cosf(angle), s = sinf(angle), weight = evidence.weight[i];
    // Higher harmonics allow common offbeat and swing positions without an audio-size envelope.
    cosine[0] += weight * c; sine[0] += weight * s;
    cosine[1] += weight * (2*c*c-1); sine[1] += weight * (2*c*s);
    cosine[2] += weight * (4*c*c*c-3*c); sine[2] += weight * (3*s-4*s*s*s);
  }
  float concentration = 0;
  const float importance[3] = {1.f, .4f, .2f};
  for (int harmonic = 0; harmonic < 3; ++harmonic)
    concentration += importance[harmonic] * sqrtf(cosine[harmonic]*cosine[harmonic] +
                                                 sine[harmonic]*sine[harmonic]) / evidence.total;
  float occupancy = (evidence.count-1) * 60.f / (evidence.span * bpm);
  if (occupancy > 1) occupancy = 1;
  return concentration / 1.6f * (.65f + .35f * occupancy);
}

}  // namespace

TempoEstimate estimateTempo(const Onset* onsets, int count, uint32_t rate, uint32_t frames) {
  TempoEstimate result;
  if (!onsets || count < 4 || count > kMaxOnsets || !rate || !frames) return result;
  Evidence evidence;
  for (int i = 0; i < count; ++i) {
    if (onsets[i].pos >= frames || (i && onsets[i].pos <= onsets[i-1].pos)) return result;
    if (onsets[i].strength < 50) continue;
    const float weight = .5f + .5f * (onsets[i].strength > 1000 ? 1000 : onsets[i].strength) / 1000.f;
    evidence.time[evidence.count] = static_cast<float>(onsets[i].pos - onsets[0].pos) / rate;
    evidence.weight[evidence.count++] = weight;
    evidence.total += weight;
  }
  if (evidence.count < 4) return result;
  evidence.span = evidence.time[evidence.count-1] - evidence.time[0];
  if (evidence.span < 1.f) return result;
  int best = 0;
  float strongest = 0;
  for (int bpm = 40; bpm <= 240; ++bpm) {
    const float score = gridScore(evidence, static_cast<float>(bpm));
    if (score > strongest) { strongest = score; best = bpm; }
  }
  // Searching many grids over a few arbitrary timestamps otherwise overfits chance.
  // Require almost exact alignment for four attacks; relax only with longer evidence.
  float required = .5f + 2.f / (evidence.count - 1);
  if (required > .995f) required = .995f;
  if (strongest < required) return result;
  float offset = 0;
  if (best > 40 && best < 240) {
    const float left = gridScore(evidence, static_cast<float>(best-1));
    const float right = gridScore(evidence, static_cast<float>(best+1));
    const float curvature = left - 2*strongest + right;
    if (curvature < -.000001f) offset = .5f * (left-right) / curvature;
    if (offset < -.5f) offset = -.5f;
    if (offset > .5f) offset = .5f;
  }
  result.bpm10 = static_cast<uint16_t>(lroundf((best+offset)*10));
  result.confidence = static_cast<uint16_t>(lroundf(strongest > 1.f ? 1000.f : strongest*1000));
  result.halfBpm10 = result.bpm10 / 2;
  result.doubleBpm10 = result.bpm10 * 2;
  const float half = gridScore(evidence, result.bpm10 / 20.f);
  const float twice = gridScore(evidence, result.bpm10 / 5.f);
  result.ambiguous = (half > twice ? half : twice) >= strongest * .7f;
  return result;
}

}  // namespace mt
