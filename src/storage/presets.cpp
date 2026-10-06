#include "presets.h"
#include <stdio.h>
#include <string.h>
#include "hw/sdcard.h"
#include "preset_io.h"

namespace storage {
namespace {

constexpr size_t kPathMax = 192;

struct PresetPath {
  char s[kPathMax];
  bool ok;
  PresetPath(const char* dir, const char* name, const char* ext) : ok(mt::presetJoin(s, sizeof(s), dir, name, ext)) {}
};

Result readPreset(const char* path, mt::Instrument& out) {
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) return Result::ReadFail;
  hw::FileSource src(f);
  const mt::LoadErr e = mt::loadPreset(src, out);
  f.close();
  switch (e) {
    case mt::LoadErr::Ok: return Result::Ok;
    case mt::LoadErr::BadCrc: return Result::BadCrc;
    case mt::LoadErr::BadVersion: return Result::BadVersion;
    default: return Result::BadFile;
  }
}

}  // namespace

bool presetExists(const char* dir, const char* name) {
  if (!hw::sdReady()) return false;
  const PresetPath p(dir, name, ".mti");
  return p.ok && hw::sdFs().exists(p.s);
}

Result savePreset(const char* dir, const char* name, const mt::Instrument& m) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  const PresetPath tmp(dir, name, ".tmp"), mti(dir, name, ".mti");
  if (!tmp.ok || !mti.ok) return Result::BadFile;
  fs::FS& fs = hw::sdFs();
  if (fs.exists(tmp.s)) fs.remove(tmp.s);
  fs::File f = fs.open(tmp.s, FILE_WRITE);
  if (!f) return Result::WriteFail;
  hw::FileSink sink(f);
  bool ok = mt::savePreset(m, sink) && sink.flush();
  f.close();
  mt::Instrument check;
  ok = ok && readPreset(tmp.s, check) == Result::Ok;
  if (ok && fs.exists(mti.s)) ok = fs.remove(mti.s);
  ok = ok && fs.rename(tmp.s, mti.s);
  if (!ok) {
    if (fs.exists(tmp.s)) fs.remove(tmp.s);
    return Result::WriteFail;
  }
  return Result::Ok;
}

Result loadPreset(const char* dir, const char* name, mt::Instrument& out) {
  if (!hw::sdReady()) return Result::NoSd;
  const PresetPath p(dir, name, ".mti");
  if (!p.ok || !hw::sdFs().exists(p.s)) return Result::NotFound;
  return readPreset(p.s, out);
}

Result removePreset(const char* dir, const char* name) {
  if (!hw::sdReady()) return Result::NoSd;
  const PresetPath p(dir, name, ".mti");
  if (!p.ok || !hw::sdFs().exists(p.s)) return Result::NotFound;
  return hw::sdFs().remove(p.s) ? Result::Ok : Result::WriteFail;
}

Result makePresetDir(const char* dir, const char* name) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  const PresetPath p(dir, name, "");
  // Longer than a browser's folder buffer: it could not be entered.
  if (!p.ok || strlen(p.s) >= static_cast<size_t>(mt::kPresetDirMax)) return Result::BadFile;
  if (hw::sdFs().exists(p.s)) return Result::Ok;
  return hw::sdFs().mkdir(p.s) ? Result::Ok : Result::WriteFail;
}

}  // namespace storage
