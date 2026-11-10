#pragma once
#include <stdint.h>
#include "model.h"
#include "hot.h"

// Offline sample processing stays in flash: IRAM is reserved for live audio and its state.
namespace mt {

struct Onset {
  uint32_t pos;       // frame where the attack starts
  uint16_t strength;  // 1..1000, 1000 = the strongest found
};
constexpr int kMaxOnsets = 128;

// Transients of mono int16 data, ascending by pos; returns the count (<= max). Energy
// in three causal frequency bands, smoothed over 20 ms with a 2 ms hop. Positive log-energy
// rises above a -60 dBFS gate select attacks; peaks closer than 40 ms keep the stronger.
// Refinement follows the band which caused the attack, in 32-frame blocks. No FFT or heap.
// More than max candidates keep the strongest.
int detectOnsets(const int16_t* d, uint32_t frames, uint32_t rate, Onset* out, int max);
// detectOnsets over data fed in pieces (a sample read from the card): the same result for the same
// frames. frames: the total that will be fed.
class OnsetScan {
 public:
  OnsetScan(uint32_t frames, uint32_t rate, int max);
  void feed(const int16_t* d, uint32_t n);  // the next n frames, in order
  int finish(Onset* out);                   // after all frames; like detectOnsets

 private:
  static constexpr uint32_t kPeakBlocks = 64, kRefine = 32;
  static constexpr int kBands = 3;
  struct Cands {
    Onset out[kMaxOnsets];
    float fl[kMaxOnsets];
    int n = 0, max = kMaxOnsets;
    void remove(int i);
    void add(uint32_t pos, float f, uint32_t gap);
  };
  void window(uint32_t k);
  uint32_t refineBand(uint32_t from, uint32_t to, int band) const;
  uint16_t peak_[kBands][kPeakBlocks] = {};
  uint32_t frames_, windows_, gap_, hop_;
  uint32_t got_ = 0, k_ = 0, hopCount_ = 0;
  float dc_ = 0, low_ = 0, middle_ = 0;
  float dcAlpha_ = 0, lowAlpha_ = 0, middleAlpha_ = 0, smoothAlpha_ = 0;
  float energy_[kBands] = {}, smooth_[kBands] = {};
  float prevDb_[kBands] = {-120.f, -120.f, -120.f};
  uint8_t fluxBand_[3] = {};
  float flux_[3] = {0, 0, 0};  // k - 2, k - 1, k: the peak test runs on k - 1
  Cands c_;
};

// Nearest onset to frame pos, -1 if n == 0.
int nearestOnset(const Onset* o, int n, uint32_t pos);
// First onset strictly after (dir > 0) or before (dir < 0) pos, -1 if none.
int stepOnset(const Onset* o, int n, uint32_t pos, int dir);
// TRANS chop of Start..End: a slice at Start, plus up to kMaxSlices - 1 strongest onsets inside
// with strength >= (100 - chopThresh) * 10, skipping those within 10 ms of Start.
void chopTransients(Instrument& m, const Onset* o, int n, uint32_t len, uint32_t rate);

}  // namespace mt
