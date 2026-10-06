#include "sdcard.h"
#include <SD.h>
#include <SPI.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pins.h"
#include "preset_paths.h"

namespace hw {
namespace {

SPIClass* spi;
bool ready;

int cmpName(const void* a, const void* b) {
  return strcasecmp(static_cast<const char*>(a), static_cast<const char*>(b));
}

// Adds name (len chars) to names[0..n): appended while there is room, otherwise it replaces the
// alphabetically last entry if it sorts before it. So a full list holds the first max names in
// sorted order, not the first max the directory happens to return. Returns the new count.
int keepName(char (*names)[kNameMax], int n, int max, const char* name, size_t len) {
  if (max <= 0) return n;
  if (n < max) {
    memcpy(names[n], name, len);
    names[n][len] = 0;
    return n + 1;
  }
  int last = 0;
  for (int i = 1; i < n; ++i)
    if (strcasecmp(names[i], names[last]) > 0) last = i;
  char tmp[kNameMax];
  memcpy(tmp, name, len);
  tmp[len] = 0;
  if (strcasecmp(tmp, names[last]) < 0) memcpy(names[last], tmp, len + 1);
  return n;
}

}  // namespace

bool sdBegin() {
  if (!spi) {
    spi = new SPIClass(FSPI);
    spi->begin(pins::kSdClk, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
  }
  if (ready) SD.end();
  ready = SD.begin(pins::kSdCs, *spi, 20000000);
  if (!ready) {
    SD.end();
    ready = SD.begin(pins::kSdCs, *spi, 4000000);
  }
  if (ready && SD.cardType() == CARD_NONE) {
    SD.end();
    ready = false;
  }
  if (!ready) {
    Serial.println("sd: no card");
    return false;
  }
  if (!SD.exists("/projects")) SD.mkdir("/projects");
  if (!SD.exists("/midi")) SD.mkdir("/midi");
  if (!SD.exists("/samples")) SD.mkdir("/samples");
  if (!SD.exists("/wavetables")) SD.mkdir("/wavetables");
  if (!SD.exists("/presets")) SD.mkdir("/presets");
  for (int t = 0; t < static_cast<int>(mt::InstrType::Count); ++t) {
    if (!mt::presetTypeHas(static_cast<mt::InstrType>(t))) continue;
    const char* root = mt::presetRoot(static_cast<mt::InstrType>(t));
    if (!SD.exists(root)) SD.mkdir(root);
  }
  return true;
}

bool sdReady() { return ready; }

fs::FS& sdFs() { return SD; }

bool FileSink::write(const void* d, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(d);
  while (n > 0) {
    if (n_ == sizeof(buf_) && !flush()) return false;
    const size_t k = n < sizeof(buf_) - n_ ? n : sizeof(buf_) - n_;
    memcpy(buf_ + n_, b, k);
    n_ += k;
    b += k;
    n -= k;
  }
  return true;
}

bool FileSink::flush() {
  if (n_ == 0) return true;
  const bool ok = f_.write(buf_, n_) == n_;
  n_ = 0;
  return ok;
}

bool FileSource::read(void* d, size_t n) {
  uint8_t* b = static_cast<uint8_t*>(d);
  while (n > 0) {
    if (pos_ == len_) {
      const int r = f_.read(buf_, sizeof(buf_));
      if (r <= 0) return false;
      len_ = static_cast<size_t>(r);
      pos_ = 0;
    }
    const size_t k = n < len_ - pos_ ? n : len_ - pos_;
    memcpy(b, buf_ + pos_, k);
    pos_ += k;
    b += k;
    n -= k;
  }
  return true;
}

bool FileSource::skip(size_t n) {
  const size_t inBuf = len_ - pos_;
  if (n <= inBuf) {
    pos_ += n;
    return true;
  }
  n -= inBuf;
  pos_ = len_ = 0;
  const size_t at = f_.position(), size = f_.size();
  if (at > size || n > size - at) return false;  // past the end (no overflow on huge n)
  return f_.seek(at + n);
}

bool sdNextEntry(fs::File& dir, String& name, bool& isDir) {
  isDir = false;
  const String path = dir.getNextFileName(&isDir);
  if (path.isEmpty()) return false;
  const int slash = path.lastIndexOf('/');
  name = slash >= 0 ? path.substring(slash + 1) : path;
  return true;
}

int sdList(const char* dir, const char* ext, char (*names)[kNameMax], int max, bool (*accept)(const char*)) {
  if (!ready) return 0;
  fs::File d = SD.open(dir);
  if (!d || !d.isDirectory()) return 0;
  const size_t extLen = strlen(ext);
  int n = 0;
  String entry;
  bool isDir;
  char base[kNameMax];
  while (sdNextEntry(d, entry, isDir)) {
    if (isDir) continue;
    const char* name = entry.c_str();
    const size_t len = strlen(name);
    if (len <= extLen || len - extLen >= kNameMax || name[0] == '.') continue;
    if (strcasecmp(name + len - extLen, ext) != 0) continue;
    memcpy(base, name, len - extLen);
    base[len - extLen] = 0;
    if (accept && !accept(base)) continue;
    n = keepName(names, n, max, base, len - extLen);
  }
  qsort(names, n, kNameMax, cmpName);
  return n;
}

int sdListFiles(const char* dir, const char* const* exts, int extCount, char (*names)[kNameMax], int max) {
  if (!ready) return 0;
  fs::File d = SD.open(dir);
  if (!d || !d.isDirectory()) return 0;
  int n = 0;
  String entry;
  bool isDir;
  while (sdNextEntry(d, entry, isDir)) {
    if (isDir) continue;
    const char* name = entry.c_str();
    const size_t len = strlen(name);
    if (len >= kNameMax || name[0] == '.') continue;
    bool match = false;
    for (int e = 0; e < extCount && !match; ++e) {
      const size_t el = strlen(exts[e]);
      match = len > el && strcasecmp(name + len - el, exts[e]) == 0;
    }
    if (!match) continue;
    n = keepName(names, n, max, name, len);
  }
  qsort(names, n, kNameMax, cmpName);
  return n;
}

int sdListDirs(const char* dir, char (*names)[kNameMax], int max) {
  if (!ready) return 0;
  fs::File d = SD.open(dir);
  if (!d || !d.isDirectory()) return 0;
  int n = 0;
  String entry;
  bool isDir;
  while (sdNextEntry(d, entry, isDir)) {
    if (!isDir) continue;
    const char* name = entry.c_str();
    const size_t len = strlen(name);
    if (len == 0 || len >= kNameMax || name[0] == '.') continue;
    n = keepName(names, n, max, name, len);
  }
  qsort(names, n, kNameMax, cmpName);
  return n;
}

}  // namespace hw
