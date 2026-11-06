#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using portMUX_TYPE = int;
using TaskHandle_t = void *;
constexpr int pdTRUE = 1, pdFALSE = 0, configMAX_PRIORITIES = 25,
              portMUX_INITIALIZER_UNLOCKED = 0;
#define pdMS_TO_TICKS(n) (n)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
