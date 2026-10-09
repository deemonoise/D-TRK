#pragma once
#include <stdint.h>
#include <functional>

namespace net {

// Web page with the firmware update (and ArduinoOTA). Call webBegin() once the network is up and
// webPoll() from the UI task.
struct WebHooks {
  std::function<void(const char*)> log;   // one short ASCII line per finished action
  std::function<void(const char*)> busy;  // progress text that must show right away
};

bool webBegin(const WebHooks& hooks);
void webPoll();  // serves requests, ArduinoOTA, delayed restart after a firmware update
void webEnd();
bool webRunning();

}  // namespace net
