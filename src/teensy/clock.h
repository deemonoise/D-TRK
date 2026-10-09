#pragma once
#include <stdint.h>

// micros() widened to 64 bits (it wraps after 71 min). Any context; call at least once an hour.
uint64_t micros64();
