#include "wifi.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>

namespace net {
namespace {

bool connecting = false;
bool failed = false;
bool up = false;
uint32_t startMs = 0;

}  // namespace

bool loadCreds(Creds& c) {
  memset(&c, 0, sizeof(c));
  Preferences p;
  if (!p.begin("wifi", true)) return false;
  p.getString("ssid", c.ssid, sizeof(c.ssid));
  p.getString("pass", c.pass, sizeof(c.pass));
  p.end();
  return c.ssid[0] != 0;
}

void saveCreds(const Creds& c) {
  Preferences p;
  if (!p.begin("wifi", false)) return;
  p.putString("ssid", c.ssid);
  p.putString("pass", c.pass);
  p.end();
}

int scan(char (*ssids)[kSsidMax], int max) {
  WiFi.mode(WIFI_STA);
  const int16_t found = WiFi.scanNetworks();
  int n = 0;
  // Results come sorted by RSSI: keep the first (strongest) of each SSID.
  for (int16_t i = 0; i < found && n < max; ++i) {
    const String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (int j = 0; j < n && !dup; ++j) dup = strcmp(ssids[j], s.c_str()) == 0;
    if (dup) continue;
    strlcpy(ssids[n++], s.c_str(), kSsidMax);
  }
  WiFi.scanDelete();
  return n;
}

void connect(const Creds& c) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("d-trk");
  WiFi.setSleep(false);  // modem sleep drops ARP replies: browsers get "address unreachable"
  WiFi.disconnect();
  WiFi.begin(c.ssid, c.pass);
  connecting = true;
  failed = false;
  up = false;
  startMs = millis();
}

Link link() {
  if (WiFi.status() == WL_CONNECTED) {
    connecting = false;
    up = true;
    return Link::Up;
  }
  if (up) {  // lost the router: the driver reconnects on its own, give it kConnectMs
    up = false;
    connecting = true;
    startMs = millis();
  }
  if (connecting && millis() - startMs >= kConnectMs) {
    connecting = false;
    failed = true;
    WiFi.disconnect();
  }
  if (connecting) return Link::Connecting;
  return failed ? Link::Failed : Link::Off;
}

void ip(char* buf, int cap) {
  if (WiFi.status() != WL_CONNECTED) {
    buf[0] = 0;
    return;
  }
  strlcpy(buf, WiFi.localIP().toString().c_str(), cap);
}

void off() {
  connecting = false;
  failed = false;
  up = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace net
