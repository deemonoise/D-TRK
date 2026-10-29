#include "practice.h"
namespace mt {
void RecordPractice::arm(bool on, bool playing, uint8_t pattern, uint32_t loop) {
  if (armed_ == on) return;
  armed_ = on;
  if (!on) { stop(); return; }
  endedAt_ = 0;
  active_ = playing;
  pattern_ = pattern;
  loop_ = loop;
}
void RecordPractice::startCount(uint64_t now, uint16_t bpm, uint8_t bars) {
  active_ = false;
  total_ = (bars > 2 ? 2 : bars) * 4;
  emitted_ = 0;
  beatUs_ = 60000000u / (bpm < 20 ? 20 : bpm > 300 ? 300 : bpm);
  next_ = now;
  counting_ = total_ > 0 && armed_;
}
RecordPractice::Tick RecordPractice::tick(uint64_t now) {
  if (!counting_ || now < next_) return Tick::None;
  if (emitted_ == total_) { counting_ = false; return Tick::Start; }
  const bool accent = emitted_ % 4 == 0;
  ++emitted_;
  next_ += beatUs_;
  return accent ? Tick::Accent : Tick::Beat;
}
void RecordPractice::started(uint8_t pattern, uint32_t loop) {
  counting_ = false;
  active_ = armed_;
  pattern_ = pattern;
  loop_ = loop;
}
void RecordPractice::stop() { active_ = counting_ = false; }
void RecordPractice::observe(bool playing, uint8_t pattern, uint32_t loop, uint64_t boundary, bool onePass) {
  if (!playing) { active_ = false; return; }
  if (!active_) return;
  if (onePass && (pattern != pattern_ || loop != loop_)) {
    active_ = armed_ = false;
    endedAt_ = boundary;
    ++completed_;
  }
  if (!onePass) { pattern_ = pattern; loop_ = loop; }
}
uint8_t RecordPractice::remaining() const {
  return counting_ ? total_ - emitted_ + (emitted_ ? 1 : 0) : 0;
}
void MetronomeVoice::trigger(int offset, bool accent, uint8_t level) {
  offset_ = offset < 0 ? 0 : offset;
  accent_ = accent;
  pendingLevel_ = level > 127 ? 127 : level;
}
void MetronomeVoice::mix(int16_t* l, int16_t* r, int n) {
  for (int i = 0; i < n; ++i) {
    if (offset_ == i) {
      left_ = 882; phase_ = 0;
      step_ = (accent_ ? 2300.f : 1500.f) / 44100.f;
      level_ = pendingLevel_ * 60.f;
      offset_ = -1;
    }
    if (!left_) continue;
    const float env = left_ / 882.f;
    phase_ += step_;
    if (phase_ >= 1.f) phase_ -= 1.f;
    const float wave = phase_ < .5f ? 4.f * phase_ - 1.f : 3.f - 4.f * phase_;
    const int v = static_cast<int>(wave * env * env * level_);
    const int a = l[i] + v, b = r[i] + v;
    l[i] = static_cast<int16_t>(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
    r[i] = static_cast<int16_t>(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    --left_;
  }
  if (offset_ >= n) offset_ -= n;
}
void MetronomeVoice::reset() { offset_ = -1; left_ = 0; }
}
