// Compile the actual ESP state machine with deterministic UART/RTOS shims.
#include "../../src/link/link.cpp"
#include "hw/sdcard.h"
#include "link_fs.h"
#include "storage/remote_fs.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
using namespace mt::link;
// Stop the firmware task at a scheduling boundary without changing production
// code.
struct StopRun {};
static int readsLeft = 0, hellos = 0;
static bool delayNextOperation = false, quiet = false, peerResponds = true;
static Msg slowType = Msg::FsWrite;
static std::deque<std::vector<uint8_t>> incoming;
struct Card : FsBackend {
  std::vector<uint8_t> file;
  bool present = false, opened = false;
  int16_t ready() override { return 0; }
  int16_t stat(const char *, bool &dir, uint32_t &size) override {
    dir = false;
    size = file.size();
    return present ? 0 : kErrNotFound;
  }
  int16_t open(int, const char *, FsMode mode, uint32_t &size) override {
    if (mode == FsMode::Read && !present)
      return kErrNotFound;
    if (mode == FsMode::Write)
      file.clear();
    present = opened = true;
    size = file.size();
    return 0;
  }
  int read(int, uint32_t at, uint8_t *out, int n) override {
    if (!opened)
      return kErrBadHandle;
    int take = at < file.size() ? std::min<size_t>(n, file.size() - at) : 0;
    if (take)
      memcpy(out, file.data() + at, take);
    return take;
  }
  int write(int, uint32_t at, const uint8_t *p, int n) override {
    if (!opened)
      return kErrBadHandle;
    if (file.size() < at + n)
      file.resize(at + n);
    memcpy(file.data() + at, p, n);
    return n;
  }
  int16_t close(int) override {
    opened = false;
    return 0;
  }
  int16_t dirOpen(const char *) override { return kErrNotFound; }
  bool dirNext(FsEntry &) override { return false; }
  void dirClose() override {}
  int16_t remove(const char *) override {
    present = false;
    file.clear();
    return 0;
  }
  int16_t rename(const char *, const char *) override { return 0; }
  int16_t mkdir(const char *) override { return 0; }
  int16_t rmdir(const char *) override { return 0; }
} card;
static FsServerCore server(card);
static Decoder transmit;
static void dispatchFrame(Msg t, uint8_t seq, const uint8_t *p, int n) {
  uint8_t bytes[kMaxEncoded];
  int count = encode(uint8_t(t), seq, p, n, bytes);
  Decoder d;
  for (int i = 0; i < count; ++i)
    if (d.feed(bytes[i]))
      slink::dispatch(d);
}
static void establish() {
  slink::begin();
  testNow = 100;
  Hello h;
  h.proto = kProtocol;
  h.modelSize = sizeof(mt::SynthModel);
  h.bootId = 42;
  uint8_t bytes[kMaxPayload];
  Writer w(bytes, sizeof bytes);
  encode(h, w);
  dispatchFrame(Msg::Hello, 0, bytes, w.size());
  assert(slink::synthUp());
}
namespace slink {
bool uartBegin(uint32_t) { return true; }
int uartRead(uint8_t *p, int n, TickType_t) {
  if (--readsLeft < 0)
    throw StopRun{};
  if (quiet) {
    quiet = false;
    return 0;
  }
  if (incoming.empty())
    return 0;
  auto b = incoming.front();
  incoming.pop_front();
  assert(int(b.size()) <= n);
  memcpy(p, b.data(), b.size());
  return b.size();
}
int uartWrite(const uint8_t *p, int n) {
  for (int i = 0; i < n; ++i)
    if (transmit.feed(p[i])) {
      auto type = Msg(transmit.type());
      if (type == Msg::Hello) {
        ++hellos;
        server.reset();
        continue;
      }
      if (!isFsRequest(type) || !peerResponds)
        continue;
      uint8_t answer[kMaxPayload], frame[kMaxEncoded];
      int count = server.handle(type, transmit.seq(), transmit.payload(),
                                transmit.size(), answer);
      if (type == slowType && delayNextOperation) {
        delayNextOperation = false;
        testNow += 1500;
        quiet = true;
      }
      int len = encode(uint8_t(type), transmit.seq(), answer, count, frame);
      incoming.emplace_back(frame, frame + len);
    }
  return n;
}
} // namespace slink
static void runTurns(int n) {
  readsLeft = n;
  try {
    slink::run(nullptr);
  } catch (StopRun &) {
  }
}
static void deliverReply(Msg type, uint8_t seq) {
  uint8_t p[4] = {0, 0, 1, 0};
  dispatchFrame(type, seq, p, 4);
}
static void arm(uint8_t *reply, Msg type = Msg::FsWrite) {
  slink::reqBuf = reply;
  slink::reqType = uint8_t(type);
  slink::reqSeq = 7;
  slink::reqActive = true;
}
static void replyContact() {
  establish();
  testNow = 1600;
  assert(!slink::synthUp());
  uint8_t out[kMaxPayload];
  arm(out);
  deliverReply(Msg::FsWrite, 7);
  assert(slink::synthUp());
  assert(slink::takeStatus().frames == 0);
  testNow += 1000;
  assert(!slink::synthUp());
}
static void progressContact() {
  establish();
  testNow = 1600;
  uint8_t out[kMaxPayload];
  arm(out, Msg::AssetsSave);
  Progress p;
  p.done = 1;
  p.total = 10;
  uint8_t data[kMaxPayload];
  Writer w(data, sizeof data);
  encode(p, w);
  dispatchFrame(Msg::Progress, 7, data, w.size());
  assert(slink::synthUp());
  assert(slink::reqActive);
  assert(slink::takeStatus().frames == 0);
}
static void ignoreNoise() {
  establish();
  testNow = 1600;
  uint8_t out[kMaxPayload];
  arm(out);
  deliverReply(Msg::FsWrite, 8);
  assert(!slink::synthUp());
  deliverReply(Msg::FsClose, 7);
  assert(!slink::synthUp());
  assert(slink::reqActive);
}
static void pendingAndTimeout() {
  establish();
  peerResponds = false;
  uint8_t p[1] = {0}, out[kMaxPayload];
  int len = 0;
  int waits = 0;
  testWait = [&](TickType_t n) {
    testNow += n;
    ++waits;
    assert(slink::synthUp());
    runTurns(1);
    assert(hellos == 0);
  };
  assert(!slink::request(Msg::FsClose, p, 1, Msg::FsClose, out, len, 2000));
  assert(waits == 3);
  assert(!slink::synthUp());
  runTurns(1);
  assert(hellos == 1);
}
static void slowFile() {
  establish();
  testWait = [](TickType_t) { runTurns(8); };
  FsClientCore client(
      +[](void *, Msg t, const uint8_t *p, int n, uint8_t *r, int &len) {
        if (!slink::synthUp())
          return false;
        return slink::request(t, p, n, t, r, len, 2000);
      },
      nullptr);
  uint8_t h;
  bool dir;
  uint32_t size;
  assert(client.open("/projects/copy.tmp", FsMode::Write, h, dir, size) == 0);
  std::vector<uint8_t> data(8192);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = (i * 37) % 251;
  delayNextOperation = true;
  size_t at = 0;
  while (at < data.size()) {
    int n = client.write(h, at, data.data() + at, data.size() - at);
    assert(n > 0);
    at += n;
  }
  assert(client.close(h) == 0);
  assert(card.file == data);
  assert(client.open("/projects/copy.tmp", FsMode::Read, h, dir, size) == 0);
  std::vector<uint8_t> readback(size);
  at = 0;
  while (at < size) {
    int n = client.read(h, at, readback.data() + at, size - at);
    assert(n > 0);
    at += n;
  }
  assert(readback == data);
  assert(client.close(h) == 0);
  assert(!card.opened);
  assert(hellos == 0);
}
static void longTimeout() {
  establish();
  peerResponds = false;
  uint8_t data[1] = {0}, out[kMaxPayload];
  int n = 0;
  testWait = [](TickType_t dt) {
    testNow += dt;
    assert(slink::synthUp());
    runTurns(1);
    assert(hellos == 0);
  };
  assert(!slink::requestLong(Msg::AssetsSave, data, 1, out, n, nullptr, nullptr,
                             500));
  assert(!slink::synthUp());
  runTurns(1);
  assert(hellos == 1);
}
static void longProgress() {
  establish();
  peerResponds = false;
  uint8_t data[1] = {0}, out[kMaxPayload];
  int n = 0, waits = 0, callbacks = 0;
  testWait = [&](TickType_t) {
    testNow += 1500;
    assert(slink::synthUp());
    runTurns(1);
    assert(hellos == 0);
    if (waits++ == 0) {
      Progress p;
      p.done = 1;
      p.total = 2;
      uint8_t b[kMaxPayload];
      Writer w(b, sizeof b);
      encode(p, w);
      dispatchFrame(Msg::Progress, slink::reqSeq, b, w.size());
    } else
      deliverReply(Msg::AssetsSave, slink::reqSeq);
  };
  assert(slink::requestLong(
      Msg::AssetsSave, data, 1, out, n,
      +[](const Progress &p, void *ctx) {
        assert(p.done == 1);
        ++*static_cast<int *>(ctx);
      },
      &callbacks, 2500));
  assert(callbacks == 1);
  assert(slink::synthUp());
  testNow += 1000;
  assert(!slink::synthUp());
}
static void wrapClock() {
  slink::begin();
  testNow = UINT32_MAX - 500;
  Hello h;
  h.proto = kProtocol;
  uint8_t b[kMaxPayload];
  Writer w(b, sizeof b);
  encode(h, w);
  dispatchFrame(Msg::Hello, 0, b, w.size());
  testNow = 100;
  assert(slink::synthUp());
  testNow = 600;
  assert(!slink::synthUp());
  uint8_t out[kMaxPayload];
  arm(out);
  deliverReply(Msg::FsWrite, 7);
  assert(slink::synthUp());
}
// The two reported projects are 59,584 and 90,263 bytes. Exercise both buffer
// remainders through the production 512-byte FileSink and 4 KiB RemoteFile.
static void remoteFile(Msg delayed) {
  establish();
  testWait = [](TickType_t) { runTurns(8); };
  slowType = delayed;
  for (size_t size : {59584u, 90263u}) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i)
      data[i] = (i * 37) % 251;
    delayNextOperation = true;
    auto file = storage::remoteFs().open("/projects/copy.tmp", FILE_WRITE);
    assert(file);
    hw::FileSink sink(file);
    for (size_t at = 0; at < size; at += 137) {
      size_t n = std::min<size_t>(137, size - at);
      assert(sink.write(data.data() + at, n));
    }
    assert(sink.flush());
    file.close();
    file = storage::remoteFs().open("/projects/copy.tmp", FILE_READ);
    assert(file);
    assert(file.size() == size);
    hw::FileSource source(file);
    std::vector<uint8_t> copy(size);
    assert(source.read(copy.data(), size));
    file.close();
    assert(copy == data);
    assert(hellos == 0);
    assert(slink::synthUp());
    assert(!card.opened);
  }
}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string c = argv[1];
  if (c == "reply")
    replyContact();
  else if (c == "progress")
    progressContact();
  else if (c == "noise")
    ignoreNoise();
  else if (c == "timeout")
    pendingAndTimeout();
  else if (c == "slow_file")
    slowFile();
  else if (c == "long_timeout")
    longTimeout();
  else if (c == "long_progress")
    longProgress();
  else if (c == "wrap")
    wrapClock();
  else if (c == "remote_open")
    remoteFile(Msg::FsOpen);
  else if (c == "remote_write")
    remoteFile(Msg::FsWrite);
  else if (c == "remote_read")
    remoteFile(Msg::FsRead);
  else if (c == "remote_close")
    remoteFile(Msg::FsClose);
  else
    assert(false);
  std::cout << c << " PASS\n";
}
