#pragma once
#include "model.h"

namespace ui {

constexpr const char* kRootNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* kResNames[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"};
static_assert(sizeof(kResNames) / sizeof(kResNames[0]) == static_cast<int>(mt::Resolution::Count), "res names");

inline const char* resName(mt::Resolution r) {
  const int i = static_cast<int>(r);
  return i < static_cast<int>(mt::Resolution::Count) ? kResNames[i] : "?";
}

}  // namespace ui
