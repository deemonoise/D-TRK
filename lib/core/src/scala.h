#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Scala .scl tuning files (https://www.huygens-fokker.org/scala/scl_format.html): '!' lines are
// comments anywhere; the first other line is the description, the next the number of pitches, then one
// pitch per line (its first word: with a '.' cents, else a ratio a/b or an integer a; the rest of the line
// is ignored). The last pitch is the period. Pure, no I/O.
constexpr int kSclDescMax = 32;
constexpr int kSclFileMax = 8192;  // bytes read from a card file

struct SclTable {
  char desc[kSclDescMax + 1] = {};
  uint8_t count = 0;
  int32_t mc[kTuneDegMax] = {};  // 1/1000 cent
};

enum class ParseErr : uint8_t {
  Ok,
  NoCount,    // no line with the number of pitches
  BadCount,   // that line is not a number
  Empty,      // 0 pitches
  TooMany,    // more than kTuneDegMax
  BadValue,   // a pitch line is not cents / a ratio, or out of range (+-9600 c)
  Truncated,  // fewer pitch lines than the count
  BadPeriod,  // the last pitch <= 0 or above kTunePeriodMaxMc
  TooBig,     // the file is larger than kSclFileMax (set by the reader, not parseScl)
};
const char* sclErrName(ParseErr e);  // "TOO MANY NOTES" etc., for toasts

// errLine (optional): the 1-based line of the error, 0 when there is none.
ParseErr parseScl(const char* text, int len, SclTable& out, int* errLine = nullptr);
// A parsed table into a project tuning named name (cut to 16 characters); root is kept.
void sclToTuning(const SclTable& s, const char* name, TuningTable& t);

}  // namespace mt
