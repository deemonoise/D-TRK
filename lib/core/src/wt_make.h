#pragma once
#include <stddef.h>
#include <stdint.h>
#include "wt_mip.h"

// Wavetable maker: a 64 x 256 source table (kWtSrcSamples int16, the canonical layout wtImport takes)
// made from a sample region (CYCLE: one pitch period per frame; WINDOW: 64 equal windows) or from
// drawn keyframes. Shared by the synth board (WtRender, from its card) and the ESP (drawing).
namespace mt {

struct ByteSink;

enum class WtMode : uint8_t { Cycle, Window, Count };
enum class WtNorm : uint8_t { Frame, Table, Count };
enum class WtMakeErr : uint8_t { Ok, TooShort, NoPitch, Silent, NoMemory, Count };
const char* wtMakeErrText(WtMakeErr e);  // short, upper case, for toasts

struct WtMakeParams {
  WtMode mode = WtMode::Cycle;
  WtNorm norm = WtNorm::Frame;
  bool seam = true;
};

constexpr uint32_t kWtMakeMinLen = kWtFrames * 32;  // shortest region (~46 ms at 44.1 kHz)
constexpr float kWtMinHz = 40.0f, kWtMaxHz = 2000.0f;  // CYCLE pitch range
constexpr int kWtMinPitched = 8;                    // CYCLE: fewer frames with a pitch = NoPitch
constexpr uint32_t kWtMakeRead = 1024;              // most frames one nextRead asks for
constexpr int kWtMakePeak = 30000;                  // result peak
constexpr int kWtSeamPts = kWtFrameLen / 8;         // WINDOW seam crossfade, points of a frame

// One frame (kWtFrameLen points) of x[0..n) from start over len samples (fractional). len >
// kWtFrameLen: point i = mean of x over [start + (i - 0.5) s, start + (i + 0.5) s), s = len / 256,
// sample j covering [j - 0.5, j + 0.5) (an area average: a low-pass, no aliasing); else Catmull-Rom at
// start + i s. Indices outside x wrap modulo n (a frame is one cycle).
void wtResample(const float* x, int n, double start, double len, float* out);

// Naive one-cycle shapes, kWtFrameLen points, peak 1 (the mip levels band-limit them).
enum class WtShape : uint8_t { Sin, Tri, Saw, Sqr, Count };
void wtShape(WtShape s, float* out);
void wtSmooth(float* f);  // one [1 2 1] / 4 pass, circular

// Drawing keyframes: frame 0..63 and one cycle in -1..1.
constexpr int kWtKeysMax = 16;
struct WtKey {
  uint8_t frame = 0;
  float pts[kWtFrameLen] = {};
};
void wtSortKeys(WtKey* keys, int n);
// Frame f between the keys (sorted, n >= 1): the key itself, or linear between its neighbours;
// before the first / after the last key that key.
void wtKeyAt(const WtKey* keys, int n, int f, float* out);
// All 64 frames into src (kWtSrcSamples): sorts the keys; false unless keys 0 and 63 are there.
bool wtFromKeys(WtKey* keys, int n, int16_t* src);

// Pitch (YIN-lite). d[t] for t in [t0, t1): sum over j < w of (x[j] - x[j + t])^2; x holds w + t1 - 1.
void wtDiff(const float* x, int w, int t0, int t1, float* d);
// Cumulative mean normalized difference dn[0..tauMax] of d[0..tauMax] (dn[0] = 1).
void wtCumNorm(const float* d, float* dn, int tauMax);
// First dip of dn below 0.15 in [tauMin, tauMax] (to its local minimum), else the lowest point if below
// 0.35; refined by a parabola through d. period fractional, conf = 1 - dn. False: no pitch.
bool wtPickPeriod(const float* d, const float* dn, int tauMin, int tauMax, float& period, float& conf);
// The three over x (w + tauMax points); d and dn hold tauMax + 1.
bool wtYin(const float* x, int w, int tauMin, int tauMax, float* d, float* dn, float& period, float& conf);

// Chunked maker over a region [from, to) of a sample at rate. Usage:
//   while (!m.done()) { if (m.nextRead(at, n)) { read up to n frames at at; m.feed(buf, got); }
//                       else m.step(); }
// Buffers are malloc'ed in begin() and freed by end() / the destructor (the result too).
class WtMaker {
 public:
  WtMaker() = default;
  ~WtMaker() { end(); }
  WtMaker(const WtMaker&) = delete;
  WtMaker& operator=(const WtMaker&) = delete;

  // Ok, or the error (then done() at once): TooShort, NoMemory.
  WtMakeErr begin(const WtMakeParams& p, uint32_t from, uint32_t to, uint32_t rate);
  // The source frames needed now: [at, at + n), n <= kWtMakeRead. False: none (call step()).
  bool nextRead(uint32_t& at, uint32_t& n) const;
  // got (1..n) frames of the last nextRead.
  void feed(const int16_t* x, uint32_t got);
  // A portion of the computation (< 1 ms on the synth board). False when there is nothing to do.
  bool step();
  bool done() const { return st_ == St::Done; }
  WtMakeErr error() const { return err_; }
  const int16_t* result() const { return err_ == WtMakeErr::Ok && done() ? out_ : nullptr; }
  int pitched() const { return pitched_; }  // CYCLE: frames whose pitch was found (WINDOW: 64)
  int progress() const;                     // 0..kWtFrames
  void end();                               // frees everything

 private:
  enum class St : uint8_t { Idle, Read1, Diff, Read2, Read, Finish, Done };
  void fail(WtMakeErr e);
  uint32_t winPos(int k) const;
  void prepWindow();  // win_ int16 -> float, mean removed
  void smoothPeriods();
  void extractCycle(int k);
  void windowSample(float v);  // WINDOW: one source sample into the cells
  void cell(float v);          // WINDOW: the next output cell
  void storeFrame(int k, const float* f);
  void finish();

  WtMakeParams p_;
  St st_ = St::Idle;
  WtMakeErr err_ = WtMakeErr::Ok;
  uint32_t from_ = 0, len_ = 0, rate_ = 0;
  int16_t* out_ = nullptr;  // kWtSrcSamples
  // CYCLE
  int tauMin_ = 0, tauMax_ = 0, wa_ = 0, k_ = 0, lag_ = 0;
  uint32_t fill_ = 0;
  float* win_ = nullptr;  // wa_ (int16 while read, float after prepWindow)
  float* d_ = nullptr;    // tauMax_ + 1
  float* dn_ = nullptr;   // tauMax_ + 1
  float period_[kWtFrames] = {};
  bool found_[kWtFrames] = {};
  int pitched_ = 0;
  // WINDOW
  uint32_t readAt_ = 0;
  int cell_ = 0;
  double cellW_ = 0, cellEnd_ = 0, acc_ = 0;
  int16_t hist_[4] = {};
  int64_t last_ = -1;
  float frame_[kWtFrameLen] = {}, tail_[kWtSeamPts] = {};
  // Per stored frame: scale to the stored int16 (0 = silent), true RMS (DC removed) and peak.
  float scale_[kWtFrames] = {}, rms_[kWtFrames] = {}, peak_[kWtFrames] = {};
  int framesOut_ = 0;
};

// The maker over a whole array x (frames), reading pieces of at most piece frames. out = kWtSrcSamples.
WtMakeErr wtMakeArray(const int16_t* x, uint32_t frames, uint32_t from, uint32_t to, uint32_t rate,
                      const WtMakeParams& p, int16_t* out, int* pitched = nullptr, uint32_t piece = kWtMakeRead);

// The table as a WAV: mono 16 bit, 44100, "smpl" root 60, "mtcr" (sampleCrc), Serum "clm " 256.
constexpr uint32_t kWtClmBytes = 8 + 32;
constexpr uint32_t kWtWavBytes = 12 + 24 + 44 + 16 + kWtClmBytes + 8 + kWtSrcSamples * 2;
bool wtWavWrite(const int16_t* src, ByteSink& out);

}  // namespace mt
