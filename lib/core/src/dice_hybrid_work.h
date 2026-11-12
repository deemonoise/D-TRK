#pragma once
#include "bass_gen.h"
namespace mt {
// Shared generator workspace,allocated with DiceSnap in PSRAM,never on the UI stack.
struct DiceHybridWork {
  Pattern pattern;
  ArpPattern arp;
  BassPattern bass;
};
}
