#include "tuning.h"

namespace mt {

void TuneMap::build(const TuningTable& t, uint8_t scaleRoot) {
  on_ = tuningValid(t);
  if (!on_) {
    for (float& o : off_) o = 0;
    return;
  }
  n_ = t.count;
  const int root = tuningRootKey(t, scaleRoot);
  const double per = t.mc[n_ - 1] * 1e-5;  // semitones
  perStep_ = static_cast<float>(per - n_);
  for (int k = 0; k < kKeys; ++k) {
    const int j = k - root;
    int q = j / n_;
    int d = j - q * n_;
    if (d < 0) {
      d += n_;
      --q;
    }
    const double cents = d ? t.mc[d - 1] * 1e-5 : 0.0;
    off_[k] = static_cast<float>(root + q * per + cents - k);
  }
}

MT_HOT float TuneMap::offAt(int k) const {
  int s = 0;  // periods added to k to land in 0..kKeys-1
  if (k < 0) s = (-k + n_ - 1) / n_;
  else if (k >= kKeys) s = -((k - (kKeys - 1) + n_ - 1) / n_);
  return off_[k + s * n_] - static_cast<float>(s) * perStep_;
}

}  // namespace mt
