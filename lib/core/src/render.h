#pragma once
#include <stdint.h>
#include "model.h"
#include "sequencer.h"
#include "synth.h"

namespace mt {

constexpr uint32_t kRenderBlockUs = 1000000u * Synth::kBlock / kSynthRate;  // 2902
constexpr int16_t kSilence = 33;             // -60 dBFS
constexpr float kNormPeak = 0.891f * 32767;  // -1 dBFS

// Length of one pass of pattern idx / of the song (chain passes), in ticks and in us.
uint64_t passTicks(const Project& p, int idx);
uint64_t songTicks(const Project& p);
uint64_t passUs(const Project& p, int idx);
uint64_t songUs(const Project& p);
// Interleaved stereo (L, R) of `frames` frames: scales so that `peak` lands at -1 dBFS; peak 0 leaves
// the buffer as is.
void normalizePeak(int16_t* lr, uint32_t frames, int16_t peak);
// The same on n samples of any layout (mono).
void normalizeSamples(int16_t* b, uint32_t n, int16_t peak);
// Frames to keep of interleaved stereo: past the last frame with a sample above kSilence, rounded up
// to `block`, at least one block.
uint32_t trimTail(const int16_t* lr, uint32_t frames, uint32_t block);
// "RS1", "RS2", ...: the first name not in p's sample list (ignoring case).
void nextResampleName(const Project& p, char out[kSampleNameMax + 1]);

struct RenderSpec {
  enum class Mode : uint8_t { Pattern, Song };
  Mode mode = Mode::Pattern;
  uint8_t pattern = 0;           // Mode::Pattern
  uint16_t tracksMask = 0xFFFF;  // bit = track sounds (on top of mute / solo)
  uint32_t tailBlocks = 0;       // blocks after the end: delay / reverb ring-out (689 = 2 s)
};

// One synth event of a rendered block: its sample offset in the block and the message.
struct SeqEv {
  uint8_t off, track, len, b[3];
};

// The sequencing half of an offline render: a sequencer of its own over the live project on a
// virtual block clock; block n starts at n x kRenderBlockUs; the sequencer is processed one block
// ahead and its synth events are time-stamped into blocks like the audio task does (eventOffset).
// The length is analytic (passUs / songUs); after it only note-offs and releases pass (the next pass
// never starts), the sequencer stops at the first tail block. MIDI is dropped. Deterministic (fixed
// seed). The caller keeps the project still (lock).
class OfflineSequence : public MidiSink {
 public:
  static constexpr int kMaxEvents = 256;  // per block
  OfflineSequence(Project& p, const RenderSpec& spec);
  // The next block's events (out: room for kMaxEvents) in the order the synth gets them; false
  // when done (nothing written).
  bool nextBlock(SeqEv* out, int& n);
  uint32_t blocksTotal() const { return bodyBlocks_ + spec_.tailBlocks; }
  uint32_t blocksDone() const { return done_; }
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
  static constexpr int kFifo = kMaxEvents;
  static bool passesAfterEnd(const uint8_t* b, uint8_t len);
  RenderSpec spec_;
  Sequencer seq_;
  uint64_t blockT_ = 0, bodyUs_ = 0;
  uint32_t bodyBlocks_ = 0, done_ = 0;
  Ev fifo_[kFifo];
  int fifoN_ = 0;
};

// Renders a block's events through a synth: stereo out, peak and clip count.
struct BlockRender {
  int16_t peak = 0;
  uint32_t clips = 0;
  void render(Synth& synth, const SeqEv* ev, int n, int16_t l[Synth::kBlock], int16_t r[Synth::kBlock]);
};

// Offline render of the INT tracks: OfflineSequence through `synth`. Two renders of the same spec are
// sample-identical. The caller owns the synth (reset before and after) and keeps the project still.
class OfflineRender {
 public:
  using Guard = OfflineSequence::Guard;
  OfflineRender(Project& p, Synth& synth, const RenderSpec& spec) : synth_(synth), seq_(p, spec) {}
  // Renders the next stereo block; false when done (nothing written).
  bool renderBlock(int16_t l[Synth::kBlock], int16_t r[Synth::kBlock]);
  uint32_t blocksTotal() const { return seq_.blocksTotal(); }
  uint32_t blocksDone() const { return seq_.blocksDone(); }
  int16_t peak() const { return out_.peak; }
  uint32_t clips() const { return out_.clips; }

 private:
  Synth& synth_;
  OfflineSequence seq_;
  BlockRender out_;
  SeqEv ev_[OfflineSequence::kMaxEvents];
};

}  // namespace mt
