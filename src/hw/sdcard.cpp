#include "sdcard.h"
#include <SD.h>
#include <SPI.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pins.h"

namespace hw {
namespace {

SPIClass* spi;
bool ready;

int cmpName(const void* a, const void* b) {
  return strcasecmp(static_cast<const char*>(a), static_cast<const char*>(b));
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
  const size_t target = f_.position() + n;
  return target <= f_.size() && f_.seek(target);
}

int sdList(const char* dir, const char* ext, char (*names)[kNameMax], int max, bool (*accept)(const char*)) {
  if (!ready) return 0;
  fs::File d = SD.open(dir);
  if (!d || !d.isDirectory()) return 0;
  const size_t extLen = strlen(ext);
  int n = 0;
  while (n < max) {
    fs::File f = d.openNextFile();
    if (!f) break;
    if (f.isDirectory()) continue;
    const char* name = f.name();
    const size_t len = strlen(name);
    if (len <= extLen || len - extLen >= kNameMax || name[0] == '.') continue;
    if (strcasecmp(name + len - extLen, ext) != 0) continue;
    memcpy(names[n], name, len - extLen);
    names[n][len - extLen] = 0;
    if (accept && !accept(names[n])) continue;
    ++n;
  }
  qsort(names, n, kNameMax, cmpName);
  return n;
}

}  // namespace hw
