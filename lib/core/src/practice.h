#pragma once
#include <stdint.h>
namespace mt {
// Live link event: 0xF9 accent level, mixed by the output after recording capture.
constexpr uint8_t kMetronomeMsg = 0xF9;
struct PracticePrefs {
  uint8_t countBars = 0, clickMode = 0, clickLevel = 64;
  bool onePass = false;
};
inline uint8_t practiceBits(const PracticePrefs& p) {
  return static_cast<uint8_t>(p.countBars | (p.clickMode << 2) | (p.onePass ? 16 : 0));
}
inline PracticePrefs practicePrefs(uint8_t bits, uint8_t level) {
  return PracticePrefs{static_cast<uint8_t>((bits & 3) <= 2 ? bits & 3 : 0),
                       static_cast<uint8_t>(((bits >> 2) & 3) <= 2 ? (bits >> 2) & 3 : 0),
                       static_cast<uint8_t>(level > 127 ? 127 : level), (bits & 16) != 0};
}
class RecordPractice {
 public:
  enum class Tick { None, Beat, Accent, Start };
  void arm(bool on, bool playing, uint8_t pattern, uint32_t loop);
  void startCount(uint64_t now, uint16_t bpm, uint8_t bars);
  Tick tick(uint64_t now);
  void started(uint8_t pattern, uint32_t loop);
  void stop();
  // Observe the HEARD position, not the sequencer lookahead; boundary is its step timestamp.
  void observe(bool playing, uint8_t pattern, uint32_t loop, uint64_t boundary, bool onePass);
  bool armed() const { return armed_; }
  bool active() const { return active_; }
  bool counting() const { return counting_; }
  uint8_t remaining() const;
  uint64_t next() const { return counting_ ? next_ : UINT64_MAX; }
  uint32_t completed() const { return completed_; }
  uint64_t endedAt() const { return endedAt_; }
 private:
  bool armed_ = false, active_ = false, counting_ = false;
  uint8_t pattern_ = 0, total_ = 0, emitted_ = 0;
  uint32_t loop_ = 0, completed_ = 0, beatUs_ = 500000;
  uint64_t next_ = 0, endedAt_ = 0;
};
class MetronomeVoice {
 public:
  void trigger(int offset, bool accent, uint8_t level);
  void mix(int16_t* left, int16_t* right, int n);
  void reset();
 private:
  int offset_ = -1, left_ = 0;
  float phase_ = 0, step_ = 0, level_ = 0;
  bool accent_ = false;
  uint8_t pendingLevel_ = 0;
};
}
