#pragma once
#include <stdint.h>
#include "bytebeat_vm.h"

namespace mt {

// Built-in bytebeat formulas. Append-only: the index is stored in projects and presets.
struct ByteFormula {
  const char* name;  // up to 8 characters, unique
  const char* expr;  // infix source, see bytebeat_compile.h
  uint8_t mode;      // kByteUnsigned / kByteSigned
  uint8_t a, b, c;   // default macros A B C (0..127); for the classics they give the original
};

int byteFormulaCount();
// Out of range = entry 0.
const ByteFormula& byteFormula(int i);
// Compiled entry i (out of range = entry 0). The table is compiled on the first call of any of
// these functions; call byteFormulaCount() once at boot so the audio path never compiles.
const ByteCode& byteCode(int i);

}  // namespace mt
