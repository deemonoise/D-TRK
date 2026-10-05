#include "synth_osc.h"
#include <math.h>

namespace mt {

float noteHz(float note) { return 440.f * exp2f((note - 69.f) * (1.f / 12.f)); }

}  // namespace mt
