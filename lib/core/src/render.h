#pragma once
#include <stdint.h>
#include "model.h"
#include "sequencer.h"
#include "synth.h"

namespace mt {

constexpr uint32_t kRenderBlockUs = 1000000u * Synth::kBlock / kSynthRate;  // 4000
constexpr int16_t kSilence = 33;             // -60 dBFS
constexpr float kNormPeak = 0.891f * 32767;  // -1 dBFS

// Length of one pass of pattern idx / of the song (chain passes), in ticks and in us.
uint64_t passTicks(const Project& p, int idx);
uint64_t songTicks(const Project& p);
uint64_t passUs(const Project& p, int idx);
uint64_t songUs(const Project& p);
// Scales so that `peak` lands at -1 dBFS; peak 0 leaves the buffer as is.
void normalizePeak(int16_t* b, uint32_t n, int16_t peak);
// Frames to keep: past the last sample above kSilence, rounded up to `block`, at least one block.
uint32_t trimTail(const int16_t* b, uint32_t n, uint32_t block);
// "RS1", "RS2", ...: the first name not in p's sample list (ignoring case).
void nextResampleName(const Project& p, char out[kSampleNameMax + 1]);

struct RenderSpec {
  enum class Mode : uint8_t { Pattern, Song };
  Mode mode = Mode::Pattern;
  uint8_t pattern = 0;           // Mode::Pattern
  uint16_t tracksMask = 0xFFFF;  // bit = track sounds (on top of mute / solo)
  uint32_t tailBlocks = 0;       // blocks after the end: delay / reverb ring-out (500 = 2 s)
};

// Offline render of the INT tracks through `synth` on a virtual block clock, with a sequencer of
// its own over the live project: block n starts at n x kRenderBlockUs; the sequencer is processed one
// block ahead and its synth events are time-stamped into blocks like the audio task does
// (eventOffset). The length is analytic (passUs / songUs); after it only note-offs and releases pass
// (the next pass never starts), the sequencer stops at the first tail block. MIDI is dropped.
// Deterministic (fixed seed): two renders of the same spec are sample-identical.
// The caller owns the synth (reset before and after) and keeps the project still (lock).
class OfflineRender : public MidiSink {
 public:
  OfflineRender(Project& p, Synth& synth, const RenderSpec& spec);
  // Renders the next block; false when done (nothing written).
  bool renderBlock(int16_t out[Synth::kBlock]);
  uint32_t blocksTotal() const { return bodyBlocks_ + spec_.tailBlocks; }
  uint32_t blocksDone() const { return done_; }
  int16_t peak() const { return peak_; }
  uint32_t clips() const { return clips_; }
  // MidiSink: MIDI dropped; synth events queued for their block.
  void send(const uint8_t*, uint8_t) override {}
  void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) override;

  // Song mode on / off for the render; restores it and the track mutes (chain scenes) at the end.
  struct Guard {
    Guard(Project& p, const RenderSpec& s);
    ~Guard();
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Project& p;
    bool songMode;
    bool mute[kTracks];
  };

 private:
  struct Ev {
    uint64_t t;
    uint8_t track, len, b[3];
  };
  static constexpr int kFifo = 256;
  static bool passesAfterEnd(const uint8_t* b, uint8_t len);
  void drain();
  Synth& synth_;
  RenderSpec spec_;
  Sequencer seq_;
  uint64_t blockT_ = 0, bodyUs_ = 0;
  uint32_t bodyBlocks_ = 0, done_ = 0, clips_ = 0;
  int16_t peak_ = 0;
  Ev fifo_[kFifo];
  int fifoN_ = 0;
};

}  // namespace mt
