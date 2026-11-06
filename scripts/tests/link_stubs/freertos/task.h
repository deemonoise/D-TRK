#pragma once
#include "FreeRTOS.h"
inline void vTaskDelay(TickType_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t,
                                   void *, int, TaskHandle_t *, int) {
  return pdTRUE;
}
