#include <cassert>
#include <cstring>
#include "Arduino.h"
#include "freertos/task.h"
#include "storage/space_query.h"
#include "link/link.h"
#include "link_fs.h"

using namespace mt::link;
using storage::SpaceState;
uint32_t boot = 7;
bool connected = true, restartDuringQuery = false, malformed = false;
int calls = 0;
int16_t cardError = kErrOk;
namespace slink {
uint32_t bootId() { return boot; }
bool synthUp() { return connected; }
bool request(Msg type, const uint8_t* p, int size, Msg replyType, uint8_t* reply, int& len, uint32_t timeout) {
  ++calls;
  assert(type == Msg::FsStat && replyType == type && timeout >= 2000);
  Reader r(p, size); char path[8]; r.str(path, sizeof path);
  assert(!std::strcmp(path, "/") && r.u8() == kFsStatSpace && r.ok());
  testNow += 6500;
  if (restartDuringQuery) ++boot;
  Writer w(reply, kMaxPayload);
  w.u16(static_cast<uint16_t>(cardError));
  w.u8(1); w.u32(0); w.u32(32000); w.u32(12345);
  len = malformed ? 1 : w.size();
  return true;
}
}
static void runTask() { assert(taskFn); taskFn(nullptr); }
int main(int argc, char** argv) {
  assert(argc == 2);
  const bool wrap = !std::strcmp(argv[1], "wrap");
  testNow = wrap ? UINT32_MAX - 3000 : 100;
  if (!std::strcmp(argv[1], "fail")) {
    taskFails = true; storage::beginSpaceQuery();
    assert(storage::spaceQuery().state == SpaceState::Failed && calls == 0);
    taskFails = false; connected = false; storage::beginSpaceQuery();
    assert(storage::spaceQuery().state == SpaceState::Failed && tasks == 0);
    connected = true; storage::beginSpaceQuery(); runTask();
    assert(storage::spaceQuery().state == SpaceState::Ready);
    return 0;
  }
  const auto before = testNow;
  storage::beginSpaceQuery();
  for (int i = 0; i < 100; ++i) {
    storage::beginSpaceQuery();
    assert(storage::spaceQuery().state == SpaceState::Pending);
  }
  assert(testNow == before && tasks == 1 && calls == 0);
  runTask();
  assert(deletions == 1 && calls == 1);
  assert(storage::spaceQuery().state == SpaceState::Ready && storage::spaceQuery().freeMb == 12345);
  if (!std::strcmp(argv[1], "cache") || wrap) {
    storage::beginSpaceQuery(); assert(tasks == 1);
    testNow += 9999; storage::beginSpaceQuery(); assert(tasks == 1);
    ++testNow; storage::beginSpaceQuery(); assert(tasks == 2);
    runTask(); assert(storage::spaceQuery().state == SpaceState::Ready);
  } else if (!std::strcmp(argv[1], "boot")) {
    ++boot;
    assert(storage::spaceQuery().state == SpaceState::Failed);
    storage::beginSpaceQuery(); assert(tasks == 2);
    restartDuringQuery = true; runTask();
    assert(storage::spaceQuery().state == SpaceState::Failed && storage::spaceQuery().freeMb == 0);
    restartDuringQuery = false; storage::beginSpaceQuery(); runTask();
    assert(storage::spaceQuery().state == SpaceState::Ready);
  } else if (!std::strcmp(argv[1], "card")) {
    testNow += 10000; cardError = kErrNoCard;
    storage::beginSpaceQuery(); runTask();
    assert(storage::spaceQuery().state == SpaceState::Failed && storage::spaceQuery().freeMb == 0);
    cardError = kErrOk; malformed = true;
    storage::beginSpaceQuery(); runTask();
    assert(storage::spaceQuery().state == SpaceState::Failed);
    malformed = false; storage::beginSpaceQuery(); runTask();
    assert(storage::spaceQuery().state == SpaceState::Ready);
  } else { assert(false); }
}
