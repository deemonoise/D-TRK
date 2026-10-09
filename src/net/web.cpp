#include "web.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <stdio.h>
#include <string.h>
#include "file_rules.h"
#include "link/link.h"
#include "link/synth_fw.h"
#include "storage/remote_fs.h"
#include "web_page.h"

namespace net {
namespace {

constexpr uint32_t kRestartDelayMs = 1000;

WebServer* srv = nullptr;
WebHooks hooks;
uint32_t restartAt = 0;
char busyMsg[32];

struct Fw {
  bool started;
  int code;
  char err[48];
  uint32_t lastKb;
} fw;

// The synth board's firmware: the .hex uploaded to its card, then FwFromFile (webPoll), then its
// reboot awaited. /api/update-synth/status reports the state to the page.
enum class SynthState : uint8_t { Idle, Upload, Flash, Reboot, Done, Error };
struct SynthUpd {
  SynthState state;
  bool started;
  int code;
  char msg[48];
  uint32_t done, total;
  uint32_t oldBoot, deadline;
  fs::File file;
  uint32_t lastKb;
} syn;
bool synthPending = false;  // uploaded: FwFromFile at the next webPoll
bool inSynthFlash = false;  // webPoll is inside FwFromFile (status requests are still served)

// Device log lines: ASCII only (the screen font has no Cyrillic).
void log(const char* fmt, const char* a, const char* b = "") {
  char line[96];
  snprintf(line, sizeof(line), fmt, a, b);
  if (hooks.log) hooks.log(line);
}

void logFail(const char* name, int code) {
  char c[8];
  snprintf(c, sizeof(c), "%d", code);
  log("! %s: HTTP %s", name, c);
}

void busy(const char* msg) {
  strlcpy(busyMsg, msg, sizeof(busyMsg));
  if (hooks.busy) hooks.busy(busyMsg);
}

void reply(int code, const char* text) { srv->send(code, "text/plain; charset=utf-8", text); }

void page() { srv->send_P(200, "text/html; charset=utf-8", kWebPage); }

void fwFail(int code, const char* msg) {
  if (fw.code) return;
  fw.code = code;
  strlcpy(fw.err, msg, sizeof(fw.err));
  if (Update.isRunning()) Update.abort();
}

void handleFwChunk() {
  HTTPUpload& u = srv->upload();
  switch (u.status) {
    case UPLOAD_FILE_START:
      fw = Fw{};
      fw.started = true;
      if (inSynthFlash) return fwFail(409, "the synth board is updating");
      if (!mt::webFirmwareName(u.filename.c_str())) return fwFail(400, "a .bin file is needed");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) return fwFail(500, Update.errorString());
      busy("FIRMWARE 0 KB");
      break;
    case UPLOAD_FILE_WRITE: {
      if (fw.code) return;
      if (Update.write(u.buf, u.currentSize) != u.currentSize) return fwFail(500, Update.errorString());
      const uint32_t kb = static_cast<uint32_t>((u.totalSize + u.currentSize) / 1024);
      if (kb - fw.lastKb >= 64) {
        fw.lastKb = kb;
        char msg[32];
        snprintf(msg, sizeof(msg), "FIRMWARE %lu KB", static_cast<unsigned long>(kb));
        busy(msg);
      }
      break;
    }
    case UPLOAD_FILE_END:
      if (fw.code) return;
      if (!Update.end(true)) fwFail(500, Update.errorString());  // checks the image
      break;
    case UPLOAD_FILE_ABORTED: fwFail(400, "upload aborted"); break;
  }
}

void handleFwDone() {
  if (!fw.started) {
    reply(400, "no file");
    return;
  }
  fw.started = false;
  if (fw.code) {
    logFail("firmware", fw.code);
    reply(fw.code, fw.err);
    return;
  }
  reply(200, "OK");
  log("+ firmware%s", "");
  busy("FIRMWARE OK, RESTART");
  restartAt = millis() + kRestartDelayMs;
  if (!restartAt) restartAt = 1;
}

void synthFail(int code, const char* msg) {
  if (syn.code) return;
  syn.code = code;
  syn.state = SynthState::Error;
  strlcpy(syn.msg, msg, sizeof(syn.msg));
  if (syn.file) syn.file.close();
  storage::remoteFs().remove(synthfw::kHexTmp);
}

bool synthBusy() {
  return syn.state == SynthState::Upload || syn.state == SynthState::Flash || syn.state == SynthState::Reboot ||
         synthPending;
}

void handleSynthChunk() {
  HTTPUpload& u = srv->upload();
  fs::FS& fs = storage::remoteFs();
  switch (u.status) {
    case UPLOAD_FILE_START:
      if (synthBusy() || inSynthFlash) {
        syn.started = true;
        syn.code = 409;
        strlcpy(syn.msg, "an update is running", sizeof(syn.msg));
        return;
      }
      syn = SynthUpd{};
      syn.started = true;
      syn.state = SynthState::Upload;
      if (!mt::webSynthFirmwareName(u.filename.c_str())) return synthFail(400, "a .hex file is needed");
      if (!slink::synthUp()) return synthFail(503, "no synth board");
      fs.mkdir("/firmware");
      fs.remove(synthfw::kHexTmp);
      syn.file = fs.open(synthfw::kHexTmp, FILE_WRITE);
      if (!syn.file) return synthFail(500, "cannot write the card");
      busy("SYNTH FW 0 KB");
      break;
    case UPLOAD_FILE_WRITE: {
      if (syn.code) return;
      if (syn.file.write(u.buf, u.currentSize) != u.currentSize) return synthFail(500, "card write failed");
      const uint32_t kb = static_cast<uint32_t>((u.totalSize + u.currentSize) / 1024);
      syn.done = kb * 1024;
      if (kb - syn.lastKb >= 64) {
        syn.lastKb = kb;
        char msg[32];
        snprintf(msg, sizeof(msg), "SYNTH FW %lu KB", static_cast<unsigned long>(kb));
        busy(msg);
      }
      break;
    }
    case UPLOAD_FILE_END:
      if (syn.code) return;
      syn.file.close();
      fs.remove(synthfw::kHexPath);
      if (!fs.rename(synthfw::kHexTmp, synthfw::kHexPath)) return synthFail(500, "card rename failed");
      break;
    case UPLOAD_FILE_ABORTED: synthFail(400, "upload aborted"); break;
  }
}

void handleSynthDone() {
  if (!syn.started) {
    reply(400, "no file");
    return;
  }
  syn.started = false;
  if (syn.code) {
    logFail("synth firmware", syn.code);
    reply(syn.code, syn.msg);
    return;
  }
  syn.state = SynthState::Flash;
  syn.done = syn.total = 0;
  strlcpy(syn.msg, "checking the file", sizeof(syn.msg));
  synthPending = true;
  reply(200, "OK");
}

void handleSynthStatus() {
  static const char* const kNames[] = {"idle", "upload", "flash", "reboot", "done", "error"};
  char js[128];
  const unsigned pct = syn.total ? static_cast<unsigned>(static_cast<uint64_t>(syn.done) * 100 / syn.total) : 0;
  snprintf(js, sizeof(js), "{\"state\":\"%s\",\"pct\":%u,\"msg\":\"%s\"}",
           kNames[static_cast<int>(syn.state)], pct, syn.msg);
  srv->send(200, "application/json", js);
}

void synthProgress(uint32_t done, uint32_t total, void*) {
  syn.done = done;
  syn.total = total;
  static unsigned lastPct = 101;
  const unsigned pct = total ? static_cast<unsigned>(static_cast<uint64_t>(done) * 100 / total) : 0;
  if (pct != lastPct) {
    lastPct = pct;
    char msg[32];
    snprintf(msg, sizeof(msg), "SYNTH FW %u%%", pct);
    busy(msg);
  }
  srv->handleClient();  // the page's status polls (inSynthFlash keeps a second update out)
}

// webPoll(): the uploaded .hex into the synth board, then its reboot awaited (no blocking).
void pollSynth() {
  if (synthPending) {
    synthPending = false;
    syn.oldBoot = slink::bootId();
    uint8_t res = 0;
    uint32_t bytes = 0;
    inSynthFlash = true;
    const synthfw::Result r = synthfw::update(synthfw::kHexPath, res, bytes, synthProgress, nullptr);
    inSynthFlash = false;
    if (r == synthfw::Result::Ok) {
      syn.state = SynthState::Reboot;
      strlcpy(syn.msg, "the synth board restarts", sizeof(syn.msg));
      syn.deadline = millis() + synthfw::kRebootWaitMs;
      busy("SYNTH FW OK, RESTART");
    } else {
      syn.state = SynthState::Error;
      strlcpy(syn.msg, r == synthfw::Result::NoSynth ? "NO ANSWER" : synthfw::rejectText(res), sizeof(syn.msg));
      log("! synth firmware: %s", syn.msg);
    }
    return;
  }
  if (syn.state != SynthState::Reboot) return;
  if (synthfw::rebooted(syn.oldBoot)) {
    syn.state = SynthState::Done;
    snprintf(syn.msg, sizeof(syn.msg), "synth firmware %s", slink::synthFw());
    log("+ synth firmware %s", slink::synthFw());
  } else if (static_cast<int32_t>(millis() - syn.deadline) >= 0) {
    syn.state = SynthState::Error;
    strlcpy(syn.msg, "the synth board did not come back", sizeof(syn.msg));
    log("! synth firmware: %s", "no answer after the update");
  }
}

void beginOta() {
  ArduinoOTA.setHostname("d-trk");  // also starts mDNS: http://d-trk.local
  ArduinoOTA.onStart([] { busy("FIRMWARE..."); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned int lastPct = 101;
    const unsigned int pct = total ? done * 100 / total : 0;
    if (pct == lastPct || (pct % 5 && pct != 100)) return;
    lastPct = pct;
    char msg[32];
    snprintf(msg, sizeof(msg), "FIRMWARE %u%%", pct);
    busy(msg);
  });
  ArduinoOTA.onEnd([] { busy("FIRMWARE OK, RESTART"); });
  ArduinoOTA.onError([](ota_error_t e) {
    char msg[32];
    snprintf(msg, sizeof(msg), "! OTA error %d", static_cast<int>(e));
    if (hooks.log) hooks.log(msg);
  });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
}

}  // namespace

bool webBegin(const WebHooks& h) {
  if (srv) return true;
  hooks = h;
  restartAt = 0;
  srv = new WebServer(80);
  if (!srv) return false;
  srv->on("/", HTTP_GET, page);
  srv->on("/firmware", HTTP_GET, page);
  srv->on("/api/update", HTTP_POST, handleFwDone, handleFwChunk);
  srv->on("/api/update-synth", HTTP_POST, handleSynthDone, handleSynthChunk);
  srv->on("/api/update-synth/status", HTTP_GET, handleSynthStatus);
  srv->onNotFound([] { reply(404, "no such page"); });
  srv->begin();
  beginOta();
  return true;
}

void webPoll() {
  if (!srv) return;
  srv->handleClient();
  ArduinoOTA.handle();
  pollSynth();
  if (restartAt && static_cast<int32_t>(millis() - restartAt) >= 0) ESP.restart();
}

void webEnd() {
  if (!srv) return;
  ArduinoOTA.end();  // stops mDNS too
  srv->stop();
  delete srv;
  srv = nullptr;
  if (Update.isRunning()) Update.abort();
  if (syn.file) {
    syn.file.close();
    storage::remoteFs().remove(synthfw::kHexTmp);
  }
  syn = SynthUpd{};
  synthPending = false;
  hooks = WebHooks{};
}

bool webRunning() { return srv != nullptr; }

}  // namespace net
