// wt32-echo: UART link check against a Teensy running teensy41-echo. Sends 64 KB of an LFSR
// pattern in 256-byte bursts, compares the echo, logs bytes/s and mismatches every 5 s.
#ifdef LINK_ECHO_TEST
#include <Arduino.h>
#include "link_frame.h"
#include "link_uart.h"

#ifndef LINK_ECHO_BAUD
#define LINK_ECHO_BAUD mt::link::kBaud
#endif

namespace slink {

void echoTest() {
  if (!uartBegin(LINK_ECHO_BAUD)) {
    Serial.println("echo: uart init failed");
    return;
  }
  constexpr int kTotal = 64 * 1024, kBurst = 256;
  static uint8_t tx[kBurst], rx[kBurst];
  for (;;) {
    uint16_t lfsr = 0xACE1;
    uint32_t mismatches = 0, missing = 0;
    const uint32_t t0 = micros();
    for (int done = 0; done < kTotal; done += kBurst) {
      for (int i = 0; i < kBurst; ++i) {
        lfsr = static_cast<uint16_t>((lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u));
        tx[i] = static_cast<uint8_t>(lfsr);
      }
      uartWrite(tx, kBurst);
      int got = 0;
      while (got < kBurst) {
        const int r = uartRead(rx + got, kBurst - got, pdMS_TO_TICKS(20));
        if (r == 0) break;
        got += r;
      }
      missing += kBurst - got;
      for (int i = 0; i < got; ++i) mismatches += rx[i] != tx[i];
    }
    const uint32_t us = micros() - t0;
    Serial.printf("echo %lu baud: %lu B/s round trip, %lu mismatches, %lu missing\n",
                  static_cast<unsigned long>(LINK_ECHO_BAUD), static_cast<unsigned long>(kTotal * 1000000ull / us),
                  static_cast<unsigned long>(mismatches), static_cast<unsigned long>(missing));
    delay(5000);
  }
}

}  // namespace slink
#endif
