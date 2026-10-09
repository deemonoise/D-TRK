#include <unity.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>
#include "link_fs.h"

using namespace mt::link;

namespace {

// The synth board's card as a temp folder: "/x" -> root + "/x".
class PosixBackend : public FsBackend {
 public:
  std::string root;
  bool cardIn = true;

  std::string real(const char* p) const { return root + p; }

  int16_t ready() override { return cardIn ? kErrOk : kErrNoCard; }

  int16_t stat(const char* path, bool& isDir, uint32_t& size) override {
    struct stat st;
    if (::stat(real(path).c_str(), &st) != 0) return errno == ENOENT ? kErrNotFound : kErrIo;
    isDir = S_ISDIR(st.st_mode);
    size = isDir ? 0 : static_cast<uint32_t>(st.st_size);
    return kErrOk;
  }

  int16_t open(int slot, const char* path, FsMode mode, uint32_t& size) override {
    const std::string p = real(path);
    if (mode == FsMode::Append) {
      FILE* f = fopen(p.c_str(), "ab");  // creates it
      if (f) fclose(f);
    }
    files[slot] = fopen(p.c_str(), mode == FsMode::Read ? "rb" : (mode == FsMode::Write ? "w+b" : "r+b"));
    if (!files[slot]) return errno == ENOENT ? kErrNotFound : kErrIo;
    fseek(files[slot], 0, SEEK_END);
    size = static_cast<uint32_t>(ftell(files[slot]));
    return kErrOk;
  }

  int read(int slot, uint32_t pos, uint8_t* d, int n) override {
    if (fseek(files[slot], pos, SEEK_SET) != 0) return kErrIo;
    return static_cast<int>(fread(d, 1, n, files[slot]));
  }

  int write(int slot, uint32_t pos, const uint8_t* d, int n) override {
    if (fseek(files[slot], pos, SEEK_SET) != 0) return kErrIo;
    return static_cast<int>(fwrite(d, 1, n, files[slot]));
  }

  int16_t close(int slot) override {
    const bool ok = fclose(files[slot]) == 0;
    files[slot] = nullptr;
    return ok ? kErrOk : kErrIo;
  }

  int16_t dirOpen(const char* path) override {
    dirPath = path;
    dir = opendir(real(path).c_str());
    return dir ? kErrOk : (errno == ENOENT ? kErrNotFound : kErrIo);
  }

  bool dirNext(FsEntry& e) override {
    while (dirent* d = readdir(dir)) {
      if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) continue;
      strncpy(e.name, d->d_name, sizeof e.name - 1);
      e.name[sizeof e.name - 1] = 0;
      std::string p = dirPath == "/" ? "/" + std::string(d->d_name) : dirPath + "/" + d->d_name;
      stat(p.c_str(), e.isDir, e.size);
      return true;
    }
    return false;
  }

  void dirClose() override {
    if (dir) closedir(dir);
    dir = nullptr;
  }

  int16_t remove(const char* path) override {
    if (unlink(real(path).c_str()) == 0) return kErrOk;
    return errno == ENOENT ? kErrNotFound : kErrIo;
  }

  int16_t rename(const char* from, const char* to) override {
    bool isDir;
    uint32_t size;
    if (stat(to, isDir, size) == kErrOk) return kErrExists;
    if (::rename(real(from).c_str(), real(to).c_str()) == 0) return kErrOk;
    return errno == ENOENT ? kErrNotFound : kErrIo;
  }

  int16_t mkdir(const char* path) override {
    if (::mkdir(real(path).c_str(), 0755) == 0) return kErrOk;
    return errno == EEXIST ? kErrExists : kErrIo;
  }

  int16_t rmdir(const char* path) override {
    if (::rmdir(real(path).c_str()) == 0) return kErrOk;
    return errno == ENOTEMPTY || errno == EEXIST ? kErrNotEmpty : kErrIo;
  }

  int16_t space(uint32_t& totalMb, uint32_t& freeMb) override {
    totalMb = 30436;
    freeMb = 1234;
    return kErrOk;
  }

 private:
  FILE* files[kFsMaxOpen] = {};
  DIR* dir = nullptr;
  std::string dirPath;
};

PosixBackend backend;
FsServerCore server(backend);

// The wire: both directions through the frame codec. dupRequests: each request reaches the server
// twice (its first reply lost, the ESP retried with the same seq).
struct Wire {
  uint8_t seq = 0;
  bool dupRequests = false;
  int frames = 0;
} wire;

bool decodeAll(const uint8_t* enc, int n, Decoder& d) {
  bool got = false;
  for (int i = 0; i < n; ++i) got = d.feed(enc[i]) || got;
  return got;
}

bool xfer(void*, Msg t, const uint8_t* p, int n, uint8_t* reply, int& replyLen) {
  static uint8_t enc[kMaxEncoded], out[kMaxPayload];
  static Decoder toServer, toClient;
  const uint8_t seq = wire.seq++;
  const int k = encode(static_cast<uint8_t>(t), seq, p, n, enc);
  if (k == 0 || !decodeAll(enc, k, toServer)) return false;
  int len = 0;
  for (int i = 0; i < (wire.dupRequests ? 2 : 1); ++i) {
    len = server.handle(static_cast<Msg>(toServer.type()), toServer.seq(), toServer.payload(), toServer.size(), out);
    if (len < 0) return false;
  }
  const int r = encode(static_cast<uint8_t>(t), toServer.seq(), out, len, enc);
  if (r == 0 || !decodeAll(enc, r, toClient)) return false;
  TEST_ASSERT_EQUAL_UINT8(seq, toClient.seq());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(t), toClient.type());
  memcpy(reply, toClient.payload(), toClient.size());
  replyLen = toClient.size();
  wire.frames++;
  return true;
}

FsClientCore client(xfer, nullptr);

void rmTree(const std::string& p) {
  if (DIR* d = opendir(p.c_str())) {
    while (dirent* e = readdir(d)) {
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
      rmTree(p + "/" + e->d_name);
    }
    closedir(d);
    ::rmdir(p.c_str());
  } else {
    unlink(p.c_str());
  }
}

void writeFile(const char* path, const std::vector<uint8_t>& data) {
  uint8_t h;
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open(path, FsMode::Write, h, isDir, size));
  TEST_ASSERT_EQUAL_UINT32(0, size);
  for (size_t at = 0; at < data.size(); at += kFsChunk) {
    const int n = static_cast<int>(std::min<size_t>(kFsChunk, data.size() - at));
    TEST_ASSERT_EQUAL_INT(n, client.write(h, static_cast<uint32_t>(at), data.data() + at, n));
  }
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(h));
}

std::vector<uint8_t> readFile(const char* path) {
  uint8_t h;
  bool isDir;
  uint32_t size;
  std::vector<uint8_t> out;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open(path, FsMode::Read, h, isDir, size));
  TEST_ASSERT_FALSE(isDir);
  uint8_t buf[kFsChunk];
  for (;;) {
    const int k = client.read(h, static_cast<uint32_t>(out.size()), buf, sizeof buf);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, k);
    if (k == 0) break;
    out.insert(out.end(), buf, buf + k);
  }
  TEST_ASSERT_EQUAL_UINT32(size, out.size());
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(h));
  return out;
}

std::vector<std::string> listAll(const char* dir, int* pages = nullptr) {
  std::vector<std::string> names;
  FsListPage page;
  FsEntry e;
  int n = 0;
  for (;;) {
    TEST_ASSERT_EQUAL_INT16(kErrOk, client.list(dir, static_cast<uint16_t>(names.size()), 32, page));
    ++n;
    TEST_ASSERT_LESS_OR_EQUAL_INT(32, page.count());
    while (page.next(e)) names.push_back(std::string(e.isDir ? "D:" : "") + e.name);
    if (!page.more()) break;
    TEST_ASSERT_LESS_THAN_INT(100, n);
  }
  if (pages) *pages = n;
  std::sort(names.begin(), names.end());
  return names;
}

bool exists(const char* path) {
  bool isDir;
  uint32_t size;
  return client.stat(path, isDir, size) == kErrOk;
}

}  // namespace

void setUp() {
  char tmpl[] = "/tmp/linkfsXXXXXX";
  TEST_ASSERT_NOT_NULL(mkdtemp(tmpl));
  backend.root = tmpl;
  backend.cardIn = true;
  wire = Wire();
  server.reset();
}

void tearDown() {
  server.reset();
  rmTree(backend.root);
}

void test_write_read_100k() {
  std::vector<uint8_t> data(100 * 1024);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 131 + (i >> 9));
  writeFile("/big.bin", data);
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.stat("/big.bin", isDir, size));
  TEST_ASSERT_FALSE(isDir);
  TEST_ASSERT_EQUAL_UINT32(data.size(), size);
  TEST_ASSERT_TRUE(readFile("/big.bin") == data);
}

void test_read_at_position_and_past_end() {
  std::vector<uint8_t> data(1000);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
  writeFile("/a.bin", data);
  uint8_t h;
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open("/a.bin", FsMode::Read, h, isDir, size));
  uint8_t buf[kFsChunk];
  TEST_ASSERT_EQUAL_INT(10, client.read(h, 990, buf, 100));
  TEST_ASSERT_EQUAL_UINT8(990 & 0xFF, buf[0]);
  TEST_ASSERT_EQUAL_INT(0, client.read(h, 1000, buf, 100));
  TEST_ASSERT_EQUAL_INT(kFsChunk, client.read(h, 0, buf, 4000));  // cut to one frame
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(h));
}

void test_append() {
  writeFile("/log.txt", {'a', 'b'});
  uint8_t h;
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open("/log.txt", FsMode::Append, h, isDir, size));
  TEST_ASSERT_EQUAL_UINT32(2, size);
  TEST_ASSERT_EQUAL_INT(1, client.write(h, size, "c", 1));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(h));
  TEST_ASSERT_TRUE(readFile("/log.txt") == std::vector<uint8_t>({'a', 'b', 'c'}));
  // Append creates a missing file.
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open("/new.txt", FsMode::Append, h, isDir, size));
  TEST_ASSERT_EQUAL_UINT32(0, size);
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(h));
  TEST_ASSERT_TRUE(exists("/new.txt"));
}

void test_list_70_entries_in_pages() {
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/many"));
  std::vector<std::string> want;
  for (int i = 0; i < 69; ++i) {
    char name[64];
    snprintf(name, sizeof name, "sample_with_a_longish_name_%02d.wav", i);
    char path[96];
    snprintf(path, sizeof path, "/many/%s", name);
    writeFile(path, {static_cast<uint8_t>(i)});
    want.push_back(name);
  }
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/many/sub"));
  want.push_back("D:sub");
  std::sort(want.begin(), want.end());
  int pages = 0;
  const std::vector<std::string> got = listAll("/many", &pages);
  TEST_ASSERT_EQUAL_INT(70, got.size());
  TEST_ASSERT_TRUE(got == want);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(3, pages);  // ~40-byte entries: about 12 per frame
}

void test_list_restarts_and_sizes() {
  writeFile("/x.bin", std::vector<uint8_t>(1234));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/d"));
  FsListPage page;
  FsEntry e;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.list("/", 0, 1, page));
  TEST_ASSERT_EQUAL_INT(1, page.count());
  TEST_ASSERT_TRUE(page.more());
  TEST_ASSERT_TRUE(exists("/x.bin"));  // another request in between keeps the listing
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.list("/", 1, 8, page));
  TEST_ASSERT_EQUAL_INT(1, page.count());
  TEST_ASSERT_FALSE(page.more());
  // From the start again, all of it.
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.list("/", 0, 8, page));
  TEST_ASSERT_EQUAL_INT(2, page.count());
  int files = 0, dirs = 0;
  while (page.next(e)) {
    if (e.isDir) {
      ++dirs;
      TEST_ASSERT_EQUAL_STRING("d", e.name);
    } else {
      ++files;
      TEST_ASSERT_EQUAL_UINT32(1234, e.size);
    }
  }
  TEST_ASSERT_EQUAL_INT(1, files);
  TEST_ASSERT_EQUAL_INT(1, dirs);
  // Past the end: an empty page.
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.list("/", 5, 8, page));
  TEST_ASSERT_EQUAL_INT(0, page.count());
  TEST_ASSERT_FALSE(page.more());
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.list("/nope", 0, 8, page));
}

void test_rename_remove() {
  writeFile("/one.mtp", {1, 2, 3});
  writeFile("/two.mtp", {4});
  TEST_ASSERT_EQUAL_INT16(kErrExists, client.rename("/one.mtp", "/two.mtp"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.remove("/two.mtp"));
  TEST_ASSERT_FALSE(exists("/two.mtp"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.rename("/one.mtp", "/two.mtp"));
  TEST_ASSERT_FALSE(exists("/one.mtp"));
  TEST_ASSERT_TRUE(readFile("/two.mtp") == std::vector<uint8_t>({1, 2, 3}));
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.remove("/one.mtp"));
}

void test_rmdir_recursive() {
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/projects"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/projects/SONG"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/projects/SONG/wt"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/projects/SONG/wt/deep"));
  writeFile("/projects/SONG/kick.wav", {1, 2});
  writeFile("/projects/SONG/wt/a.wav", {3});
  writeFile("/projects/SONG/wt/deep/b.wav", {4});
  writeFile("/projects/SONG.mtp", {5});
  TEST_ASSERT_EQUAL_INT16(kErrExists, client.mkdir("/projects/SONG"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.rmdir("/projects/SONG"));
  TEST_ASSERT_FALSE(exists("/projects/SONG"));
  TEST_ASSERT_TRUE(exists("/projects/SONG.mtp"));
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.rmdir("/projects/SONG"));
  TEST_ASSERT_EQUAL_INT16(kErrBadArg, client.rmdir("/"));
}

void test_card_space() {
  uint32_t total = 0, free = 0;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.space(total, free));
  TEST_ASSERT_EQUAL_UINT32(30436, total);
  TEST_ASSERT_EQUAL_UINT32(1234, free);
  // A plain FsStat (no flags byte) still answers without the space.
  bool isDir = false;
  uint32_t size = 1;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.stat("/", isDir, size));
  TEST_ASSERT_TRUE(isDir);
}

void test_errors() {
  uint8_t h;
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.open("/missing.txt", FsMode::Read, h, isDir, size));
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.lastError());
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.stat("/missing.txt", isDir, size));
  TEST_ASSERT_EQUAL_INT16(kErrNotFound, client.open("/no/dir/f.txt", FsMode::Write, h, isDir, size));
  uint8_t buf[8];
  TEST_ASSERT_EQUAL_INT(kErrBadHandle, client.read(2, 0, buf, 8));
  TEST_ASSERT_EQUAL_INT(kErrBadHandle, client.read(200, 0, buf, 8));
  TEST_ASSERT_EQUAL_INT16(kErrBadArg, client.stat("relative.txt", isDir, size));
  // A folder opens without a handle, and not for writing.
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.mkdir("/f"));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open("/f", FsMode::Read, h, isDir, size));
  TEST_ASSERT_TRUE(isDir);
  TEST_ASSERT_EQUAL_INT16(kErrIsDir, client.open("/f", FsMode::Write, h, isDir, size));
  // Handles run out.
  uint8_t hs[kFsMaxOpen];
  for (int i = 0; i < kFsMaxOpen; ++i) {
    char p[16];
    snprintf(p, sizeof p, "/h%d", i);
    TEST_ASSERT_EQUAL_INT16(kErrOk, client.open(p, FsMode::Write, hs[i], isDir, size));
  }
  TEST_ASSERT_EQUAL_INT16(kErrTooMany, client.open("/h9", FsMode::Write, h, isDir, size));
  for (uint8_t x : hs) TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(x));
  TEST_ASSERT_EQUAL_INT16(kErrBadHandle, client.close(hs[0]));
}

void test_retried_requests_done_once() {
  wire.dupRequests = true;
  uint8_t hs[kFsMaxOpen];
  bool isDir;
  uint32_t size;
  // Done twice, each open would take two handles: the fourth would fail.
  for (int i = 0; i < kFsMaxOpen; ++i) {
    char p[16];
    snprintf(p, sizeof p, "/r%d", i);
    TEST_ASSERT_EQUAL_INT16(kErrOk, client.open(p, FsMode::Write, hs[i], isDir, size));
    TEST_ASSERT_EQUAL_INT(1, client.write(hs[i], 0, "x", 1));
  }
  for (uint8_t x : hs) TEST_ASSERT_EQUAL_INT16(kErrOk, client.close(x));
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.remove("/r0"));  // the repeat would answer NotFound
  wire.dupRequests = false;
  TEST_ASSERT_TRUE(readFile("/r1") == std::vector<uint8_t>({'x'}));
}

void test_card_pulled() {
  uint8_t h;
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrOk, client.open("/c.bin", FsMode::Write, h, isDir, size));
  backend.cardIn = false;
  TEST_ASSERT_EQUAL_INT(kErrNoCard, client.write(h, 0, "z", 1));
  TEST_ASSERT_EQUAL_INT16(kErrNoCard, client.stat("/c.bin", isDir, size));
  backend.cardIn = true;
  TEST_ASSERT_EQUAL_INT(kErrBadHandle, client.write(h, 0, "z", 1));  // its files went with the card
  TEST_ASSERT_TRUE(exists("/c.bin"));
}

void test_not_fs_and_link_down() {
  uint8_t out[kMaxPayload];
  TEST_ASSERT_EQUAL_INT(-1, server.handle(Msg::Status, 0, nullptr, 0, out));
  FsClientCore dead([](void*, Msg, const uint8_t*, int, uint8_t*, int&) { return false; }, nullptr);
  bool isDir;
  uint32_t size;
  TEST_ASSERT_EQUAL_INT16(kErrLink, dead.stat("/x", isDir, size));
  TEST_ASSERT_EQUAL_INT16(kErrLink, dead.lastError());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_100k);
  RUN_TEST(test_read_at_position_and_past_end);
  RUN_TEST(test_append);
  RUN_TEST(test_list_70_entries_in_pages);
  RUN_TEST(test_list_restarts_and_sizes);
  RUN_TEST(test_rename_remove);
  RUN_TEST(test_rmdir_recursive);
  RUN_TEST(test_errors);
  RUN_TEST(test_retried_requests_done_once);
  RUN_TEST(test_card_pulled);
  RUN_TEST(test_card_space);
  RUN_TEST(test_not_fs_and_link_down);
  return UNITY_END();
}
