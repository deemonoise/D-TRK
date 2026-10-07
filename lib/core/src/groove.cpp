#include "groove.h"

namespace mt {
namespace {

#define FLAT {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}
// MPC-style swing: every second sixteenth late, by the same amount as Swing 54 / 58 / 62 / 66.
#define SWING(x) {0, x, 0, x, 0, x, 0, x, 0, x, 0, x, 0, x, 0, x}
#define ACC {100, 82, 92, 82, 100, 82, 92, 82, 100, 82, 92, 82, 100, 82, 92, 82}

constexpr Groove kGrooves[] = {
    {"OFF", {0}, FLAT},
    {"MPC 54", SWING(8), ACC},
    {"MPC 58", SWING(16), ACC},
    {"MPC 62", SWING(24), ACC},
    {"MPC 66", SWING(32), ACC},
    // Triplet shuffle: the off-sixteenth lands on the last triplet of the eighth.
    {"SHUFFLE", SWING(33), {100, 70, 95, 70, 100, 70, 95, 70, 100, 70, 95, 70, 100, 70, 95, 70}},
    // Off-beats a little early: drives forward.
    {"PUSH", {0, -8, -4, -8, 0, -8, -4, -8, 0, -8, -4, -8, 0, -8, -4, -8}, ACC},
    // Everything but the beats late: relaxed.
    {"LAID BACK", {0, 12, 8, 14, 0, 12, 8, 14, 0, 12, 8, 14, 0, 12, 8, 14},
     {100, 85, 90, 80, 100, 85, 90, 80, 100, 85, 90, 80, 100, 85, 90, 80}},
    // Uneven, fixed (not random): every pass the same stumble.
    {"DRUNK", {0, 14, -6, 20, 4, -10, 16, 2, -4, 18, 6, -8, 10, 22, -2, 12},
     {100, 76, 88, 70, 96, 82, 74, 90, 100, 72, 86, 78, 94, 68, 84, 80}},
    // Hip-hop: swung sixteenths, the backbeat (steps 5, 13) a hair late and loud.
    {"BOOM BAP", {0, 20, 0, 20, 6, 20, 0, 20, 0, 20, 0, 20, 6, 20, 0, 20},
     {100, 70, 85, 70, 110, 70, 85, 70, 100, 70, 85, 70, 110, 70, 85, 70}},
    // House: straight, the off-beat eighths (hats) accented.
    {"HOUSE", {0}, {90, 70, 110, 70, 90, 70, 110, 70, 90, 70, 110, 70, 90, 70, 110, 70}},
};
constexpr int kCount = sizeof(kGrooves) / sizeof(kGrooves[0]);

#undef FLAT
#undef SWING
#undef ACC

}  // namespace

int grooveCount() { return kCount; }

const Groove& grooveAt(int i) { return kGrooves[i > 0 && i < kCount ? i : 0]; }

}  // namespace mt
