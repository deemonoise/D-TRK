#pragma once
#include <stdint.h>
namespace rec_test {
extern uint32_t nowMs;
extern int audioGuards;
}
inline uint32_t millis() { return rec_test::nowMs; }
inline uint32_t micros() { return rec_test::nowMs * 1000; }
#define F_CPU_ACTUAL 600000000u
