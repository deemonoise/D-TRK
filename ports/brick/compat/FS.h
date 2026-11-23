#pragma once
#include <filesystem>
#include <memory>

#include "Arduino.h"
#define FILE_READ "rb"
#define FILE_WRITE "wb"
#define FILE_APPEND "ab"
namespace fs {
namespace disk = std::filesystem;
disk::path root();
disk::path resolve(const char* path);
class File {
 public:
  File() = default;
  File(disk::path path, const char* mode);
  explicit operator bool() const { return bool(handle_) || bool(directory_); }
  bool isDirectory() const { return bool(directory_); }
  size_t size() const;
  size_t position() const { return handle_ ? ftell(handle_.get()) : 0; }
  bool seek(size_t pos) { return handle_ && fseek(handle_.get(), pos, SEEK_SET) == 0; }
  size_t read(void* out, size_t n) { return handle_ ? fread(out, 1, n, handle_.get()) : 0; }
  int read() { return handle_ ? fgetc(handle_.get()) : EOF; }
  size_t write(const void* in, size_t n) { return handle_ ? fwrite(in, 1, n, handle_.get()) : 0; }
  size_t write(uint8_t value) { return write(&value, 1); }
  size_t print(const char* in) { return write(in, strlen(in)); }
  void close() {
    handle_.reset();
    directory_.reset();
  }
  const char* name() const { return name_.c_str(); }
  String getNextFileName(bool* dir);
  File openNextFile();

 private:
  std::shared_ptr<FILE> handle_;
  std::shared_ptr<disk::directory_iterator> directory_;
  disk::path path_;
  std::string name_;
  disk::path next();
};
class FS {
 public:
  File open(const char* path, const char* mode = FILE_READ) { return File(resolve(path), mode); }
  bool exists(const char* path);
  bool mkdir(const char* path);
  bool remove(const char* path);
  bool rmdir(const char* path);
  bool rename(const char* from, const char* to);
};
}  // namespace fs
using fs::File;
