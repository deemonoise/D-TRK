#include "storage.h"
#include <Arduino.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "audio/bank.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "file_rules.h"
#include "hw/sdcard.h"
#include "project_io.h"
#include "sample_set.h"
#include "wav.h"
#include "wt_mip.h"

namespace storage {
namespace {

constexpr const char* kDir = "/projects/";
constexpr const char* kLast = "/last.txt";
constexpr uint32_t kStopWaitMs = 200;
constexpr uint32_t kStopRepostMs = 20;
constexpr int kMaxFolderFiles = mt::kProjSamples + 64;  // listed when cleaning a sample folder

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
static_assert(sizeof(mt::Step) == 14, "Step layout changed: update snapshot() and project_io");
static_assert(sizeof(mt::TrackCfg) == 20, "TrackCfg changed: update snapshot() and project_io");
static_assert(sizeof(mt::Pattern) == 3 + mt::kTracks + sizeof(mt::Step) * mt::kTracks * mt::kMaxSteps,
              "Pattern changed: update snapshot() and project_io");
static_assert(sizeof(mt::Instrument) == 362, "Instrument changed: update snapshot() and project_io");
static_assert(sizeof(mt::Project) == 469840, "Project changed: update snapshot() and project_io");

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
  memcpy(out.chainTr, live.chainTr, sizeof(out.chainTr));
  memcpy(out.chainRep, live.chainRep, sizeof(out.chainRep));
  memcpy(out.chainScene, live.chainScene, sizeof(out.chainScene));
  memcpy(out.scenes, live.scenes, sizeof(out.scenes));
  out.songMode = live.songMode;
  memcpy(out.instruments, live.instruments, sizeof(out.instruments));
  out.masterVol = live.masterVol;
  out.preview = live.preview;
  out.dlyTime = live.dlyTime;
  out.dlyFb = live.dlyFb;
  out.dlyTone = live.dlyTone;
  out.dlyLevel = live.dlyLevel;
  out.rvbSize = live.rvbSize;
  out.rvbDamp = live.rvbDamp;
  out.rvbLevel = live.rvbLevel;
  out.compAmt = live.compAmt;
  out.compRel = live.compRel;
  out.scTrack = live.scTrack;
  out.scDepth = live.scDepth;
  memcpy(out.samples, live.samples, sizeof(out.samples));
  out.sampleCount = live.sampleCount;
  memcpy(out.wavetables, live.wavetables, sizeof(out.wavetables));
  out.wavetableCount = live.wavetableCount;
  out.hasSampleList = live.hasSampleList;
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

// /projects/<project>/<sample>.wav
struct SamplePath {
  char s[64];
  SamplePath(const char* project, const char* sample) { snprintf(s, sizeof(s), "%s%s/%s.wav", kDir, project, sample); }
};

// /projects/<project>/wt/<wavetable>.wav (sources of the project's wavetables)
struct WtPath {
  char s[64];
  WtPath(const char* project, const char* wt) { snprintf(s, sizeof(s), "%s%s/wt/%s.wav", kDir, project, wt); }
};

// Folder the live project's sample files are in: the one it was loaded from until a save has
// written its own folder completely (a Save As that failed half way copies from here again).
char srcFolder[17] = "";

// True if path holds frames of data with this crc: checked by the "mtcr" chunk and the file size (no
// data read).
bool fileCurrent(const char* path, uint32_t crc, uint32_t frames) {
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) return false;
  mt::WavInfo w;
  hw::FileSource src(f);
  const bool ok = mt::wavParse(src, w) == mt::WavErr::Ok && w.channels == 1 && w.bits == 16 && w.hasCrc &&
                  w.crc == crc && w.frames() == frames &&
                  f.size() >= static_cast<size_t>(w.dataOffset) + static_cast<size_t>(frames) * 2;
  f.close();
  return ok;
}
bool fileCurrent(const char* path, const mt::ProjSample& s) { return fileCurrent(path, s.crc, s.frames); }
bool fileCurrent(const char* path, const mt::ProjWavetable& t) { return fileCurrent(path, t.crc, mt::kWtSrcSamples); }

// Removes the files in dir (".wav" / ".tmp") that are not ".wav" of a name keep() accepts: leftovers of
// interrupted writes and of entries no longer in the list.
template <typename Keep>
Result cleanFolder(const char* dir, Keep keep) {
  auto* names = static_cast<char(*)[hw::kNameMax]>(heap_caps_malloc(kMaxFolderFiles * hw::kNameMax, MALLOC_CAP_SPIRAM));
  if (!names) return Result::NoMemory;
  static constexpr const char* kExts[] = {".wav", ".tmp"};
  const int n = hw::sdListFiles(dir, kExts, 2, names, kMaxFolderFiles);
  Result r = Result::Ok;
  for (int k = 0; k < n; ++k) {
    char base[hw::kNameMax];
    strlcpy(base, names[k], sizeof(base));
    const bool wav = strcasecmp(base + strlen(base) - 4, ".wav") == 0;
    base[strlen(base) - 4] = 0;  // ".wav" / ".tmp", any case
    if (wav && keep(base)) continue;
    char path[hw::kNameMax + 40];
    snprintf(path, sizeof(path), "%s/%s", dir, names[k]);
    if (!hw::sdFs().remove(path)) r = Result::WriteFail;
  }
  heap_caps_free(names);
  return r;
}

// The names the project's .bak lists (nullptr: no readable .bak). Their files stay in the folder:
// falling back to the .bak (autoload or by hand) must find them.
mt::ProjectFileNames* bakNames(const char* name) {
  const Path bak(name, ".bak");
  if (!hw::sdFs().exists(bak.s)) return nullptr;
  fs::File f = hw::sdFs().open(bak.s, FILE_READ);
  if (!f) return nullptr;
  auto* n = new (std::nothrow) mt::ProjectFileNames();
  if (!n) return nullptr;
  hw::FileSource src(f);
  if (mt::readProjectFileNames(src, *n) != mt::LoadErr::Ok) {
    delete n;
    return nullptr;
  }
  return n;
}

// Copies src to dst through dst.tmp (replaces dst at the end).
bool copyFile(const char* src, const char* dst) {
  fs::FS& fs = hw::sdFs();
  fs::File in = fs.open(src, FILE_READ);
  if (!in) return false;
  char tmp[72];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  if (fs.exists(tmp)) fs.remove(tmp);
  fs::File out = fs.open(tmp, FILE_WRITE);
  constexpr size_t kBuf = 2048;
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kBuf, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  bool ok = out && buf;
  while (ok) {
    const int n = in.read(buf, kBuf);
    if (n < 0) ok = false;
    if (n <= 0) break;
    ok = out.write(buf, n) == static_cast<size_t>(n);
  }
  heap_caps_free(buf);
  in.close();
  if (out) out.close();
  if (ok) {
    if (fs.exists(dst)) fs.remove(dst);
    ok = fs.rename(tmp, dst);
  }
  if (!ok && fs.exists(tmp)) fs.remove(tmp);
  return ok;
}

// Bank progress of one sample -> SyncProgress.
struct PullCtx {
  SyncProgress cb;
  void* ctx;
  const char* name;
};

void pullProgress(uint32_t done, uint32_t total, void* ctx) {
  const PullCtx& c = *static_cast<PullCtx*>(ctx);
  if (c.cb) c.cb(c.name, done, total, c.ctx);
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
    case Result::SamplesNotSaved: return "SAMPLES NOT SAVED";
    case Result::AudioBusy: return "AUDIO BUSY";
    case Result::DiskFull: return "DISK FULL";
    case Result::Cancelled: return "CANCELLED";
    case Result::Capped: return "SAMPLE CAP";
    case Result::BankFull: return "BANK FULL";
    case Result::NoBank: return "NO SAMPLE BANK";
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

bool validName(const char* name) { return mt::projectBaseValid(name); }

bool exists(const char* name, bool bak) {
  if (!hw::sdReady()) return false;
  return hw::sdFs().exists(Path(name, bak ? ".bak" : ".mtp").s);
}

Result save(mt::Project& live, const char* name, SyncProgress cb, void* ctx) {
  if (!hw::sdReady()) return Result::NoSd;
  char nm[17];
  if (!sanitize(name, nm)) return Result::BadFile;
  char from[sizeof(live.name)];  // folder the samples come from (Save As copies missing ones over)
  strlcpy(from, srcFolder[0] ? srcFolder : live.name, sizeof(from));
  mt::Project* snap = allocProject();
  if (!snap) return Result::NoMemory;
  // Wavetables no instrument uses are not saved (nor kept in the folder).
  engine::lockProject();
  mt::projWtPrune(live);
  engine::unlockProject();
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
  const Result fr = syncFolder(live, from, cb, ctx);
  if (fr != Result::Ok) {
    Serial.printf("storage: sample folder of %s: %s\n", nm, resultText(fr));
    return Result::SamplesNotSaved;
  }
  strlcpy(srcFolder, nm, sizeof(srcFolder));
  return Result::Ok;
}

Result syncFolder(const mt::Project& live, const char* from, SyncProgress cb, void* ctx) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!mt::projectBaseValid(live.name)) return Result::BadFile;
  char dir[32];
  snprintf(dir, sizeof(dir), "%s%s", kDir, live.name);
  fs::FS& fs = hw::sdFs();
  const bool had = fs.exists(dir);
  if (!had && live.sampleCount == 0 && live.wavetableCount == 0) return Result::Ok;
  if (!had && !fs.mkdir(dir)) return Result::WriteFail;
  Result r = Result::Ok;
  for (int i = 0; i < live.sampleCount; ++i) {
    const mt::ProjSample& s = live.samples[i];
    const int j = audio::bankMounted() ? mt::projSampleBank(live, audio::bank(), i) : -1;
    const SamplePath path(live.name, s.name);
    if (j < 0) {
      // Missing (not cached): whatever file it has stays; after Save As it gets the old folder's.
      if (!from || strcasecmp(from, live.name) == 0 || !mt::projectBaseValid(from) || fileCurrent(path.s, s))
        continue;
      const SamplePath old(from, s.name);
      if (!fs.exists(old.s)) continue;
      if (cb) cb(s.name, 0, 1, ctx);
      if (!copyFile(old.s, path.s)) {
        Serial.printf("storage: copy %s: failed\n", old.s);
        r = Result::WriteFail;
      }
      continue;
    }
    if (fileCurrent(path.s, s)) continue;
    if (cb) cb(s.name, 0, s.frames * 2, ctx);
    const audio::BankResult br = audio::exportWav(j, path.s);
    if (br != audio::BankResult::Ok) {
      Serial.printf("storage: write %s: %s\n", path.s, audio::bankResultText(br));
      r = Result::WriteFail;
    }
    if (cb) cb(s.name, s.frames * 2, s.frames * 2, ctx);
  }
  // Wavetable sources, in the "wt" subfolder (their names may equal sample names).
  char wtDir[40];
  snprintf(wtDir, sizeof(wtDir), "%s/wt", dir);
  const bool hadWt = fs.exists(wtDir);
  const bool wtDirOk = hadWt || live.wavetableCount == 0 || fs.mkdir(wtDir);
  if (!wtDirOk) r = Result::WriteFail;
  for (int i = 0; wtDirOk && i < live.wavetableCount; ++i) {
    const mt::ProjWavetable& t = live.wavetables[i];
    const int j = audio::bankMounted() ? mt::projWtBank(live, audio::bank(), i) : -1;
    const WtPath path(live.name, t.name);
    if (j < 0) {
      // Missing (not cached): as for samples, Save As copies the old folder's file.
      if (!from || strcasecmp(from, live.name) == 0 || !mt::projectBaseValid(from) || fileCurrent(path.s, t)) continue;
      const WtPath old(from, t.name);
      if (!fs.exists(old.s)) continue;
      if (cb) cb(t.name, 0, 1, ctx);
      if (!copyFile(old.s, path.s)) {
        Serial.printf("storage: copy %s: failed\n", old.s);
        r = Result::WriteFail;
      }
      continue;
    }
    if (fileCurrent(path.s, t)) continue;
    constexpr uint32_t kBytes = mt::kWtSrcSamples * 2;
    if (cb) cb(t.name, 0, kBytes, ctx);
    const audio::BankResult br = audio::exportWt(t.name, live, path.s);
    if (br != audio::BankResult::Ok) {
      Serial.printf("storage: write %s: %s\n", path.s, audio::bankResultText(br));
      r = Result::WriteFail;
    }
    if (cb) cb(t.name, kBytes, kBytes, ctx);
  }
  // A failed write may leave the only good copy of a listed sample under another name: clean up next time.
  if (r != Result::Ok) return r;
  // Files of samples / wavetables in neither the list nor the .bak's, leftovers of interrupted writes.
  mt::ProjectFileNames* bak = bakNames(live.name);
  r = cleanFolder(dir, [&](const char* base) { return mt::projSampleFind(live, base) >= 0 || (bak && bak->has(base, false)); });
  if (r == Result::Ok && (hadWt || live.wavetableCount > 0))
    r = cleanFolder(wtDir, [&](const char* base) { return mt::projWtFind(live, base) >= 0 || (bak && bak->has(base, true)); });
  delete bak;
  return r;
}

Result pullSamples(mt::Project& live, int* missing, SyncProgress cb, void* ctx) {
  int miss = 0;
  Result r = Result::Ok;
  bool migrated = false;
  bool old = !live.hasSampleList;  // file older than the sample list
  if (old) {
    old = false;
    for (const mt::Instrument& in : live.instruments) old = old || in.sample[0];
  }
  if (old) {
    int m = 0;
    const audio::BankResult br = audio::migrateProject(live, &m);
    if (br == audio::BankResult::Busy) r = Result::EngineBusy;
    miss += m;
    if (br == audio::BankResult::Ok) live.hasSampleList = migrated = true;
    Serial.printf("storage: migrated %s: %d samples, %d missing\n", live.name, live.sampleCount, m);
  }
  const bool sd = hw::sdReady() && mt::projectBaseValid(live.name);
  for (int i = 0; i < live.sampleCount; ++i) {
    const mt::ProjSample& s = live.samples[i];
    if (!audio::bankMounted()) {
      ++miss;
      continue;
    }
    if (mt::projSampleBank(live, audio::bank(), i) >= 0) continue;
    SamplePath path(live.name, s.name);
    // Not in the own folder: an old sample may be in the folder of the project that migrated it.
    char proj[17], old[17];
    if (sd && !hw::sdFs().exists(path.s) && audio::legacySource(s.crc, s.frames, proj, old) &&
        strcasecmp(proj, live.name) != 0)
      path = SamplePath(proj, old);
    if (!sd || !hw::sdFs().exists(path.s)) {
      ++miss;
      continue;
    }
    PullCtx pc{cb, ctx, s.name};
    if (cb) cb(s.name, 0, 1, ctx);
    audio::ImportOut out{};
    const uint32_t want = s.crc;
    const audio::BankResult br = audio::importToCache(path.s, live, out, &want, pullProgress, &pc);
    // Other data under this name (edited on a computer): the list keeps its crc, the sample is missing.
    if (br != audio::BankResult::Ok || out.crc != s.crc || out.frames != s.frames) {
      Serial.printf("storage: pull %s: %s\n", path.s,
                    br != audio::BankResult::Ok ? audio::bankResultText(br) : "OTHER DATA");
      if (br == audio::BankResult::Busy) r = Result::EngineBusy;
      ++miss;
    }
  }
  for (int i = 0; i < live.wavetableCount; ++i) {
    const mt::ProjWavetable& t = live.wavetables[i];
    if (!audio::bankMounted()) {
      ++miss;
      continue;
    }
    if (mt::projWtBank(live, audio::bank(), i) >= 0) continue;
    const WtPath path(live.name, t.name);
    if (!sd || !hw::sdFs().exists(path.s)) {
      ++miss;
      continue;
    }
    PullCtx pc{cb, ctx, t.name};
    if (cb) cb(t.name, 0, 1, ctx);
    uint32_t crc = 0;
    const uint32_t want = t.crc;
    const audio::BankResult br = audio::importWtToCache(path.s, live, crc, &want, pullProgress, &pc);
    // Other data under this name: the list keeps its crc, the wavetable is missing (its osc is silent).
    if (br != audio::BankResult::Ok || crc != t.crc) {
      Serial.printf("storage: pull %s: %s\n", path.s,
                    br != audio::BankResult::Ok ? audio::bankResultText(br) : "OTHER DATA");
      if (br == audio::BankResult::Busy) r = Result::EngineBusy;
      ++miss;
    }
  }
  // Migrated samples get their files now (the .mtp is written by the next save).
  if (migrated && r == Result::Ok && syncFolder(live, live.name, cb, ctx) != Result::Ok) r = Result::SamplesNotSaved;
  if (missing) *missing = miss;
  return r;
}

Result installProject(const char* tmpPath, const char* fileName) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!mt::webFileAllowed(mt::WebDir::Projects, fileName)) return Result::BadFile;
  mt::Project* chk = allocProject();
  if (!chk) return Result::NoMemory;
  const Result r = readFile(tmpPath, *chk);
  freeProject(chk);
  if (r != Result::Ok) return r;

  char dst[48];
  snprintf(dst, sizeof(dst), "%s%s", kDir, fileName);
  fs::FS& fs = hw::sdFs();
  const size_t n = strlen(fileName);
  const bool mtp = n > 4 && strcmp(fileName + n - 4, ".mtp") == 0;
  char bak[48] = {0};
  bool rotated = false;
  if (fs.exists(dst)) {
    if (mtp) {
      snprintf(bak, sizeof(bak), "%s%.*s.bak", kDir, static_cast<int>(n - 4), fileName);
      if (fs.exists(bak)) fs.remove(bak);
      if (!fs.rename(dst, bak)) return Result::WriteFail;
      rotated = true;
    } else if (!fs.remove(dst)) {
      return Result::WriteFail;
    }
  }
  if (!fs.rename(tmpPath, dst)) {
    if (rotated) fs.rename(bak, dst);
    return Result::WriteFail;
  }
  return Result::Ok;
}

Result load(mt::Project& live, const char* name, bool fromBak, int* missing, SyncProgress cb, void* ctx) {
  if (missing) *missing = 0;
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
  if (r != Result::Ok) return r;
  strlcpy(srcFolder, name, sizeof(srcFolder));
  // The project is loaded either way; only a failed folder write is reported.
  return pullSamples(live, missing, cb, ctx) == Result::SamplesNotSaved ? Result::SamplesNotSaved : Result::Ok;
}

Result newProject(mt::Project& live) {
  if (!stopEngine()) return Result::EngineBusy;
  srcFolder[0] = 0;
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
  strlcpy(srcFolder, nm, sizeof(srcFolder));
  Serial.printf("storage: loaded %s%s\n", nm, bak ? ".bak" : "");
  if (fromBak) *fromBak = bak;
  return true;
}

}  // namespace storage
