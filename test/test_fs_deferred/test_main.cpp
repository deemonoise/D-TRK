#include <unity.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>
#include "fs_deferred.h"
#include "stream_sched.h"

using namespace mt::link;

namespace {
struct MemoryFs final : FsBackend {
  bool present = true;
  bool failWrite = false;
  int readyCalls = 0, writes = 0, reads = 0, closes = 0, polls = 0;
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> dirs = {"/"};
  std::string handles[kFsMaxOpen];
  std::vector<FsEntry> entries;
  size_t dirPos = 0;
  int dirOpens = 0, dirReads = 0, dirFailAt = -1;
  int16_t dirError() const override { return dirFailAt >= 0 && dirReads >= dirFailAt ? kErrIo : kErrOk; }
  int16_t ready() override { ++readyCalls; return present ? kErrOk : kErrNoCard; }
  int16_t stat(const char* path, bool& dir, uint32_t& size) override {
    dir = dirs.count(path) != 0;
    if (dir) { size = 0; return kErrOk; }
    const auto f = files.find(path);
    if (f == files.end()) return kErrNotFound;
    size = static_cast<uint32_t>(f->second.size());
    return kErrOk;
  }
  int16_t open(int slot, const char* path, FsMode mode, uint32_t& size) override {
    if (!files.count(path) && mode == FsMode::Read) return kErrNotFound;
    auto& data = files[path];
    if (mode == FsMode::Write) data.clear();
    handles[slot] = path;
    size = static_cast<uint32_t>(data.size());
    return kErrOk;
  }
  int read(int slot, uint32_t pos, uint8_t* out, int n) override {
    ++reads;
    const auto& data = files[handles[slot]];
    const int take = pos < data.size() ? std::min<int>(n, data.size() - pos) : 0;
    if (take) memcpy(out, data.data() + pos, take);
    return take;
  }
  int write(int slot, uint32_t pos, const uint8_t* in, int n) override {
    ++writes;
    if (failWrite) return kErrIo;
    auto& data = files[handles[slot]];
    if (data.size() < pos + n) data.resize(pos + n);
    memcpy(data.data() + pos, in, n);
    return n;
  }
  int16_t close(int slot) override { ++closes; handles[slot].clear(); return kErrOk; }
  int16_t dirOpen(const char*) override { ++dirOpens; dirPos = 0; return kErrOk; }
  bool dirNext(FsEntry& e) override {
    ++dirReads;
    if (dirError() != kErrOk) return false;
    if (dirPos == entries.size()) return false;
    e = entries[dirPos++]; return true;
  }
  void dirClose() override {}
  int16_t remove(const char* path) override { return files.erase(path) ? kErrOk : kErrNotFound; }
  int16_t rename(const char* from, const char* to) override {
    if (files.count(to)) return kErrExists;
    if (!files.count(from)) return kErrNotFound;
    files[to] = files[from]; files.erase(from); return kErrOk;
  }
  int16_t mkdir(const char* path) override { return dirs.insert(path).second ? kErrOk : kErrExists; }
  int16_t rmdir(const char* path) override { return dirs.erase(path) ? kErrOk : kErrNotFound; }
  void poll() override { ++polls; }
  int16_t space(uint32_t& total, uint32_t& free) override { total = 64000; free = 12000; return kErrOk; }
};
struct Answer { Msg type; uint8_t seq; std::vector<uint8_t> data; };
struct Replies {
  std::vector<Answer> answers;
  static void send(void* ctx, Msg type, uint8_t seq, const uint8_t* data, int n) {
    static_cast<Replies*>(ctx)->answers.push_back({type, seq, {data, data + n}});
  }
};
MemoryFs* disk;
FsServerCore* server;
FsDeferred* pending;
Replies* replies;
uint8_t req[kMaxPayload], out[kMaxPayload];

int path(const char* p, int flags = -1) {
  Writer w(req, sizeof req); w.str(p); if (flags >= 0) w.u8(flags); return w.size();
}
int error(const uint8_t* data) { Reader r(data, 2); return static_cast<int16_t>(r.u16()); }
int answerError() { return error(replies->answers.back().data.data()); }
void queuePath(Msg type, uint8_t seq, const char* p, uint32_t now = 0) {
  const int n = path(p);
  TEST_ASSERT_EQUAL(-1, pending->submit(type, seq, req, n, now, out));
}
void openData(uint8_t seq = 1) {
  Writer w(req, sizeof req); w.u8(static_cast<uint8_t>(FsMode::Write)); w.str("/data");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsOpen, seq, req, w.size(), 0, out));
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
}
}  // namespace

void setUp() {
  disk = new MemoryFs; server = new FsServerCore(*disk); replies = new Replies;
  pending = new FsDeferred(*server, Replies::send, replies);
}
void tearDown() { delete pending; delete server; delete replies; delete disk; }

void test_receiving_only_copies_request_and_background_executes_once() {
  queuePath(Msg::FsMkdir, 7, "/first");
  path("/other");
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
  TEST_ASSERT_TRUE(replies->answers.empty());
  TEST_ASSERT_TRUE(pending->waiting());
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_TRUE(disk->dirs.count("/first"));
  TEST_ASSERT_FALSE(disk->dirs.count("/other"));
  TEST_ASSERT_EQUAL(1, disk->readyCalls);
  TEST_ASSERT_EQUAL(1, replies->answers.size());
  TEST_ASSERT_EQUAL_UINT8(7, replies->answers[0].seq);
  TEST_ASSERT_EQUAL(Msg::FsMkdir, replies->answers[0].type);
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_FALSE(pending->waiting());
  TEST_ASSERT_FALSE(pending->step(1, true));
}

void test_construction_over_noload_memory_initializes_every_guard() {
  alignas(FsDeferred) uint8_t dirty[sizeof(FsDeferred)];
  memset(dirty, 0xA5, sizeof dirty);
  auto* queue = new (dirty) FsDeferred(*server, Replies::send, replies);
  TEST_ASSERT_FALSE(queue->waiting());
  TEST_ASSERT_FALSE(queue->expire(UINT32_MAX));
  TEST_ASSERT_FALSE(queue->step(UINT32_MAX, true));
  const int n = path("/fresh");
  TEST_ASSERT_EQUAL(-1, queue->submit(Msg::FsMkdir, 3, req, n, 0, out));
  TEST_ASSERT_TRUE(queue->step(0, true));
  TEST_ASSERT_TRUE(disk->dirs.count("/fresh"));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  queue->~FsDeferred();
}

void test_pending_retry_does_not_execute_twice_or_refresh_deadline() {
  queuePath(Msg::FsMkdir, 9, "/once", 100);
  queuePath(Msg::FsMkdir, 9, "/once", 900);
  TEST_ASSERT_FALSE(pending->expire(1099));
  TEST_ASSERT_TRUE(pending->expire(1100));
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
  TEST_ASSERT_EQUAL(kErrIo, answerError());
  TEST_ASSERT_FALSE(pending->step(1101, true));
  TEST_ASSERT_FALSE(disk->dirs.count("/once"));
}

void test_completed_reply_and_previous_retry_bypass_sd_while_next_waits() {
  queuePath(Msg::FsMkdir, 1, "/a"); pending->step(0, true);
  queuePath(Msg::FsMkdir, 2, "/b");
  const int n = path("/a");
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 1, req, n, 1, out));
  TEST_ASSERT_EQUAL(kErrOk, error(out));
  TEST_ASSERT_EQUAL(1, disk->readyCalls);
  TEST_ASSERT_TRUE(pending->waiting());
  TEST_ASSERT_TRUE(pending->step(1, true));
  TEST_ASSERT_TRUE(disk->dirs.count("/b"));
}

void test_busy_request_is_rejected_without_replacing_queued_request() {
  queuePath(Msg::FsMkdir, 1, "/a");
  const int n = path("/b");
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 2, req, n, 0, out));
  TEST_ASSERT_EQUAL(kErrIo, error(out));
  pending->step(0, true);
  TEST_ASSERT_TRUE(disk->dirs.count("/a")); TEST_ASSERT_FALSE(disk->dirs.count("/b"));
}

void test_expired_reply_is_cached_and_does_not_execute_on_late_retry() {
  queuePath(Msg::FsMkdir, 7, "/late");
  TEST_ASSERT_TRUE(pending->expire(FsDeferred::kWaitMs));
  const int n = path("/late");
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 7, req, n, 2000, out));
  TEST_ASSERT_EQUAL(kErrIo, error(out));
  TEST_ASSERT_FALSE(pending->step(2000, true));
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
}

void test_deadline_survives_millisecond_wrap_and_step_checks_expiry() {
  queuePath(Msg::FsMkdir, 1, "/wrap", UINT32_MAX - 499);
  TEST_ASSERT_FALSE(pending->expire(499));
  TEST_ASSERT_TRUE(pending->step(500, true));
  TEST_ASSERT_EQUAL(kErrIo, answerError());
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
}

void test_reset_cancels_pending_clears_cache_and_closes_open_files() {
  openData();
  queuePath(Msg::FsMkdir, 2, "/cancel");
  pending->reset();
  TEST_ASSERT_TRUE(pending->waiting());
  TEST_ASSERT_EQUAL(0, disk->closes);
  TEST_ASSERT_FALSE(pending->step(1, false));
  TEST_ASSERT_EQUAL(1, replies->answers.size());
  TEST_ASSERT_TRUE(pending->step(2, true));
  TEST_ASSERT_EQUAL(1, disk->closes);
  TEST_ASSERT_FALSE(pending->waiting());
  TEST_ASSERT_FALSE(disk->dirs.count("/cancel"));
  queuePath(Msg::FsMkdir, 2, "/after");
  pending->step(3, true);
  TEST_ASSERT_TRUE(disk->dirs.count("/after"));
}

void test_reset_does_not_touch_card_and_cleanup_precedes_a_new_request() {
  openData();
  pending->reset();
  TEST_ASSERT_EQUAL(0, disk->closes);
  TEST_ASSERT_TRUE(pending->waiting());
  queuePath(Msg::FsMkdir, 2, "/after-reset");
  TEST_ASSERT_FALSE(pending->step(100, false));
  TEST_ASSERT_EQUAL(0, disk->closes);
  TEST_ASSERT_FALSE(disk->dirs.count("/after-reset"));
  TEST_ASSERT_TRUE(pending->step(101, true));  // admitted cleanup, its own portion
  TEST_ASSERT_EQUAL(1, disk->closes);
  TEST_ASSERT_FALSE(disk->dirs.count("/after-reset"));
  TEST_ASSERT_TRUE(pending->waiting());
  TEST_ASSERT_TRUE(pending->step(102, true));  // only now can the new command execute
  TEST_ASSERT_TRUE(disk->dirs.count("/after-reset"));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
}

void test_new_request_expires_during_cleanup_starvation_without_closing_files() {
  openData();
  pending->reset();
  queuePath(Msg::FsMkdir, 2, "/starved", 100);
  TEST_ASSERT_TRUE(pending->expire(1100));
  TEST_ASSERT_EQUAL(kErrIo, answerError());
  TEST_ASSERT_EQUAL(0, disk->closes);
  TEST_ASSERT_TRUE(pending->waiting());  // cleanup still pending, expired request is not
  TEST_ASSERT_FALSE(pending->step(1100, false));
  const int n = path("/starved");
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 2, req, n, 1101, out));
  TEST_ASSERT_EQUAL(kErrIo, error(out));
  TEST_ASSERT_TRUE(pending->step(1102, true));
  TEST_ASSERT_EQUAL(1, disk->closes);
  TEST_ASSERT_FALSE(pending->waiting());
  TEST_ASSERT_FALSE(disk->dirs.count("/starved"));
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 2, req, n, 1103, out));
  TEST_ASSERT_EQUAL(kErrIo, error(out));
}

void test_repeated_reset_coalesces_cleanup_and_invalidates_the_old_open_reply() {
  openData(7);
  disk->files["/data"] = {1,2,3};
  pending->reset(); pending->reset();
  Writer w(req, sizeof req); w.u8(static_cast<uint8_t>(FsMode::Write)); w.str("/data");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsOpen, 7, req, w.size(), 0, out));
  TEST_ASSERT_TRUE(pending->step(1, true));
  TEST_ASSERT_EQUAL(1, disk->closes);
  TEST_ASSERT_EQUAL(3, disk->files["/data"].size());
  TEST_ASSERT_EQUAL(1, disk->readyCalls);
  TEST_ASSERT_TRUE(pending->step(2, true));
  TEST_ASSERT_TRUE(disk->files["/data"].empty());
  TEST_ASSERT_EQUAL(2, disk->readyCalls);
  TEST_ASSERT_FALSE(pending->waiting());
  TEST_ASSERT_FALSE(pending->step(3, true));
  TEST_ASSERT_EQUAL(1, disk->closes);
}

void test_copy_waits_until_streaming_is_idle_and_preserves_copy_semantics() {
  disk->files["/source"] = std::vector<uint8_t>(6000, 37);
  Writer w(req, sizeof req); w.str("/source"); w.str("/target");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsCopy, 3, req, w.size(), 0, out));
  TEST_ASSERT_FALSE(pending->step(1, false));
  TEST_ASSERT_FALSE(disk->files.count("/target"));
  TEST_ASSERT_EQUAL(0, disk->reads);
  TEST_ASSERT_TRUE(pending->step(2, true));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_TRUE(disk->files["/source"] == disk->files["/target"]);
  TEST_ASSERT_FALSE(disk->files.count("/target.tmp"));
  TEST_ASSERT_TRUE(disk->polls > 0);
}

void test_space_scan_waits_but_plain_stat_can_run_with_streams() {
  int n = path("/", kFsStatSpace);
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsStat, 1, req, n, 0, out));
  TEST_ASSERT_FALSE(pending->step(0, false));
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_EQUAL(15, replies->answers.back().data.size());
  n = path("/");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsStat, 2, req, n, 0, out));
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
}

void test_timeout_sequence_wrap_cannot_replay_an_ancient_success() {
  queuePath(Msg::FsMkdir, 4, "/old"); pending->step(0, true);
  for (int i = 1; i <= 255; ++i) {
    queuePath(Msg::FsMkdir, static_cast<uint8_t>(4+i), "/never");
    TEST_ASSERT_TRUE(pending->expire(FsDeferred::kWaitMs));
  }
  queuePath(Msg::FsMkdir, 4, "/new"); pending->step(0, true);
  TEST_ASSERT_TRUE(disk->dirs.count("/new"));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
}

void test_write_result_is_identical_on_retry_and_card_failure_is_reported() {
  openData();
  Writer w(req, sizeof req); w.u8(0); w.u32(0);
  for (int i = 0; i < kFsChunk; ++i) w.u8(static_cast<uint8_t>(i));
  const int n = w.size();
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsWrite, 2, req, n, 0, out));
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(1, disk->writes);
  TEST_ASSERT_EQUAL(4, pending->submit(Msg::FsWrite, 2, req, n, 0, out));
  TEST_ASSERT_EQUAL(1, disk->writes);
  TEST_ASSERT_EQUAL_MEMORY(replies->answers.back().data.data(), out, 4);
  TEST_ASSERT_EQUAL(kFsChunk, disk->files["/data"].size());
  disk->present = false;
  queuePath(Msg::FsStat, 3, "/"); pending->step(0, false);
  TEST_ASSERT_EQUAL(kErrNoCard, answerError());
  TEST_ASSERT_EQUAL(1, disk->closes);
}

void test_non_fs_and_invalid_payloads_never_touch_card_or_queue() {
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::Time, 1, nullptr, 0, 0, out));
  TEST_ASSERT_FALSE(pending->waiting());
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsRead, 1, nullptr, 1, 0, out));
  TEST_ASSERT_EQUAL(kErrBadArg, error(out));
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsRead, 2, req, kMaxPayload+1, 0, out));
  TEST_ASSERT_EQUAL(kErrBadArg, error(out));
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsRead, 3, req, -1, 0, out));
  TEST_ASSERT_EQUAL(kErrBadArg, error(out));
  TEST_ASSERT_FALSE(pending->waiting()); TEST_ASSERT_EQUAL(0, disk->readyCalls);
}

void test_scheduler_holds_file_request_until_audio_reserve_is_sufficient() {
  struct Audio final : mt::StreamConsumer {
    uint32_t reserve = 10000;
    bool needs(uint32_t& out) override { out = reserve; return false; }
    void load() override {}
  } audio;
  struct FileWork final : mt::BgWork {
    bool step() override { return pending->step(0, false); }
  } work;
  // The scheduler, not receipt of the frame, admits FsOpen. Card latency remains unbounded.
  Writer w(req, sizeof req); w.u8(static_cast<uint8_t>(FsMode::Write)); w.str("/data");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsOpen, 1, req, w.size(), 0, out));
  mt::StreamConsumer* consumers[] = {&audio}; mt::BgWork* bg[] = {&work}; int next = 0;
  mt::schedulePass(consumers, 1, bg, 1, next);
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
  TEST_ASSERT_TRUE(pending->waiting());
  audio.reserve = 60000;
  mt::schedulePass(consumers, 1, bg, 1, next);
  TEST_ASSERT_EQUAL(1, disk->readyCalls);
  TEST_ASSERT_FALSE(pending->waiting());
}

void test_sequence_wrap_after_non_fs_frames_does_not_replay_different_payload() {
  queuePath(Msg::FsMkdir, 4, "/old");
  TEST_ASSERT_TRUE(pending->step(0, true));
  for (int i = 1; i <= 256; ++i)
    TEST_ASSERT_EQUAL(-1, pending->submit(Msg::Time, static_cast<uint8_t>(4+i), nullptr, 0, i, out));
  const int n = path("/new");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsMkdir, 4, req, n, 300, out));
  TEST_ASSERT_TRUE(pending->step(300, true));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_TRUE(disk->dirs.count("/new"));
}

void test_pending_same_seq_different_payload_is_rejected_as_overlap() {
  queuePath(Msg::FsMkdir, 4, "/old", 100);
  const int n = path("/new");
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 4, req, n, 101, out));
  TEST_ASSERT_EQUAL(kErrIo, error(out));
  TEST_ASSERT_TRUE(pending->step(102, true));
  TEST_ASSERT_TRUE(disk->dirs.count("/old"));
  TEST_ASSERT_FALSE(disk->dirs.count("/new"));
}

void test_invalid_payload_cannot_reach_cache_or_pending_comparison() {
  queuePath(Msg::FsMkdir, 4, "/old");
  const int n = path("/old");
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 4, nullptr, n, 0, out));
  TEST_ASSERT_EQUAL(kErrBadArg,error(out));
  TEST_ASSERT_TRUE(pending->step(0,true));
  TEST_ASSERT_EQUAL(2, pending->submit(Msg::FsMkdir, 4, nullptr, n, 0, out));
  TEST_ASSERT_EQUAL(kErrBadArg,error(out));
}

void queueList(uint8_t seq, const char* dir, uint16_t start, uint8_t max = 2) {
  Writer w(req, sizeof req); w.u16(start); w.u8(max); w.str(dir);
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsList, seq, req, w.size(), 0, out));
}

void seedList() {
  for (int i = 0; i < 7; ++i) {
    FsEntry e;
    e.name[0] = 'a' + i;
    disk->entries.push_back(e);
  }
}

void test_live_listing_admits_first_and_exact_next_page_without_io_in_inspector() {
  seedList();
  TEST_ASSERT_TRUE(server->listCanContinue("/", 0));
  TEST_ASSERT_FALSE(server->listCanContinue("/", 2));
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
  TEST_ASSERT_EQUAL(0, disk->dirOpens);
  queueList(1, "/", 0);
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_TRUE(server->listCanContinue("/", 2));
  TEST_ASSERT_FALSE(server->listCanContinue("/", 1));
  TEST_ASSERT_FALSE(server->listCanContinue("/other", 2));
  const int reads = disk->dirReads;
  TEST_ASSERT_TRUE(server->listCanContinue("/", 2));
  TEST_ASSERT_EQUAL(reads, disk->dirReads);
  TEST_ASSERT_EQUAL(1, disk->readyCalls);
  queueList(2, "/", 2);
  TEST_ASSERT_TRUE(pending->step(1, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_EQUAL(1, disk->dirOpens);
  TEST_ASSERT_TRUE(server->listCanContinue("/", 4));
  const auto& data = replies->answers.back().data;
  Reader page(data.data(), data.size()); page.u16(); page.u8();
  TEST_ASSERT_EQUAL_UINT8(2, page.u8());
  page.u8(); page.u32(); char name[kFsPathMax]; page.str(name, sizeof name);
  TEST_ASSERT_EQUAL_STRING("c", name);
}

void test_arbitrary_listing_skip_waits_for_idle_and_metadata_invalidates_cursor() {
  seedList(); queueList(1, "/", 0);
  TEST_ASSERT_TRUE(pending->step(0, true));
  queueList(2, "/", 4);
  const int ready = disk->readyCalls;
  TEST_ASSERT_FALSE(pending->step(1, false));
  TEST_ASSERT_EQUAL(ready, disk->readyCalls);
  TEST_ASSERT_TRUE(pending->step(2, true));
  TEST_ASSERT_EQUAL(2, disk->dirOpens);
  TEST_ASSERT_TRUE(server->listCanContinue("/", 6));
  queuePath(Msg::FsMkdir, 3, "/folder");
  TEST_ASSERT_TRUE(server->listCanContinue("/", 6));  // receipt does not invalidate it
  TEST_ASSERT_TRUE(pending->step(3, false));
  TEST_ASSERT_FALSE(server->listCanContinue("/", 6));
  queueList(4, "/", 6);
  TEST_ASSERT_FALSE(pending->step(4, false));
  TEST_ASSERT_TRUE(pending->step(5, true));
}

void test_malformed_listing_is_rejected_without_waiting_for_idle() {
  for (int n = 0; n < 5; ++n) {
    const uint8_t truncated[] = {1, 0, 2, 3, '/'};
    TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsList, n, truncated, n, 0, out));
    TEST_ASSERT_TRUE(pending->step(0, false));
    TEST_ASSERT_EQUAL(kErrBadArg, answerError());
  }
  queueList(10, "relative", 2);
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrBadArg, answerError());
  TEST_ASSERT_EQUAL(0, disk->dirOpens);
}

void test_save_metadata_runs_when_audio_reserve_admits_it_without_stopping_transport() {
  disk->files["/song.mtp"] = {1,2,3};
  disk->files["/song.tmp"] = {4,5,6};
  disk->files["/song.bak"] = {0};
  queuePath(Msg::FsRemove, 1, "/song.bak");
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  Writer first(req, sizeof req); first.str("/song.mtp"); first.str("/song.bak");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsRename, 2, req, first.size(), 0, out));
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  Writer second(req, sizeof req); second.str("/song.tmp"); second.str("/song.mtp");
  TEST_ASSERT_EQUAL(-1, pending->submit(Msg::FsRename, 3, req, second.size(), 0, out));
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_TRUE(disk->files["/song.mtp"] == std::vector<uint8_t>({4,5,6}));
  TEST_ASSERT_TRUE(disk->files["/song.bak"] == std::vector<uint8_t>({1,2,3}));
  queuePath(Msg::FsMkdir, 4, "/song");
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_TRUE(disk->dirs.count("/song"));
}

void test_recursive_removal_still_requires_audio_idle() {
  disk->dirs.insert("/new");
  queuePath(Msg::FsRmdir, 4, "/new");
  TEST_ASSERT_FALSE(pending->step(0, false));
  TEST_ASSERT_TRUE(disk->dirs.count("/new"));
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_FALSE(disk->dirs.count("/new"));
}

void test_directory_read_failure_is_not_an_empty_or_partial_success() {
  seedList();
  for (int failure : {1, 2, 3}) { // first entry, page body, read-ahead
    disk->dirReads = 0;
    disk->dirFailAt = failure;
    queueList(static_cast<uint8_t>(failure), "/", 0);
    TEST_ASSERT_TRUE(pending->step(0, true));
    TEST_ASSERT_EQUAL(kErrIo, answerError());
    TEST_ASSERT_EQUAL(2, replies->answers.back().data.size());
    TEST_ASSERT_FALSE(server->listCanContinue("/", 2));
  }
  disk->dirFailAt = -1;
  queueList(4, "/", 0);
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_EQUAL(kErrOk, answerError());
}

void test_directory_skip_and_recursive_delete_preserve_read_failure() {
  seedList(); disk->dirFailAt = 1;
  queueList(1, "/", 4);
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_EQUAL(kErrIo, answerError());
  disk->dirs.insert("/keep");
  queuePath(Msg::FsRmdir, 2, "/keep");
  TEST_ASSERT_TRUE(pending->step(0, true));
  TEST_ASSERT_EQUAL(kErrIo, answerError());
  TEST_ASSERT_TRUE(disk->dirs.count("/keep"));
}

void test_listing_reply_retry_keeps_cursor_and_reset_revokes_continuation() {
  seedList(); queueList(1, "/", 0);
  TEST_ASSERT_TRUE(pending->step(0, false));
  const int reads = disk->dirReads;
  Writer w(req, sizeof req); w.u16(0); w.u8(2); w.str("/");
  const auto first = replies->answers.back().data;
  TEST_ASSERT_EQUAL(first.size(), pending->submit(Msg::FsList, 1, req, w.size(), 1, out));
  TEST_ASSERT_EQUAL_MEMORY(first.data(), out, first.size());
  TEST_ASSERT_EQUAL(reads, disk->dirReads);
  TEST_ASSERT_TRUE(server->listCanContinue("/", 2));
  pending->reset();
  TEST_ASSERT_FALSE(pending->step(2, false));
  TEST_ASSERT_TRUE(pending->step(3, true));
  TEST_ASSERT_FALSE(server->listCanContinue("/", 2));
  queueList(2, "/", 2);
  TEST_ASSERT_FALSE(pending->step(4, false));
  TEST_ASSERT_TRUE(pending->step(5, true));
  TEST_ASSERT_EQUAL(2, disk->dirOpens);
}

void test_listing_cursor_tracks_sent_entries_when_names_fill_packet() {
  for (int i = 0; i < 3; ++i) {
    FsEntry entry;
    memset(entry.name, 'a' + i, 250);
    entry.name[250] = 0;
    disk->entries.push_back(entry);
  }
  queueList(1, "/", 0, 255);
  TEST_ASSERT_TRUE(pending->step(0, false));
  TEST_ASSERT_EQUAL_UINT8(1, replies->answers.back().data[3]);
  TEST_ASSERT_TRUE(server->listCanContinue("/", 1));
  TEST_ASSERT_FALSE(server->listCanContinue("/", 2));
  const int reads = disk->dirReads;
  queueList(2, "/", 1, 255);
  TEST_ASSERT_TRUE(pending->step(1, false));
  TEST_ASSERT_EQUAL(1, disk->dirOpens);
  TEST_ASSERT_EQUAL(reads + 1, disk->dirReads);  // held entry emitted, next entry held
  Reader r(replies->answers.back().data.data(), replies->answers.back().data.size());
  r.u16(); r.u8(); TEST_ASSERT_EQUAL_UINT8(1, r.u8()); r.u8(); r.u32();
  char name[kFsPathMax]; r.str(name, sizeof name);
  TEST_ASSERT_EQUAL_UINT8('b', name[0]);
  TEST_ASSERT_EQUAL(250, strlen(name));
}

void test_file_request_runs_with_achievable_reserve_of_six_block_audio_cache() {
  struct Audio : mt::StreamConsumer {
    // At 44.1 kHz, the guarded six-block cache can have only ~40 ms left even when full.
    bool needs(uint32_t& r) override { r = 40000; return false; }
    void load() override {}
  } audio;
  struct File : mt::BgWork {
    uint32_t minReserveUs() const override { return FsDeferred::kReserveUs; }
    bool step() override { return pending->step(0, false); }
  } file;
  queuePath(Msg::FsMkdir, 1, "/save");
  mt::StreamConsumer* consumers[] = {&audio}; mt::BgWork* work[] = {&file}; int next = 0;
  mt::schedulePass(consumers, 1, work, 1, next);
  TEST_ASSERT_EQUAL(1, replies->answers.size());
  TEST_ASSERT_EQUAL(kErrOk, answerError());
  TEST_ASSERT_TRUE(disk->dirs.count("/save"));
  TEST_ASSERT_FALSE(pending->waiting());
}

void test_file_reserve_starvation_expires_without_blocking_other_background_work() {
  struct Audio : mt::StreamConsumer {
    bool needs(uint32_t& r) override { r = 20000; return false; }
    void load() override {}
  } audio;
  struct File : mt::BgWork {
    uint32_t minReserveUs() const override { return FsDeferred::kReserveUs; }
    bool step() override { return pending->step(0, false); }
  } file;
  struct Other : mt::BgWork {
    uint32_t minReserveUs() const override { return 10000; }
    int calls = 0;
    bool step() override { ++calls; return true; }
  } other;
  queuePath(Msg::FsMkdir, 1, "/starved");
  mt::StreamConsumer* consumers[] = {&audio}; mt::BgWork* work[] = {&file, &other};
  int next = 0;
  for (int i = 0; i < 10; ++i) mt::schedulePass(consumers, 1, work, 2, next);
  TEST_ASSERT_EQUAL(10, other.calls);
  TEST_ASSERT_EQUAL(0, disk->readyCalls);
  TEST_ASSERT_TRUE(pending->expire(FsDeferred::kWaitMs));
  TEST_ASSERT_EQUAL(kErrIo, answerError());
  TEST_ASSERT_FALSE(pending->step(FsDeferred::kWaitMs + 1, true));
  TEST_ASSERT_FALSE(disk->dirs.count("/starved"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_live_listing_admits_first_and_exact_next_page_without_io_in_inspector);
  RUN_TEST(test_arbitrary_listing_skip_waits_for_idle_and_metadata_invalidates_cursor);
  RUN_TEST(test_malformed_listing_is_rejected_without_waiting_for_idle);
  RUN_TEST(test_save_metadata_runs_when_audio_reserve_admits_it_without_stopping_transport);
  RUN_TEST(test_recursive_removal_still_requires_audio_idle);
  RUN_TEST(test_directory_read_failure_is_not_an_empty_or_partial_success);
  RUN_TEST(test_directory_skip_and_recursive_delete_preserve_read_failure);
  RUN_TEST(test_file_request_runs_with_achievable_reserve_of_six_block_audio_cache);
  RUN_TEST(test_file_reserve_starvation_expires_without_blocking_other_background_work);
  RUN_TEST(test_listing_reply_retry_keeps_cursor_and_reset_revokes_continuation);
  RUN_TEST(test_listing_cursor_tracks_sent_entries_when_names_fill_packet);
  RUN_TEST(test_receiving_only_copies_request_and_background_executes_once);
  RUN_TEST(test_construction_over_noload_memory_initializes_every_guard);
  RUN_TEST(test_pending_retry_does_not_execute_twice_or_refresh_deadline);
  RUN_TEST(test_completed_reply_and_previous_retry_bypass_sd_while_next_waits);
  RUN_TEST(test_busy_request_is_rejected_without_replacing_queued_request);
  RUN_TEST(test_expired_reply_is_cached_and_does_not_execute_on_late_retry);
  RUN_TEST(test_deadline_survives_millisecond_wrap_and_step_checks_expiry);
  RUN_TEST(test_reset_cancels_pending_clears_cache_and_closes_open_files);
  RUN_TEST(test_reset_does_not_touch_card_and_cleanup_precedes_a_new_request);
  RUN_TEST(test_new_request_expires_during_cleanup_starvation_without_closing_files);
  RUN_TEST(test_repeated_reset_coalesces_cleanup_and_invalidates_the_old_open_reply);
  RUN_TEST(test_copy_waits_until_streaming_is_idle_and_preserves_copy_semantics);
  RUN_TEST(test_space_scan_waits_but_plain_stat_can_run_with_streams);
  RUN_TEST(test_timeout_sequence_wrap_cannot_replay_an_ancient_success);
  RUN_TEST(test_write_result_is_identical_on_retry_and_card_failure_is_reported);
  RUN_TEST(test_non_fs_and_invalid_payloads_never_touch_card_or_queue);
  RUN_TEST(test_scheduler_holds_file_request_until_audio_reserve_is_sufficient);
  RUN_TEST(test_sequence_wrap_after_non_fs_frames_does_not_replay_different_payload);
  RUN_TEST(test_pending_same_seq_different_payload_is_rejected_as_overlap);
  RUN_TEST(test_invalid_payload_cannot_reach_cache_or_pending_comparison);
  return UNITY_END();
}
