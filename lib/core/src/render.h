#pragma once
#include <stdint.h>
#include "model.h"
#include "sequencer.h"
#include "synth.h"

namespace mt {

constexpr uint32_t kRenderBlockUs = 1000000u * Synth::kBlock / kSynthRate;  // 2902
constexpr int16_t kSilence = 33;             // -60 dBFS
constexpr float kNormPeak = 0.891f * 32767;     // -1 dBFS
constexpr float kNormPeakHot = 0.966f * 32767;  // -0.3 dBFS (one-shot bounce)
constexpr float kTapScale = 32767.f * 0.25f;    // a RenderTaps tap (mix units) to int16, as MAIN 100
// The preview tap (one-shot bounce) to int16: kTapScale with the preview fader (kPreviewVol) undone, so
// a sampler at vol 127 plays an unnormalized one-shot back at its source's level (KIT bake).
constexpr float kPrevTapScale = kTapScale * 127.f / kPreviewVol;

constexpr int kRenderSlotsMax = 512;  // RenderSpec::maxSlots

// Length of one pass of pattern idx / of the song, in ticks and in us. The song: the follow-action path
// from slot 0 with Project::seed (followWalk, salt 0), at most maxSlots passes (a song of NEXT slots:
// each slot once). songUs: each slot's BPM is the tempo from its start ("-" keeps it).
uint64_t passTicks(const Project& p, int idx);
uint64_t songTicks(const Project& p, int maxSlots = kSongMax);
uint64_t passUs(const Project& p, int idx);
uint64_t ticksToUs(const Project& p, uint64_t ticks);  // at p's BPM
uint64_t songUs(const Project& p, int maxSlots = kSongMax);
// Ticks of track's own loop in pt: its length (Pattern::trackLen, polymeter) at its speed (x2 / x4). The
// whole pass when that is not shorter or the track is slow (1/2, 1/4: it counts through pass ends).
uint64_t trackLoopTicks(const Pattern& pt, int track);
// Interleaved stereo (L, R) of `frames` frames: scales so that `peak` lands at -1 dBFS; peak 0 leaves
// the buffer as is.
void normalizePeak(int16_t* lr, uint32_t frames, int16_t peak, float target = kNormPeak);
// The same on n samples of any layout (mono). target: the peak's level after (kNormPeak / kNormPeakHot).
void normalizeSamples(int16_t* b, uint32_t n, int16_t peak, float target = kNormPeak);
// Frames to keep of interleaved stereo: past the last frame with a sample above kSilence, rounded up
// to `block`, at least one block.
uint32_t trimTail(const int16_t* lr, uint32_t frames, uint32_t block);
// The toast of a render into the bank (Resample, one-shot): "NAME 3.2s" (tenths cut), ", N CLIPS" when
// clips (", 1 CLIP"). Cut to n.
void renderDoneText(const char* name, uint32_t frames, uint32_t clips, char* out, int n);
// "RS1", "RS2", ...: the first name not in p's sample list (ignoring case).
void nextResampleName(const Project& p, char out[kSampleNameMax + 1]);

struct RenderSpec {
  enum class Mode : uint8_t { Pattern, Song };
  Mode mode = Mode::Pattern;
  uint8_t pattern = 0;           // Mode::Pattern
  uint16_t tracksMask = 0xFFFF;  // bit = track sounds (on top of mute / solo)
  uint16_t maxSlots = kSongMax;  // Mode::Song: at most this many slot passes (1..kRenderSlotsMax)
  uint32_t tailBlocks = 0;       // blocks after the end: delay / reverb ring-out (689 = 2 s)
  uint8_t loops = 1;             // Mode::Pattern: passes of the pattern in the body (1..17)
  uint32_t cutTicks = 0;         // Mode::Pattern: the body ends this far into its last pass (0: the whole pass)
};

// One synth event of a rendered block: its sample offset in the block and the message.
struct SeqEv {
  uint8_t off, track, len, b[3];
};

// The sequencing half of an offline render: a sequencer of its own over the live project (Mode::Song:
// SONG from slot 0 along the follow path of salt 0, see songTicks; else LOOP) on a virtual block clock;
// block n starts at n x kRenderBlockUs; the sequencer is processed one block ahead and its synth events
// are time-stamped into blocks like the audio task does (eventOffset). The length is analytic (passUs /
// songUs); after it only note-offs and releases pass (the next pass never starts), the sequencer stops at
// the first tail block. MIDI is dropped. Deterministic (fixed seed). The caller keeps the project still
// (lock).
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

  // Restores the track mutes (written by the song's slot masks) and the BPM (slot tempos) at the end of the render.
  struct Guard {
    explicit Guard(Project& p);
    ~Guard();
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Project& p;
    bool mute[kTracks];
    uint16_t bpm;
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
  // Peak and clips of a block (render() calls it on the master).
  void measure(const int16_t* l, const int16_t* r, int n = Synth::kBlock);
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
