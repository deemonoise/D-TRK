#include "text_store.h"
#include <stdio.h>
#include <string.h>

namespace storage {
namespace {

bool textPath(char* out, size_t cap, const char* dir, const char* name, const char* ext) {
  const int n = snprintf(out, cap, "%s/%s%s", dir, name, ext);
  return n > 0 && static_cast<size_t>(n) < cap;
}

}  // namespace

int listTexts(const char* dir, const char* ext, char (*names)[hw::kNameMax], int max) {
  if (!hw::sdReady() || !hw::sdFs().exists(dir)) return 0;
  return hw::sdList(dir, ext, names, max, validName);
}

Result loadText(const char* dir, const char* name, const char* ext, char* out, int cap) {
  if (cap < 1) return Result::BadFile;
  out[0] = '\0';
  if (!hw::sdReady()) return Result::NoSd;
  char path[192];
  if (!textPath(path, sizeof(path), dir, name, ext)) return Result::BadFile;
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) return Result::NotFound;
  const size_t n = f.read(reinterpret_cast<uint8_t*>(out), static_cast<size_t>(cap - 1));
  f.close();
  out[n] = '\0';
  return Result::Ok;
}

Result saveText(const char* dir, const char* name, const char* ext, const char* text) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  fs::FS& fs = hw::sdFs();
  if (!fs.exists("/presets")) fs.mkdir("/presets");
  if (!fs.exists(dir) && !fs.mkdir(dir)) return Result::WriteFail;
  char tmp[192], dst[192], bak[192];
  if (!textPath(tmp, sizeof(tmp), dir, name, ".tmp") || !textPath(dst, sizeof(dst), dir, name, ext) ||
      !textPath(bak, sizeof(bak), dir, name, ".bak"))
    return Result::BadFile;
  if (fs.exists(tmp)) fs.remove(tmp);
  fs::File f = fs.open(tmp, FILE_WRITE);
  if (!f) return Result::WriteFail;
  const size_t len = strlen(text);
  bool ok = f.write(reinterpret_cast<const uint8_t*>(text), len) == len && f.write('\n') == 1;
  f.close();
  // The old file moves to .bak first, so a failed rename can put it back.
  bool hadOld = false;
  if (ok && fs.exists(dst)) {
    if (fs.exists(bak)) fs.remove(bak);
    ok = fs.rename(dst, bak);
    hadOld = ok;
  }
  ok = ok && fs.rename(tmp, dst);
  if (!ok) {
    if (fs.exists(tmp)) fs.remove(tmp);
    if (hadOld && !fs.exists(dst)) fs.rename(bak, dst);
    return Result::WriteFail;
  }
  if (hadOld) fs.remove(bak);
  return Result::Ok;
}

}  // namespace storage
