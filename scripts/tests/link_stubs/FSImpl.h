#pragma once
#include "Arduino.h"
#include <ctime>
#include <memory>
namespace fs {
enum SeekMode { SeekSet, SeekCur, SeekEnd };
class FileImpl;
using FileImplPtr = std::shared_ptr<FileImpl>;
class FileImpl {
public:
  virtual ~FileImpl() = default;
  virtual size_t write(const uint8_t *, size_t) = 0;
  virtual size_t read(uint8_t *, size_t) = 0;
  virtual void flush() = 0;
  virtual bool seek(uint32_t, SeekMode) = 0;
  virtual size_t position() const = 0;
  virtual size_t size() const = 0;
  virtual bool setBufferSize(size_t) = 0;
  virtual void close() = 0;
  virtual time_t getLastWrite() = 0;
  virtual const char *path() const = 0;
  virtual const char *name() const = 0;
  virtual boolean isDirectory() = 0;
  virtual FileImplPtr openNextFile(const char *) = 0;
  virtual boolean seekDir(long) = 0;
  virtual String getNextFileName() = 0;
  virtual String getNextFileName(bool *) = 0;
  virtual void rewindDirectory() = 0;
  virtual operator bool() = 0;
};
class FSImpl {
public:
  virtual ~FSImpl() = default;
  virtual FileImplPtr open(const char *, const char *, bool) = 0;
  virtual bool exists(const char *) = 0;
  virtual bool rename(const char *, const char *) = 0;
  virtual bool remove(const char *) = 0;
  virtual bool mkdir(const char *) = 0;
  virtual bool rmdir(const char *) = 0;
};
class File {
  FileImplPtr p;

public:
  File() {}
  File(FileImplPtr v) : p(v) {}
  explicit operator bool() const { return p && bool(*p); }
  size_t write(const uint8_t *d, size_t n) {
    return *this ? p->write(d, n) : 0;
  }
  int read(uint8_t *d, size_t n) { return *this ? int(p->read(d, n)) : -1; }
  void flush() {
    if (p)
      p->flush();
  }
  void close() {
    if (p) {
      p->close();
      p.reset();
    }
  }
  bool seek(uint32_t n) { return p && p->seek(n, SeekSet); }
  size_t position() const { return p ? p->position() : 0; }
  size_t size() const { return p ? p->size() : 0; }
  bool isDirectory() { return p && p->isDirectory(); }
  String getNextFileName(bool *d) {
    return p ? p->getNextFileName(d) : String();
  }
};
class FS {
  std::shared_ptr<FSImpl> p;

public:
  explicit FS(std::shared_ptr<FSImpl> v) : p(v) {}
  File open(const char *s, const char *m = "r") { return p->open(s, m, false); }
  bool exists(const char *s) { return p->exists(s); }
  bool rename(const char *a, const char *b) { return p->rename(a, b); }
  bool remove(const char *s) { return p->remove(s); }
  bool mkdir(const char *s) { return p->mkdir(s); }
  bool rmdir(const char *s) { return p->rmdir(s); }
};
} // namespace fs
#define FILE_WRITE "w"
#define FILE_READ "r"
