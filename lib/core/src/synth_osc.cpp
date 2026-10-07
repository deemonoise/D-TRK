#include "synth_osc.h"
#include "hot.h"
#include <math.h>

namespace mt {

float gSine[kSineLen + 1];
namespace {
struct SineInit {
  SineInit() {
    for (int i = 0; i <= kSineLen; ++i) gSine[i] = sinf(6.2831853f * i / kSineLen);
  }
} sineInit;
}  // namespace

MT_HOT float noteHz(float note) { return 440.f * exp2f((note - 69.f) * (1.f / 12.f)); }

}  // namespace mt
