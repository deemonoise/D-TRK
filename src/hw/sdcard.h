#pragma once
#include <FS.h>
#include "project_io.h"

namespace hw {

// microSD over SPI. UI task only; the engine never touches the card.
bool sdBegin();  // (re)mounts, creates /projects and /midi
bool sdReady();
fs::FS& sdFs();

// Buffered adapters; check ok() / close() for write errors.
class FileSink : public mt::ByteSink {
 public:
  explicit FileSink(fs::File& f) : f_(f) {}
  bool write(const void* d, size_t n) override;
  bool flush();

 private:
  fs::File& f_;
  uint8_t buf_[512];
  size_t n_ = 0;
};

class FileSource : public mt::ByteSource {
 public:
  explicit FileSource(fs::File& f) : f_(f) {}
  bool read(void* d, size_t n) override;
  bool skip(size_t n) override;

 private:
  fs::File& f_;
  uint8_t buf_[512];
  size_t pos_ = 0, len_ = 0;
};

constexpr int kNameMax = 32;  // file name without the extension, incl. terminator
// Names in dir ending with ext (e.g. ".mtp"), extension stripped, sorted. Returns the count.
// accept (optional) filters base names, e.g. storage::validName.
int sdList(const char* dir, const char* ext, char (*names)[kNameMax], int max,
           bool (*accept)(const char* base) = nullptr);

}  // namespace hw
