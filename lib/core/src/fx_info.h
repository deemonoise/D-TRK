#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

const char* fxName(Fx f);                    // 3 chars, "..." for None
const char* fxLongName(Fx f);                // full name, up to 24 chars, "" for None
uint8_t fxDefault(Fx f);                     // value set when the command is chosen
// 3 chars, right-aligned; CHD: 4 chars, left-aligned (type + inversion digit, space for inv 0: "MA7 ", "MA72").
void fxFormat(Fx f, uint8_t v, char out[5]);
uint8_t fxStep(Fx f, uint8_t v, int delta);  // encoder step, clamped, signed-aware
// GAT value v (1..200, see gatePercent) moved by delta detents. Fine: 1 unit per detent (1 % up to
// 100, 7 % above). Coarse (Shift): per detent the shown percent +-10, then the nearest GAT value
// strictly in the turn's direction (always at least a unit, never against the turn; on a tie the
// smaller move), clamped 1..200. Below 100 exact +-10 %; above, 7 % units are nearer: 95 -> 107,
// 100 -> 107, 107 -> 114, 114 -> 107. GRID's GAT value and the piano roll's A + turn share it.
uint8_t gatStep(uint8_t v, int delta, bool coarse);
Fx fxNextCmd(Fx f, int delta);               // cycles every command, grouped (not the enum order)
bool fxSynthOnly(Fx f);                      // SLD..SLC, DLY, DRV..ARM, BIT, SRR, LFO..LFR, PAN, DTY, GTE, MOD: INT tracks only
bool fxDrumOnly(Fx f);                       // ACC: drum tracks only (KIT instrument), ignored elsewhere

}  // namespace mt
