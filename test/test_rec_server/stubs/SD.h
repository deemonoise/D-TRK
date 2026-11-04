#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>
#define O_RDWR 1
#define O_CREAT 2
#define O_TRUNC 4
namespace rec_test {
struct Node { std::vector<uint8_t> bytes; };
extern bool failWrite, failSeal;
extern unsigned dataWrites;
}
class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(std::shared_ptr<rec_test::Node> n) : node_(n) {}
  explicit operator bool() const { return bool(node_); }
  size_t write(const void* p, size_t n) {
    if (!node_ || rec_test::failWrite) return 0;
    if (pos_ >= 104) ++rec_test::dataWrites;
    node_->bytes.resize(std::max(node_->bytes.size(), pos_ + n));
    std::copy_n(static_cast<const uint8_t*>(p), n, node_->bytes.data() + pos_);
    pos_ += n;
    return n;
  }
  bool close() { node_.reset(); return !rec_test::failSeal; }
  bool truncate(uint64_t n) {
    if (!node_ || rec_test::failSeal) return false;
    node_->bytes.resize(static_cast<size_t>(n));
    return true;
  }
  bool seekSet(uint64_t n) { pos_ = static_cast<size_t>(n); return bool(node_); }
  bool preAllocate(uint64_t) { return true; }
 private:
  std::shared_ptr<rec_test::Node> node_;
  size_t pos_ = 0;
};
struct RecTestVolume {
  std::map<std::string, std::shared_ptr<rec_test::Node>> nodes;
  bool exists(const char* p) { return nodes.count(p); }
  bool mkdir(const char*, bool) { return true; }
  FsFile open(const char* p, int) {
    auto n = std::make_shared<rec_test::Node>();
    nodes[p] = n;
    return FsFile(n);
  }
  bool remove(const char* p) { return nodes.erase(p) != 0; }
  bool rename(const char* from, const char* to) {
    if (rec_test::failSeal || !nodes.count(from)) return false;
    nodes[to] = nodes[from];
    nodes.erase(from);
    return true;
  }
  uint64_t bytesPerCluster() { return 32768; }
  uint32_t freeClusterCount() { return 65536; }
};
struct RecTestSd { RecTestVolume sdfs; };
extern RecTestSd SD;
