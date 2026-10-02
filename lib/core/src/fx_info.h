#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

const char* fxName(Fx f);                    // 3 chars, "..." for None
uint8_t fxDefault(Fx f);                     // value set when the command is chosen
void fxFormat(Fx f, uint8_t v, char out[5]);  // 3 chars, right-aligned
uint8_t fxStep(Fx f, uint8_t v, int delta);  // encoder step, clamped, signed-aware
Fx fxNextCmd(Fx f, int delta);               // cycles None..PGM

}  // namespace mt
