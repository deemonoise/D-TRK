#pragma once
#include "Arduino.h"
inline void AudioNoInterrupts() { ++rec_test::audioGuards; }
inline void AudioInterrupts() { --rec_test::audioGuards; }
inline unsigned AudioMemoryUsageMax() { return 0; }
