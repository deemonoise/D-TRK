#pragma once
#include "FS.h"
class Preferences {
 public:
  bool begin(const char* ns, bool ro) {
    dir_ = fs::resolve("/settings") / ns;
    std::error_code ec;
    if (!ro) fs::disk::create_directories(dir_, ec);
    return fs::disk::is_directory(dir_, ec);
  }
  void end() {}
  bool isKey(const char* key) const {
    std::error_code ec;
    return fs::disk::is_regular_file(dir_ / key, ec);
  }
  size_t getBytesLength(const char* key) const {
    fs::File file(dir_ / key, FILE_READ);
    return file.size();
  }
  size_t getBytes(const char* key, void* buf, size_t size) const {
    fs::File file(dir_ / key, FILE_READ);
    return file.read(buf, size);
  }
  size_t getString(const char* key, char* buf, size_t size) const {
    if (!size) return 0;
    auto n = getBytes(key, buf, size - 1);
    buf[n] = 0;
    return n;
  }
  size_t putBytes(const char* key, const void* buf, size_t size) {
    auto path = dir_ / key;
    auto tmp = path.string() + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return 0;
    bool ok = fwrite(buf, 1, size, f) == size;
    if (fclose(f) != 0 || !ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
      std::remove(tmp.c_str());
      return 0;
    }
    return size;
  }
  uint8_t getUChar(const char* key, uint8_t def) const {
    uint8_t v;
    return getBytes(key, &v, 1) == 1 ? v : def;
  }
  void putUChar(const char* key, uint8_t v) { putBytes(key, &v, 1); }
  void remove(const char* key) {
    std::error_code ec;
    fs::disk::remove(dir_ / key, ec);
  }

 private:
  fs::disk::path dir_;
};
