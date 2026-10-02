#include "storage.h"
#include <Arduino.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "project_io.h"

namespace storage {
namespace {

constexpr const char* kDir = "/projects/";
constexpr const char* kLast = "/last.txt";
constexpr uint32_t kStopWaitMs = 200;
constexpr uint32_t kStopRepostMs = 20;

struct Path {
  char s[48];
  Path(const char* name, const char* ext) { snprintf(s, sizeof(s), "%s%s%s", kDir, name, ext); }
};

// Temporary Project in PSRAM; at most one exists at a time.
mt::Project* allocProject() {
  void* m = heap_caps_malloc(sizeof(mt::Project), MALLOC_CAP_SPIRAM);
  return m ? new (m) mt::Project() : nullptr;
}

void freeProject(mt::Project* p) {
  if (!p) return;
  p->~Project();
  heap_caps_free(p);
}

// If one of these fires, a field was added/removed: update snapshot() below and
// lib/core/src/project_io.cpp (save + load + its tests) before changing the expected size.
static_assert(sizeof(mt::Step) == 6, "Step layout changed: update snapshot() and project_io");
static_assert(sizeof(mt::TrackCfg) == 17, "TrackCfg changed: update snapshot() and project_io");
static_assert(sizeof(mt::Pattern) == 3 + sizeof(mt::Step) * mt::kTracks * mt::kMaxSteps,
              "Pattern changed: update snapshot() and project_io");
static_assert(sizeof(mt::Project) == 98576, "Project changed: update snapshot() and project_io");

// Pattern by pattern, so the engine never waits for a whole-project copy.
void snapshot(const mt::Project& live, mt::Project& out) {
  engine::lockProject();
  memcpy(out.name, live.name, sizeof(out.name));
  out.bpm = live.bpm;
  out.scaleRoot = live.scaleRoot;
  out.scaleType = live.scaleType;
  memcpy(out.tracks, live.tracks, sizeof(out.tracks));
  memcpy(out.chain, live.chain, sizeof(out.chain));
  out.chainLen = live.chainLen;
  out.songMode = live.songMode;
  engine::unlockProject();
  for (int i = 0; i < mt::kPatterns; ++i) {
    engine::lockProject();
    out.patterns[i] = live.patterns[i];
    engine::unlockProject();
  }
}

// Swaps in a new project with the engine stopped. Ties may still hold notes of the old data.
void afterReplace(const mt::Project& live) {
  engine::post(engine::Cmd::ReleaseTies);
  engine::post(engine::Cmd::SetBpm, live.bpm);
}

bool writeLast(const char* name) {
  fs::File f = hw::sdFs().open(kLast, FILE_WRITE);
  if (!f) return false;
  const size_t n = strlen(name);
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(name), n) == n;
  f.close();
  return ok;
}

Result readFile(const char* path, mt::Project& out) {
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) return Result::ReadFail;
  hw::FileSource src(f);
  const mt::LoadErr e = mt::loadProject(src, out);
  f.close();
  switch (e) {
    case mt::LoadErr::Ok: return Result::Ok;
    case mt::LoadErr::BadCrc: return Result::BadCrc;
    case mt::LoadErr::BadVersion: return Result::BadVersion;
    default: return Result::BadFile;
  }
}

Result writeTmp(const char* path, const mt::Project& p) {
  fs::FS& fs = hw::sdFs();
  if (fs.exists(path)) fs.remove(path);
  fs::File f = fs.open(path, FILE_WRITE);
  if (!f) return Result::WriteFail;
  hw::FileSink sink(f);
  const bool ok = mt::saveProject(p, sink) && sink.flush();
  f.close();
  return ok ? Result::Ok : Result::WriteFail;
}

}  // namespace

// Re-posts Stop in case the queue was full.
bool stopEngine() {
  const uint32_t t0 = millis();
  uint32_t lastPost = 0;
  bool posted = false;
  for (;;) {
    const engine::Status s = engine::status();
    if (!s.playing && !s.paused) return true;
    const uint32_t now = millis();
    if (now - t0 >= kStopWaitMs) return false;
    if (!posted || now - lastPost >= kStopRepostMs) {
      engine::post(engine::Cmd::Stop);
      lastPost = now;
      posted = true;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

const char* resultText(Result r) {
  switch (r) {
    case Result::Ok: return "OK";
    case Result::NoSd: return "NO SD CARD";
    case Result::NoMemory: return "NO MEMORY";
    case Result::NotFound: return "FILE NOT FOUND";
    case Result::WriteFail: return "WRITE FAILED";
    case Result::ReadFail: return "READ FAILED";
    case Result::BadCrc: return "CRC ERROR";
    case Result::BadVersion: return "FILE TOO NEW";
    case Result::BadFile: return "BAD FILE";
    case Result::EngineBusy: return "ENGINE BUSY";
  }
  return "?";
}

bool sanitize(const char* in, char out[17]) {
  int n = 0;
  for (; *in && n < 16; ++in) {
    const char c = *in;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
      out[n++] = c;
  }
  out[n] = 0;
  return n > 0;
}

bool validName(const char* name) {
  char nm[17];
  return strlen(name) <= 16 && sanitize(name, nm) && strcmp(nm, name) == 0;
}

bool exists(const char* name, bool bak) {
  if (!hw::sdReady()) return false;
  return hw::sdFs().exists(Path(name, bak ? ".bak" : ".mtp").s);
}

Result save(mt::Project& live, const char* name) {
  if (!hw::sdReady()) return Result::NoSd;
  char nm[17];
  if (!sanitize(name, nm)) return Result::BadFile;
  mt::Project* snap = allocProject();
  if (!snap) return Result::NoMemory;
  snapshot(live, *snap);
  memcpy(snap->name, nm, sizeof(nm));

  const Path tmp(nm, ".tmp"), mtp(nm, ".mtp"), bak(nm, ".bak");
  fs::FS& fs = hw::sdFs();
  Result r = writeTmp(tmp.s, *snap);
  if (r == Result::Ok && readFile(tmp.s, *snap) != Result::Ok) r = Result::WriteFail;  // snapshot no longer needed
  freeProject(snap);
  if (r != Result::Ok) {
    fs.remove(tmp.s);
    return r;
  }
  bool rotated = false;
  if (fs.exists(mtp.s)) {
    if (fs.exists(bak.s)) fs.remove(bak.s);
    if (!fs.rename(mtp.s, bak.s)) {
      fs.remove(tmp.s);
      return Result::WriteFail;
    }
    rotated = true;
  }
  if (!fs.rename(tmp.s, mtp.s)) {
    if (rotated) fs.rename(bak.s, mtp.s);  // put the previous version back under its name
    return Result::WriteFail;
  }
  writeLast(nm);

  engine::lockProject();
  memcpy(live.name, nm, sizeof(nm));
  engine::unlockProject();
  return Result::Ok;
}

Result load(mt::Project& live, const char* name, bool fromBak) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;  // would load under a different name
  const Path path(name, fromBak ? ".bak" : ".mtp");
  if (!hw::sdFs().exists(path.s)) return Result::NotFound;
  mt::Project* tmp = allocProject();
  if (!tmp) return Result::NoMemory;
  Result r = readFile(path.s, *tmp);
  if (r == Result::Ok && !stopEngine()) r = Result::EngineBusy;
  if (r == Result::Ok) {
    strlcpy(tmp->name, name, sizeof(tmp->name));  // the file name wins over the stored one
    engine::lockProject();
    live = *tmp;
    engine::unlockProject();
    afterReplace(live);
    writeLast(live.name);
  }
  freeProject(tmp);
  return r;
}

Result newProject(mt::Project& live) {
  if (!stopEngine()) return Result::EngineBusy;
  engine::lockProject();
  live.reset();
  engine::unlockProject();
  afterReplace(live);
  // Otherwise a reboot would autoload the project that was just closed.
  if (hw::sdReady() && hw::sdFs().exists(kLast)) hw::sdFs().remove(kLast);
  return Result::Ok;
}

bool autoload(mt::Project& p, bool* fromBak, Result* failed) {
  if (fromBak) *fromBak = false;
  if (failed) *failed = Result::Ok;
  if (!hw::sdReady()) return false;
  char raw[32] = {0}, nm[17];
  if (!hw::sdFs().exists(kLast)) return false;
  fs::File f = hw::sdFs().open(kLast, FILE_READ);
  if (!f) return false;
  f.read(reinterpret_cast<uint8_t*>(raw), sizeof(raw) - 1);
  f.close();
  if (!sanitize(raw, nm)) return false;
  Result r = readFile(Path(nm, ".mtp").s, p);
  bool bak = false;
  if (r != Result::Ok && r != Result::BadVersion && hw::sdFs().exists(Path(nm, ".bak").s)) {
    r = readFile(Path(nm, ".bak").s, p);
    bak = true;
  }
  if (r != Result::Ok) {
    Serial.printf("storage: autoload %s: %s\n", nm, resultText(r));
    if (failed) *failed = r;
    p.reset();
    return false;
  }
  memcpy(p.name, nm, sizeof(nm));
  Serial.printf("storage: loaded %s%s\n", nm, bak ? ".bak" : "");
  if (fromBak) *fromBak = bak;
  return true;
}

}  // namespace storage
