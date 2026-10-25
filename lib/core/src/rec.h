#pragma once
#include <atomic>
#include <stdint.h>
#include "hot.h"
#include "model.h"
#include "synth_osc.h"
#include "wav.h"

namespace mt {

// Recording from MIC / LINE / RESAMPLE (the synth's own output) on the synth board: the audio
// interrupt feeds blocks to a RecCapture (ring in PSRAM, meter, threshold, pre-roll), loop() drains
// the ring into a WAV file through a RecWriter. Only the capture side is MT_HOT.

constexpr uint32_t kRecRate = kSynthRate;                // 44100
constexpr uint32_t kRecMaxFrames = 5u * 60u * kRecRate;  // 5 min, then an automatic STOP
constexpr uint32_t kRecPreRollFrames = kRecRate / 50;    // 20 ms kept before the threshold trips
constexpr uint32_t kRecMinFrames = kRecRate / 20;        // shorter takes are TOO SHORT
constexpr uint32_t kRecRingBytes = 1u << 20;             // ring in PSRAM (~6 s stereo)
constexpr int8_t kRecThreshOff = -128;                   // THRESH OFF: recording starts at once
enum class RecSource : uint8_t { Mic, Line, Resample };
enum class RecState : uint8_t { Idle, Armed, Rec, Saving };

// Single producer (the audio interrupt) / single consumer (loop()) ring of interleaved stereo int16
// frames. Capacity is a power of two (init rounds down); the counters run free modulo 2^32.
class RecRing {
 public:
  void init(int16_t* mem, uint32_t frames);
  // Producer: n frames, or none (and overflowed()) when fewer than n are free.
  bool push(const int16_t* l, const int16_t* r, uint32_t n) MT_HOT;
  // Consumer: up to max frames into interleaved, the count.
  uint32_t pop(int16_t* interleaved, uint32_t max);
  uint32_t size() const { return head_.load() - tail_.load(); }
  uint32_t capacity() const { return mask_ + 1; }
  // Producer, while the consumer does not read (Armed): drops all but the newest n frames.
  void keepLast(uint32_t n) MT_HOT;
  bool overflowed() const { return overflow_.load(); }
  void reset();  // empty, no overflow; neither side running

 private:
  int16_t* mem_ = nullptr;
  uint32_t mask_ = 0;
  std::atomic<uint32_t> head_{0}, tail_{0};
  std::atomic<bool> overflow_{false};
};

// Peak and clip count of the input since the last take(): fed by the interrupt for every block while
// the REC window is open, taken by loop() for RecLevel.
struct RecMeter {
  std::atomic<uint16_t> peak{0};
  std::atomic<uint32_t> clips{0};  // samples at full scale (|s| >= 32767)
  void feed(const int16_t* s, uint32_t n) MT_HOT;
  void take(uint16_t& p, uint32_t& c);  // and reset
};

// Threshold level for -127..0 dB: 32767 * 10^(db / 20); kRecThreshOff -> 0.
int16_t recThreshLevel(int8_t db);
// GAIN range of a source: MIC 0..63 dB (preamp steps + ADC gain, codec side), LINE 0..15 (line-in
// level), RESAMPLE none (0).
int recGainClamp(RecSource src, int gain);

// The interrupt side of a take: arm() starts it (Armed until a sample reaches the threshold, keeping
// the last kRecPreRollFrames in the ring; THRESH OFF: Rec at once), feed() takes every input block
// (the meter runs in any state), and the take ends in Idle on stop(), maxFrames (autoStopped()) or
// a full ring (overflow(): the card is too slow; what is in the ring stays). MIC is mono: its left
// input goes to both halves of a ring frame (the writer keeps the left one).
class RecCapture {
 public:
  void init(int16_t* mem, uint32_t frames) { ring_.init(mem, frames); }
  // Only while Idle and the consumer does not read.
  void arm(RecSource src, int8_t threshDb, uint32_t maxFrames);
  void feed(const int16_t* l, const int16_t* r, uint32_t n) MT_HOT;
  // Only while Idle: the source the meter follows before a take (MIC: the left input only).
  void watch(RecSource src) { src_ = src; }
  void stop() { state_.store(RecState::Idle); }
  RecState state() const { return state_.load(); }
  RecSource source() const { return src_; }
  uint8_t channels() const { return src_ == RecSource::Mic ? 1 : 2; }
  uint32_t frames() const { return frames_.load(); }  // in the take, pre-roll included
  bool overflow() const { return overflow_.load(); }
  bool autoStopped() const { return autoStopped_.load(); }
  RecRing& ring() { return ring_; }
  RecMeter& meter() { return meter_; }

 private:
  RecRing ring_;
  RecMeter meter_;
  RecSource src_ = RecSource::Line;
  int16_t level_ = 0;
  uint32_t maxFrames_ = kRecMaxFrames;
  std::atomic<RecState> state_{RecState::Idle};
  std::atomic<uint32_t> frames_{0};
  std::atomic<bool> overflow_{false}, autoStopped_{false};
};

// Mono mix of n interleaved stereo frames, as the bank imports a stereo WAV (wavToMono: the sum / 2
// toward zero, no normalization).
void recMonoMix(const int16_t* lr, uint32_t n, int16_t* out);

// Turns ring frames into the data of a take's WAV: stereo as is, mono the left half; counts frames
// and the crc the header carries (sampleCrc of the mono data, for stereo of its mono mix). The caller
// writes kWavHeaderBytes of zeros first and puts finish()'s header there at the end.
class RecWriter {
 public:
  using Sink = bool (*)(const void* d, uint32_t n, void* ctx);  // false: write failed
  void begin(uint16_t channels, Sink sink, void* ctx);
  bool write(const int16_t* lr, uint32_t n);  // n interleaved stereo frames; false once failed()
  void header(uint8_t out[kWavHeaderBytes]) const;
  bool finish(uint8_t out[kWavHeaderBytes]) const {  // header(); false if a write failed
    header(out);
    return !failed_;
  }
  uint32_t frames() const { return frames_; }
  uint32_t crc() const { return crc_; }
  bool failed() const { return failed_; }

 private:
  Sink sink_ = nullptr;
  void* ctx_ = nullptr;
  uint16_t ch_ = 2;
  uint32_t frames_ = 0, crc_ = 0;
  bool failed_ = false;
};

// Takes are files /samples/rec/RECn.wav on the synth board's card (written as RECn.wav.tmp, renamed
// when complete) and samples "RECn" in the project. n: 1..kRecNumberMax.
constexpr int kRecNumberMax = 9999;
constexpr int kRecPathMax = 32;
void recName(int n, char out[kSampleNameMax + 1]);
void recPath(int n, char out[kRecPathMax]);
void recTmpPath(int n, char out[kRecPathMax]);
// First n in from..kRecNumberMax, then 1..from-1 (from outside 1..kRecNumberMax: 1), whose "RECn"
// is not in p's sample list; 0 when all are taken (cannot happen while kProjSamples < kRecNumberMax).
int nextRecNumber(const SynthModel& p, int from);
// Puts a finished take into p and on instrument instr: sample `name` (projSampleSet) with crc /
// frames, the instrument plays it from the start (slices cleared) at root. The sample index, or -1
// (list full, bad name or instrument) with p unchanged.
int recBind(Project& p, int instr, const char* name, uint32_t crc, uint32_t frames, uint8_t root);

}  // namespace mt
