#pragma once
#include <FS.h>
#include "project_io.h"

namespace hw {

// microSD over SPI. UI task only; the engine never touches the card.
bool sdBegin();  // (re)mounts (retries with a bus reset), creates /projects, /midi, /samples, /wavetables and /presets/<TYPE>
bool sdReady();
// After a read / write error: remounts only if the card no longer answers (a bad file is not a
// reason to drop the mount). Returns sdReady().
bool sdRecover();
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

// Next entry of an open directory: its name (no path) and whether it is a folder; false at the end.
// Entries are not opened, so a name the card cannot read (non-ASCII, shown as "?") is skipped by the
// caller's filters instead of ending the listing.
bool sdNextEntry(fs::File& dir, String& name, bool& isDir);

constexpr int kNameMax = 96;  // listed file / folder name, incl. terminator
// The listings below keep the alphabetically first max names when a folder holds more.
// Names in dir ending with ext (e.g. ".mtp"), extension stripped, sorted. Returns the count.
// accept (optional) filters base names, e.g. storage::validName.
int sdList(const char* dir, const char* ext, char (*names)[kNameMax], int max,
           bool (*accept)(const char* base) = nullptr);

// Files in dir ending with one of exts (any case), full names with the extension, sorted.
int sdListFiles(const char* dir, const char* const* exts, int extCount, char (*names)[kNameMax], int max);

// Subfolders of dir (names shorter than kNameMax, no hidden ones), sorted. Returns the count.
int sdListDirs(const char* dir, char (*names)[kNameMax], int max);

}  // namespace hw
