#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"

// UART to the synth board (UART_NUM_2, pins::kLinkTx / kLinkRx). No TX buffer: a write returns once
// the bytes are in the hardware FIFO, so a queued bulk frame never delays an event frame by more
// than its own length.
namespace slink {

bool uartBegin(uint32_t baud);
// Up to n bytes, waiting at most wait ticks for the first; returns the count (0 on timeout).
int uartRead(uint8_t* d, int n, TickType_t wait);
int uartWrite(const uint8_t* d, int n);

}  // namespace slink
