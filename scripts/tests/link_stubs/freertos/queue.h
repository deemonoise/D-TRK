#pragma once
#include "FreeRTOS.h"
#include <cstring>
#include <deque>
#include <vector>
struct TestQueue {
  size_t width;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = TestQueue *;
inline QueueHandle_t xQueueCreate(int, size_t n) {
  return new TestQueue{n, {}};
}
inline int xQueueSend(QueueHandle_t q, const void *p, TickType_t) {
  q->items.emplace_back((const uint8_t *)p, (const uint8_t *)p + q->width);
  return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q, void *p, TickType_t) {
  if (!q || q->items.empty())
    return pdFALSE;
  memcpy(p, q->items.front().data(), q->width);
  q->items.pop_front();
  return pdTRUE;
}
