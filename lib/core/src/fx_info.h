#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

const char* fxName(Fx f);                    // 3 chars, "..." for None
const char* fxLongName(Fx f);                // full name, up to 24 chars, "" for None
uint8_t fxDefault(Fx f);                     // value set when the command is chosen
void fxFormat(Fx f, uint8_t v, char out[5]);  // 3 chars, right-aligned
uint8_t fxStep(Fx f, uint8_t v, int delta);  // encoder step, clamped, signed-aware
Fx fxNextCmd(Fx f, int delta);               // cycles every command, grouped (not the enum order)
bool fxSynthOnly(Fx f);                      // SLD..SLC, DLY, DRV..ARM: INT tracks only, ignored on MIDI
bool fxDrumOnly(Fx f);                       // ACC: drum tracks only (KIT instrument), ignored elsewhere

}  // namespace mt
