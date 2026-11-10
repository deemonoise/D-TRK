#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include "rec_ui.h"

using mt::RecState;
uint32_t nowMs = 1;
uint32_t millis() { return nowMs; }
int sends = 0, cancels = 0, polls = 0, starts = 0;
namespace storage {
enum class SpaceState { Idle, Pending, Ready, Failed };
struct SpaceResult { SpaceState state; uint32_t freeMb; };
SpaceResult result{SpaceState::Idle, 0};
int queries = 0;
void beginSpaceQuery() {
  if (result.state != SpaceState::Pending) { result = {SpaceState::Pending, 0}; ++queries; }
}
SpaceResult spaceQuery() { return result; }
bool remoteSpace(uint32_t& total, uint32_t& free) { nowMs += 6500; total = 32000; free = 16000; return true; }
}
namespace audio {
bool recLevel(mt::link::RecLevelRep&) { ++polls; return true; }
void recCancel() { ++cancels; }
}
struct List {
  void setEdit(bool) {}
  void setSel(int) {}
};
struct App {
  int invalidates = 0;
  struct Status { bool playing = false; } st;
  Status status() const { return st; }
  void invalidate() { ++invalidates; }
  void toast(const char*) {}
};
class RecDialog {
 public:
  enum class Mode { Sample, Sd };
  App app_;
  List list_;
  Mode mode_ = Mode::Sample;
  int instr_ = 0;
  static constexpr int kSource = 0;
  static constexpr uint32_t kPollMs = 100, kRetryMs = 500;
  bool taking_ = false, open_ = false, pendingSet_ = false, waitPlay_ = false, preparing_ = false;
  uint32_t adoptUntil_ = 0, nextPoll_ = 0;
  int32_t freeMb_ = -1;
  int8_t thresh_ = mt::kRecThreshOff;
  mt::RecSource src_ = mt::RecSource::Mic;
  RecState state_ = RecState::Idle;
  std::function<void()> onSaved_;
  void reset() { taking_ = waitPlay_ = false; adoptUntil_ = 0; }
  void send() { ++sends; pendingSet_ = false; }
  void onPlay() { start(); }
  void level(const mt::link::RecLevelRep&, uint32_t) {}
  void start() { ++starts; }
  void stop() {}
  bool open(int, std::function<void()>);
  bool openSd();
  void poll();
  void closeWindow();
  mt::RecBtn btnState() const;
  void toggle();
};

/* METHODS */

int main(int argc, char** argv) {
  assert(argc == 2);
  RecDialog r;
  if (!std::strcmp(argv[1], "taking")) {
    r.mode_ = RecDialog::Mode::Sd; r.taking_ = true;
    assert(r.openSd()); assert(nowMs == 1); assert(storage::queries == 0); assert(sends == 0);
    assert(r.taking_);
    return 0;
  }
  assert(r.openSd());
  assert(nowMs == 1); // opening must never call the blocking cold-card path
  assert(sends == 0);
  if (!std::strcmp(argv[1], "cold")) {
    for (int i = 0; i < 70; ++i) { nowMs += 100; const auto before = nowMs; r.poll(); assert(nowMs == before); }
    assert(sends == 0 && polls == 0 && storage::queries == 1);
    assert(!mt::recBtnLive(r.btnState()));
    assert(mt::recCancelAct(r.btnState()) == mt::RecCancel::Close);
    char label[24]; mt::recBtnText(r.btnState(), 0, label, sizeof label);
    assert(!std::strcmp(label, "PREPARING"));
    r.toggle(); assert(starts == 0);
  } else if (!std::strcmp(argv[1], "reopen")) {
    r.closeWindow(); assert(!r.open_ && cancels == 0);
    assert(r.openSd()); assert(storage::queries == 1);
    r.closeWindow(); storage::result = {storage::SpaceState::Ready, 1234};
    r.poll(); assert(!r.open_ && sends == 0 && polls == 0);
  } else if (!std::strcmp(argv[1], "ready")) {
    r.pendingSet_ = true;
    storage::result = {storage::SpaceState::Ready, 1234}; nowMs += 7000;
    r.poll(); assert(r.freeMb_ == 1234 && !r.preparing_);
    assert(sends == 1 && polls == 1);
    r.toggle(); assert(starts == 1);
    r.closeWindow(); assert(cancels == 1);
  } else if (!std::strcmp(argv[1], "failed")) {
    storage::result = {storage::SpaceState::Failed, 99999}; nowMs += 7000;
    r.poll(); assert(!r.preparing_ && r.freeMb_ == -1);
    assert(sends == 1); // the recorder still validates the card itself at START
  } else if (!std::strcmp(argv[1], "sample")) {
    r.closeWindow(); assert(r.open(2, nullptr));
    r.poll(); assert(sends == 0 && polls == 0);
    storage::result = {storage::SpaceState::Ready, 1234}; nowMs += 7000;
    r.poll(); assert(sends == 1 && !r.preparing_);
  } else { assert(false); }
}
