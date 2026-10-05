#pragma once
#include <stdint.h>
#include <functional>

namespace net {

// Web page + file API + firmware update (page and ArduinoOTA). Call webBegin() once the
// link is up and webPoll() from the UI task: the card stays single-owner.
struct WebHooks {
  std::function<void(const char*)> log;   // one short ASCII line per finished action
  std::function<void(const char*)> busy;  // progress text that must show right away
  std::function<const char*()> project;   // base name of the open project file, "" when none
};

enum class OpenFile : uint8_t { Untouched, Replaced, Removed };

bool webBegin(const WebHooks& hooks);
void webPoll();  // serves requests, ArduinoOTA, delayed restart after a firmware update
void webEnd();
bool webRunning();
// What happened to <project>.mtp since webBegin() (last change wins).
OpenFile webOpenFile();

}  // namespace net
