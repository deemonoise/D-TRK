#include "synth_syn_sat.h"
#include "model.h"
#include "synth_drive.h"
#include "synth_shape.h"
#include "synth_osc.h"
#include "synth_vfilter.h"
namespace mt {
namespace {
// raw16..112 in16-step intervals, then127; fitted biased soft-clip parameters x1024 (Soft: cubic knee; others: tanh).
// These are scalar model coefficients, not oscillator tables or recorded wave periods.
MT_CONTROL_DATA const uint16_t kSoft[4][24] = {
  {114, 23, 1413, 251, 56, 1501, 428, 89, 1546, 702, 139, 1627, 1071, 197, 1237, 1568, 251, 953, 2213, 290, 744, 3001, 310, 628},
  {117, 372, 1424, 651, 266, 579, 907, 376, 735, 1101, 498, 1034, 1400, 572, 934, 1795, 639, 821, 2173, 676, 746, 2599, 678, 715},
  {128, 610, 2287, 837, 295, 644, 1262, 404, 755, 1781, 503, 892, 2390, 561, 759, 3164, 581, 663, 4201, 575, 586, 5563, 558, 565},
  {246, 583, 1622, 1446, 236, 541, 2221, 278, 583, 3213, 300, 622, 4496, 308, 555, 6289, 306, 504, 8647, 294, 456, 11263, 270, 436},
};
MT_CONTROL_DATA const uint16_t kShaper[16] = {644, 365, 1506, 364, 2633, 364, 4523, 365, 7594, 342, 12750, 319, 21403, 296, 34908, 275};
MT_CONTROL_DATA const uint16_t kAnalog[16] = {315, 441, 567, 562, 879, 625, 1818, 556, 3552, 437, 6116, 365, 10285, 306, 16467, 260};
MT_CONTROL_DATA const uint16_t kDigital[16] = {1025, 219, 1025, 510, 1677, 546, 2887, 546, 8170, 467, 23480, 353, 64512, 330, 64512, 351};
// Interpolate scalar parameters, extending the measured raw16 endpoint to silence at0.
MT_CONTROL void lookup(const uint16_t* table, int stride, float raw, float* out) {
  raw = fclamp(raw, 0.f, 127.f);
  int i = raw < 16.f ? 0 : static_cast<int>(raw * (1.f / 16.f)) - 1;
  if (i > 6) i = 6;
  const float t = raw < 16.f ? 0.f : (raw - (i + 1) * 16.f) * (i == 6 ? 1.f / 15.f : 1.f / 16.f);
  for (int k = 0; k < stride; ++k) {
    const float a = table[i * stride + k];
    out[k] = (a + (table[(i + 1) * stride + k] - a) * t) * (1.f / 1024.f);
  }
  if (raw < 16.f) out[0] *= raw * (1.f / 16.f);
}
// Positive turns make the sine table's truncated index a floor, including negative inputs.
MT_INLINE float sine(float radians) { return phaseSine(fclamp(radians, -1024.f, 1024.f) * .1591549431f + 256.f); }
MT_INLINE float cosine(float radians) { return sine(radians + 1.5707963268f); }
}
void SynSat::set(uint8_t type, uint8_t level) {
  if (type >= static_cast<uint8_t>(SynSatType::Count)) type = 0;
  if (level > 127) level = 127;
  if (type == type_ && level == level_) return;
  type_ = type; level_ = level;
  if (type >= 1 && type <= 4) {
    float p[3]; lookup(kSoft[type - 1], 3, level, p);
    g_ = p[0]; bias_ = p[1]; scale_ = p[2];
  } else if (type == 6) {
    float p[2]; lookup(kShaper, 2, level, p); g_ = p[0]; scale_ = p[1];
  } else if (type == 7) {
    bias_ = fclamp((level - 64.f) * (1.f / 64.f), 0.f, 1.f);
    if (level == 127) bias_ = 1.f;
    g_ = level < 64 ? fastPow(level * (1.f / 64.f), 1.4f) : 1.f;
    scale_ = 1.f + bias_;
  } else if (type == 5) {
    float p[2]; lookup(kDigital, 2, level, p); g_ = p[0]; scale_ = p[1];
  }
}
void synAnalogDrive(float level, float& drive, float& gain) {
  float p[2]; lookup(kAnalog, 2, level, p); drive = p[0]; gain = p[1];
}
MT_HOT void SynSat::processBlock(float* x, int n) {
  if (!type_) return;
  float prev = x1_, dc = dc_;
  const float g = g_, bias = bias_, scale = scale_;
  if (type_ <= 4) {
    const bool cubic = type_ == 2;
    const float base = cubic ? vfSat(bias) : Drive::tanhLut(bias);
    const float *table, *slopes; Drive::tables(table, slopes);
    // Two evaluations per input interval; interpolated midpoint and box decimation.
    for (int i = 0; i < n; ++i) {
      const float input = x[i];
      const float mid = g * .5f * (input + prev) + bias, now = g * input + bias;
      const float y = (.5f * (cubic ? vfSat(mid) + vfSat(now) :
                             Drive::tanhLut(mid, table, slopes) + Drive::tanhLut(now, table, slopes)) - base) * scale;
      dc += .001424f * (y - dc); x[i] = y - dc; prev = input;
    }
  } else if (type_ == 6) {
    float oldF = -cosine(g * prev);
    for (int i = 0; i < n; ++i) {
      const float input = x[i], u = g * input, old = g * prev, F = -cosine(u);
      x[i] = scale * adaaStep(u, old, F, oldF, [](float z) { return sine(z); });
      prev = input; oldF = F;
    }
  } else if (type_ == 7) {
    float oldF = .5f * prev * fabsf(prev);
    for (int i = 0; i < n; ++i) {
      const float input = x[i], F = .5f * input * fabsf(input);
      const float rect = adaaStep(input, prev, F, oldF, [](float z) { return fabsf(z); });
      const float y = g * scale * ((1.f - bias) * .5f * (input + prev) + bias * rect);
      dc += .001424f * (y - dc); x[i] = y - dc; prev = input; oldF = F;
    }
  } else {
    auto anti = [](float u) { const float a = fabsf(u); return a <= 1.f ? .5f * u * u : a - .5f; };
    float oldF = anti(g * prev);
    for (int i = 0; i < n; ++i) {
      const float input = x[i], u = g * input, F = anti(u);
      x[i] = scale * adaaStep(u, g * prev, F, oldF, [](float z) { return fclamp(z, -1.f, 1.f); });
      prev = input; oldF = F;
    }
  }
  x1_ = prev; dc_ = dc;
}
}
