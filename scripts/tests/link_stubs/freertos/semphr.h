#pragma once
#include "FreeRTOS.h"
#include <functional>
struct TestSemaphore {
  int count;
};
using SemaphoreHandle_t = TestSemaphore *;
inline std::function<void(TickType_t)> testWait;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  return new TestSemaphore{1};
}
inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  return new TestSemaphore{0};
}
inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) {
  if (!s->count && wait && testWait)
    testWait(wait);
  if (!s->count)
    return pdFALSE;
  --s->count;
  return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t s) {
  s->count = 1;
  return pdTRUE;
}
