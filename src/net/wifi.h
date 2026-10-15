#pragma once
#include <stdint.h>

namespace net {

// Home Wi-Fi (STA). UI task only. Radio is off except inside the FILE -> Wi-Fi dialog.

struct Creds {
  char ssid[33];
  char pass[64];
};

bool loadCreds(Creds& c);  // false when nothing is stored (NVS namespace "wifi")
void saveCreds(const Creds& c);

constexpr int kSsidMax = 33;
// Blocking scan (~2-4 s). Unique non-empty SSIDs, strongest first. Returns the count.
int scan(char (*ssids)[kSsidMax], int max);

enum class Link : uint8_t { Off, Connecting, Up, Failed };

void connect(const Creds& c);  // starts connecting; poll link()
Link link();                  // Failed after kConnectMs without an address
void ip(char* buf, int cap);  // "192.168.1.23" or ""
void off();                   // disconnect and power the radio down
// Boot, before the big internal blocks: starts and stops the Wi-Fi stack once so its lasting
// allocations (tcpip task, netif, event loop) land at boot, not inside the space the Wi-Fi dialog
// frees by moving the synth, sequencer and reverb out (they could not move back).
void prime();

constexpr uint32_t kConnectMs = 15000;

}  // namespace net
