#include "wt_make.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "project_io.h"
#include "sample_set.h"
#include "wav.h"

namespace mt {

namespace {

constexpr float kPiF = 3.14159265f;
constexpr int N = kWtFrameLen;
constexpr int kLagChunk = 64;          // YIN lags a step() computes
constexpr float kYinThresh = 0.15f;    // first dip below this is the period
constexpr float kYinAccept = 0.35f;    // lowest point above this = no pitch
constexpr float kSilentRms = 32.768f;  // -60 dBFS
constexpr float kMaxGain = 16.0f;      // FRAME norm gain ceiling

inline int wrapIdx(int64_t j, int n) {
  const int64_t m = j % n;
  return static_cast<int>(m < 0 ? m + n : m);
}

inline float catmull(float a, float b, float c, float d, float t) {
  return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}

// Integral of x (sample j covering [j - 0.5, j + 0.5)) over [a, b), a <= b.
double area(const float* x, int n, double a, double b) {
  a += 0.5;
  b += 0.5;
  int64_t j = static_cast<int64_t>(floor(a));
  double s = 0, t = a;
  while (t < b) {
    const double e = static_cast<double>(j + 1) < b ? static_cast<double>(j + 1) : b;
    s += x[wrapIdx(j, n)] * (e - t);
    t = e;
    ++j;
  }
  return s;
}

// Raised cosine 0..1 at m of kWtSeamPts (m = kWtSeamPts - 1 gives 1).
float frameRms(const float* f) {
  double m = 0, s = 0;
  for (int i = 0; i < N; ++i) m += f[i];
  m /= N;
  for (int i = 0; i < N; ++i) s += (f[i] - m) * (f[i] - m);
  return static_cast<float>(sqrt(s / N));
}

inline float seamW(int m) { return 0.5f - 0.5f * cosf(kPiF * (m + 1) / kWtSeamPts); }

}  // namespace

const char* wtMakeErrText(WtMakeErr e) {
  switch (e) {
    case WtMakeErr::Ok: return "OK";
    case WtMakeErr::TooShort: return "REGION TOO SHORT";
    case WtMakeErr::NoPitch: return "NO PITCH (TRY WINDOW)";
    case WtMakeErr::Silent: return "SILENT";
    case WtMakeErr::NoMemory: return "NO MEMORY";
    default: return "ERROR";
  }
}

void wtResample(const float* x, int n, double start, double len, float* out) {
  const double s = len / N;
  if (len > N) {
    for (int i = 0; i < N; ++i) {
      const double a = start + (i - 0.5) * s;
      out[i] = static_cast<float>(area(x, n, a, a + s) / s);
    }
    return;
  }
  for (int i = 0; i < N; ++i) {
    const double t = start + i * s;
    const double fl = floor(t);
    const int64_t j = static_cast<int64_t>(fl);
    out[i] = catmull(x[wrapIdx(j - 1, n)], x[wrapIdx(j, n)], x[wrapIdx(j + 1, n)], x[wrapIdx(j + 2, n)],
                     static_cast<float>(t - fl));
  }
}

void wtShape(WtShape sh, float* out) {
  for (int i = 0; i < N; ++i) {
    const float ph = static_cast<float>(i) / N;
    float v = 0;
    switch (sh) {
      case WtShape::Sin: v = sinf(2 * kPiF * ph); break;
      case WtShape::Tri: v = ph < 0.25f ? 4 * ph : (ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4); break;
      case WtShape::Saw: v = ph < 0.5f ? 2 * ph : 2 * ph - 2; break;
      default: v = ph < 0.5f ? 1.0f : -1.0f; break;
    }
    out[i] = v;
  }
  if (sh == WtShape::Sin) {  // exact zeros / peaks
    out[0] = out[N / 2] = 0;
    out[N / 4] = 1;
    out[3 * N / 4] = -1;
  }
}

void wtSmooth(float* f) {
  float prev = f[N - 1];
  const float first = f[0];
  for (int i = 0; i < N; ++i) {
    const float next = i + 1 < N ? f[i + 1] : first;
    const float cur = f[i];
    f[i] = 0.25f * prev + 0.5f * cur + 0.25f * next;
    prev = cur;
  }
}

void wtSortKeys(WtKey* keys, int n) {
  for (int i = 1; i < n; ++i)  // insertion sort by swapping (few keys)
    for (int j = i; j > 0 && keys[j - 1].frame > keys[j].frame; --j) {
      WtKey t = keys[j];
      keys[j] = keys[j - 1];
      keys[j - 1] = t;
    }
}

void wtKeyAt(const WtKey* keys, int n, int f, float* out) {
  if (n <= 0) {
    memset(out, 0, sizeof(float) * N);
    return;
  }
  int b = 0;
  while (b < n && keys[b].frame < f) ++b;
  if (b == n || keys[b].frame == f || b == 0) {
    memcpy(out, keys[b == n ? n - 1 : b].pts, sizeof(float) * N);
    return;
  }
  const WtKey& A = keys[b - 1];
  const WtKey& B = keys[b];
  const float t = static_cast<float>(f - A.frame) / (B.frame - A.frame);
  for (int i = 0; i < N; ++i) out[i] = (1 - t) * A.pts[i] + t * B.pts[i];
}

bool wtFromKeys(WtKey* keys, int n, int16_t* src) {
  if (n < 2) return false;
  wtSortKeys(keys, n);
  if (keys[0].frame != 0 || keys[n - 1].frame != kWtFrames - 1) return false;
  float f[N];
  for (int k = 0; k < kWtFrames; ++k) {
    wtKeyAt(keys, n, k, f);
    for (int i = 0; i < N; ++i) {
      const float v = f[i] < -1 ? -1 : (f[i] > 1 ? 1 : f[i]);
      src[k * N + i] = static_cast<int16_t>(lrintf(v * kWtPeak));
    }
  }
  return true;
}

void wtDiff(const float* x, int w, int t0, int t1, float* d) {
  for (int t = t0; t < t1; ++t) {
    float s = 0;
    const float* y = x + t;
    for (int j = 0; j < w; ++j) {
      const float e = x[j] - y[j];
      s += e * e;
    }
    d[t] = s;
  }
}

void wtCumNorm(const float* d, float* dn, int tauMax) {
  dn[0] = 1;
  double sum = 0;
  for (int t = 1; t <= tauMax; ++t) {
    sum += d[t];
    dn[t] = sum > 0 ? static_cast<float>(d[t] * t / sum) : 1.0f;
  }
}

bool wtPickPeriod(const float* d, const float* dn, int tauMin, int tauMax, float& period, float& conf) {
  if (tauMin < 1) tauMin = 1;
  int best = -1;
  for (int t = tauMin; t <= tauMax; ++t)
    if (dn[t] < kYinThresh) {
      while (t + 1 <= tauMax && dn[t + 1] < dn[t]) ++t;
      best = t;
      break;
    }
  if (best < 0) {
    int m = tauMin;
    for (int t = tauMin + 1; t <= tauMax; ++t)
      if (dn[t] < dn[m]) m = t;
    if (dn[m] >= kYinAccept) return false;
    best = m;
  }
  float p = static_cast<float>(best);
  if (best > 1 && best < tauMax) {
    const float a = d[best - 1], b = d[best], c = d[best + 1];
    const float den = a - 2 * b + c;
    if (den > 0) {
      float sh = 0.5f * (a - c) / den;
      sh = sh < -0.5f ? -0.5f : (sh > 0.5f ? 0.5f : sh);
      p += sh;
    }
  }
  period = p;
  conf = 1 - dn[best];
  return true;
}

bool wtYin(const float* x, int w, int tauMin, int tauMax, float* d, float* dn, float& period, float& conf) {
  d[0] = 0;
  wtDiff(x, w, 1, tauMax + 1, d);
  wtCumNorm(d, dn, tauMax);
  return wtPickPeriod(d, dn, tauMin, tauMax, period, conf);
}

// ---------------------------------------------------------------- maker

void WtMaker::end() {
  free(out_);
  free(win_);
  free(d_);
  free(dn_);
  out_ = nullptr;
  win_ = d_ = dn_ = nullptr;
  st_ = St::Idle;
}

void WtMaker::fail(WtMakeErr e) {
  err_ = e;
  st_ = St::Done;
  free(win_);
  free(d_);
  free(dn_);
  win_ = d_ = dn_ = nullptr;
}

WtMakeErr WtMaker::begin(const WtMakeParams& p, uint32_t from, uint32_t to, uint32_t rate) {
  end();
  p_ = p;
  err_ = WtMakeErr::Ok;
  from_ = from;
  len_ = to > from ? to - from : 0;
  rate_ = rate ? rate : 44100;
  pitched_ = 0;
  framesOut_ = 0;
  k_ = 0;
  fill_ = 0;
  lag_ = 0;
  memset(found_, 0, sizeof found_);
  memset(scale_, 0, sizeof scale_);
  if (len_ < kWtMakeMinLen) {
    fail(WtMakeErr::TooShort);
    return err_;
  }
  out_ = static_cast<int16_t*>(malloc(sizeof(int16_t) * kWtSrcSamples));
  if (!out_) {
    fail(WtMakeErr::NoMemory);
    return err_;
  }
  if (p_.mode == WtMode::Window) {
    readAt_ = 0;
    cell_ = 0;
    cellW_ = static_cast<double>(len_) / kWtSrcSamples;
    cellEnd_ = cellW_;
    acc_ = 0;
    last_ = -1;
    st_ = St::Read;
    return err_;
  }
  tauMax_ = static_cast<int>(rate_ / kWtMinHz);
  const int byLen = static_cast<int>(len_ / 2.5);
  if (tauMax_ > byLen) tauMax_ = byLen;
  tauMin_ = static_cast<int>(rate_ / kWtMaxHz);
  if (tauMin_ < 2) tauMin_ = 2;
  wa_ = static_cast<int>(2.5f * tauMax_) + 2;
  if (static_cast<uint32_t>(wa_) > len_) wa_ = static_cast<int>(len_);
  win_ = static_cast<float*>(malloc(sizeof(float) * wa_));
  d_ = static_cast<float*>(malloc(sizeof(float) * (tauMax_ + 1)));
  dn_ = static_cast<float*>(malloc(sizeof(float) * (tauMax_ + 1)));
  if (!win_ || !d_ || !dn_) {
    fail(WtMakeErr::NoMemory);
    free(out_);
    out_ = nullptr;
    return err_;
  }
  st_ = St::Read1;
  return err_;
}

uint32_t WtMaker::winPos(int k) const {
  return from_ + static_cast<uint32_t>(static_cast<uint64_t>(k) * (len_ - wa_) / (kWtFrames - 1));
}

bool WtMaker::nextRead(uint32_t& at, uint32_t& n) const {
  if (st_ == St::Read1 || st_ == St::Read2) {
    at = winPos(k_) + fill_;
    const uint32_t left = static_cast<uint32_t>(wa_) - fill_;
    n = left < kWtMakeRead ? left : kWtMakeRead;
    return true;
  }
  if (st_ == St::Read) {
    at = from_ + readAt_;
    const uint32_t left = len_ - readAt_;
    n = left < kWtMakeRead ? left : kWtMakeRead;
    return true;
  }
  return false;
}

void WtMaker::prepWindow() {
  // The int16 samples sit at the start of win_ (as bytes): convert from the end backwards.
  const int16_t* s = reinterpret_cast<const int16_t*>(win_);
  double m = 0;
  for (int i = wa_ - 1; i >= 0; --i) {
    win_[i] = s[i];
    m += win_[i];
  }
  const float mean = static_cast<float>(m / wa_);
  for (int i = 0; i < wa_; ++i) win_[i] -= mean;
}

void WtMaker::feed(const int16_t* x, uint32_t got) {
  if (st_ == St::Read1 || st_ == St::Read2) {
    memcpy(reinterpret_cast<int16_t*>(win_) + fill_, x, got * sizeof(int16_t));
    fill_ += got;
    if (fill_ < static_cast<uint32_t>(wa_)) return;
    fill_ = 0;
    prepWindow();
    if (st_ == St::Read1) {
      st_ = St::Diff;
      lag_ = 1;
      d_[0] = 0;
    } else {
      extractCycle(k_);
      if (++k_ >= kWtFrames) st_ = St::Finish;
    }
    return;
  }
  if (st_ != St::Read) return;
  for (uint32_t i = 0; i < got; ++i) windowSample(x[i]);
  readAt_ += got;
  if (readAt_ < len_) return;
  // Flush: cubic cells past the last sample (clamped), the area cells rounding left over.
  if (cellW_ < 1.0) {
    while (cell_ < kWtSrcSamples) {
      const double t = cell_ * cellW_;
      const int64_t j = static_cast<int64_t>(floor(t));
      auto at = [&](int64_t q) -> float {
        if (q < 0) q = 0;
        if (q > last_) q = last_;
        return hist_[q & 3];
      };
      // Only the last 4 samples are kept: positions here are within them (t > last - 3).
      cell(catmull(at(j - 1), at(j), at(j + 1), at(j + 2), static_cast<float>(t - floor(t))));
    }
  } else {
    while (cell_ < kWtSrcSamples) {
      cell(static_cast<float>(acc_ / cellW_));
      acc_ = 0;
    }
  }
  st_ = St::Finish;
}

void WtMaker::windowSample(float v) {
  if (cellW_ >= 1.0) {  // area: sample i covers [i, i + 1)
    double t = static_cast<double>(last_ + 1);
    const double e = t + 1;
    ++last_;
    while (cell_ < kWtSrcSamples && e > cellEnd_) {
      acc_ += v * (cellEnd_ - t);
      t = cellEnd_;
      cell(static_cast<float>(acc_ / cellW_));
      acc_ = 0;
      cellEnd_ = (cell_ + 1) * cellW_;
    }
    acc_ += v * (e - t);
    return;
  }
  ++last_;
  hist_[last_ & 3] = static_cast<int16_t>(v);
  if (last_ == 0) hist_[3] = hist_[0];  // x[-1] = x[0]
  while (cell_ < kWtSrcSamples) {
    const double t = cell_ * cellW_;
    const int64_t j = static_cast<int64_t>(floor(t));
    if (j + 2 > last_) break;
    const float a = hist_[(j < 1 ? 0 : j - 1) & 3];
    cell(catmull(a, hist_[j & 3], hist_[(j + 1) & 3], hist_[(j + 2) & 3], static_cast<float>(t - floor(t))));
  }
}

void WtMaker::cell(float v) {
  frame_[cell_ % N] = v;
  ++cell_;
  if (cell_ % N) return;
  const int k = cell_ / N - 1;
  const float rawRms = frameRms(frame_);  // before the seam blends the previous frame in
  if (p_.seam) {
    float pre[kWtSeamPts], raw[kWtSeamPts];
    memcpy(raw, frame_ + N - kWtSeamPts, sizeof raw);
    for (int m = 0; m < kWtSeamPts; ++m) pre[m] = k > 0 ? tail_[m] : frame_[kWtSeamPts - m];
    for (int m = 0; m < kWtSeamPts; ++m) {
      const float w = seamW(m);
      frame_[N - kWtSeamPts + m] = (1 - w) * frame_[N - kWtSeamPts + m] + w * pre[m];
    }
    memcpy(tail_, raw, sizeof raw);
  }
  storeFrame(k, frame_);
  rms_[k] = rawRms;
}

void WtMaker::smoothPeriods() {
  float sm[kWtFrames];
  for (int k = 0; k < kWtFrames; ++k) {
    if (!found_[k]) continue;
    float v[5];
    int n = 0;
    for (int j = k - 2; j <= k + 2; ++j)
      if (j >= 0 && j < kWtFrames && found_[j]) v[n++] = period_[j];
    for (int i = 1; i < n; ++i)
      for (int j = i; j > 0 && v[j - 1] > v[j]; --j) {
        const float t = v[j];
        v[j] = v[j - 1];
        v[j - 1] = t;
      }
    sm[k] = v[n / 2];
  }
  for (int k = 0; k < kWtFrames; ++k) {
    if (found_[k]) continue;
    int a = k - 1, b = k + 1;
    while (a >= 0 && !found_[a]) --a;
    while (b < kWtFrames && !found_[b]) ++b;
    if (a < 0) sm[k] = sm[b];
    else if (b >= kWtFrames) sm[k] = sm[a];
    else sm[k] = sm[a] + (sm[b] - sm[a]) * (k - a) / static_cast<float>(b - a);
  }
  memcpy(period_, sm, sizeof sm);
}

void WtMaker::extractCycle(int k) {
  const float P = period_[k];
  const int mid = wa_ / 2;
  int lo = mid - static_cast<int>(ceilf(P));
  if (lo < 1) lo = 1;
  int best = -1;
  float bestSlope = 0;
  for (int i = lo; i < mid; ++i)
    if (win_[i] < 0 && win_[i + 1] >= 0) {
      const float sl = win_[i + 1] - win_[i];
      if (sl > bestSlope) {
        bestSlope = sl;
        best = i;
      }
    }
  const double z = best < 0 ? mid - P / 2 : best + (-win_[best]) / static_cast<double>(win_[best + 1] - win_[best]);
  float f[N];
  wtResample(win_, wa_, z, P, f);
  if (p_.seam) {
    auto lin = [&](double t) {
      int j = static_cast<int>(floor(t));
      if (j < 0) j = 0;
      if (j > wa_ - 2) j = wa_ - 2;
      const float fr = static_cast<float>(t - j);
      return win_[j] + fr * (win_[j + 1] - win_[j]);
    };
    const float delta = lin(z + P) - lin(z);
    for (int i = 0; i < N; ++i) f[i] -= delta * i / N;
  }
  storeFrame(k, f);
}

void WtMaker::storeFrame(int k, const float* f) {
  float pk = 0;
  for (int i = 0; i < N; ++i) pk = fabsf(f[i]) > pk ? fabsf(f[i]) : pk;
  rms_[k] = frameRms(f);
  peak_[k] = pk;
  scale_[k] = pk > 0 ? 32767.0f / pk : 0;
  int16_t* o = out_ + k * N;
  for (int i = 0; i < N; ++i) o[i] = static_cast<int16_t>(lrintf(f[i] * scale_[k]));
  framesOut_ = k + 1;
}

void WtMaker::finish() {
  float g[kWtFrames];
  int src[kWtFrames];
  float top = 0;
  bool any = false;
  for (int k = 0; k < kWtFrames; ++k) {
    src[k] = k;
    g[k] = 1;
    top = rms_[k] > top ? rms_[k] : top;
    any |= peak_[k] > 0;
  }
  if (p_.norm == WtNorm::Frame) {
    if (top < kSilentRms) {
      fail(WtMakeErr::Silent);
      return;
    }
    for (int k = 0; k < kWtFrames; ++k) {
      if (rms_[k] >= kSilentRms) continue;
      int j = -1;
      for (int a = k - 1; a >= 0 && j < 0; --a)
        if (rms_[a] >= kSilentRms) j = a;
      for (int b = k + 1; b < kWtFrames && j < 0; ++b)
        if (rms_[b] >= kSilentRms) j = b;
      src[k] = j;
    }
    for (int k = 0; k < kWtFrames; ++k) {
      const float r = rms_[src[k]];
      g[k] = top / r < kMaxGain ? top / r : kMaxGain;
    }
  } else if (!any) {
    fail(WtMakeErr::Silent);
    return;
  }
  float mx = 0;
  for (int k = 0; k < kWtFrames; ++k) {
    const float a = peak_[src[k]] * g[k];
    mx = a > mx ? a : mx;
  }
  const float S = kWtMakePeak / mx;
  // In place: a copied frame reads its source, so sources (lower or higher) must be scaled last.
  // Frames are copied from loud ones only, which are never copies: first the copies, then the rest.
  for (int pass = 0; pass < 2; ++pass)
    for (int k = 0; k < kWtFrames; ++k) {
      if ((src[k] != k) != (pass == 0)) continue;
      const int j = src[k];
      const float c = scale_[j] > 0 ? g[k] * S / scale_[j] : 0;
      const int16_t* in = out_ + j * N;
      int16_t* o = out_ + k * N;
      for (int i = 0; i < N; ++i) {
        float v = in[i] * c;
        v = v > 32767 ? 32767 : (v < -32767 ? -32767 : v);
        o[i] = static_cast<int16_t>(lrintf(v));
      }
    }
  st_ = St::Done;
  free(win_);
  free(d_);
  free(dn_);
  win_ = d_ = dn_ = nullptr;
}

bool WtMaker::step() {
  switch (st_) {
    case St::Diff: {
      const int w = wa_ - tauMax_ - 1;
      const int t1 = lag_ + kLagChunk < tauMax_ + 1 ? lag_ + kLagChunk : tauMax_ + 1;
      wtDiff(win_, w, lag_, t1, d_);
      lag_ = t1;
      if (lag_ <= tauMax_) return true;
      wtCumNorm(d_, dn_, tauMax_);
      float per = 0, conf = 0;
      found_[k_] = wtPickPeriod(d_, dn_, tauMin_, tauMax_, per, conf);
      period_[k_] = per;
      pitched_ += found_[k_];
      if (++k_ < kWtFrames) {
        st_ = St::Read1;
        return true;
      }
      if (pitched_ < kWtMinPitched) {
        fail(WtMakeErr::NoPitch);
        return true;
      }
      smoothPeriods();
      k_ = 0;
      st_ = St::Read2;
      return true;
    }
    case St::Finish:
      if (p_.mode == WtMode::Window) pitched_ = kWtFrames;
      finish();
      return true;
    default: return false;
  }
}

int WtMaker::progress() const {
  if (st_ == St::Done) return kWtFrames;
  if (p_.mode == WtMode::Window) return framesOut_ < kWtFrames ? framesOut_ : kWtFrames - 1;
  if (st_ == St::Read1 || st_ == St::Diff) return k_ / 2;
  if (st_ == St::Read2) return kWtFrames / 2 + k_ / 2;
  return kWtFrames - 1;
}

WtMakeErr wtMakeArray(const int16_t* x, uint32_t frames, uint32_t from, uint32_t to, uint32_t rate,
                      const WtMakeParams& p, int16_t* out, int* pitched, uint32_t piece) {
  if (to > frames) to = frames;
  WtMaker m;
  m.begin(p, from, to, rate);
  while (!m.done()) {
    uint32_t at, n;
    if (m.nextRead(at, n)) m.feed(x + at, n < piece ? n : piece);
    else if (!m.step()) break;
  }
  if (pitched) *pitched = m.pitched();
  if (m.error() != WtMakeErr::Ok) return m.error();
  memcpy(out, m.result(), sizeof(int16_t) * kWtSrcSamples);
  return WtMakeErr::Ok;
}

// ---------------------------------------------------------------- WAV

bool wtWavWrite(const int16_t* src, ByteSink& out) {
  uint8_t h[kWavHeaderBytes];
  wavHeader(h, kWtSrcSamples, 44100, 60, sampleCrc(src, kWtSrcSamples));
  const uint32_t riff = kWtWavBytes - 8;
  h[4] = static_cast<uint8_t>(riff);
  h[5] = static_cast<uint8_t>(riff >> 8);
  h[6] = static_cast<uint8_t>(riff >> 16);
  h[7] = static_cast<uint8_t>(riff >> 24);
  uint8_t clm[kWtClmBytes] = {'c', 'l', 'm', ' ', kWtClmBytes - 8, 0, 0, 0};
  const char txt[] = "<!>256 10000000 wavetable D-TRK";
  static_assert(sizeof txt <= kWtClmBytes - 8, "clm text");
  memcpy(clm + 8, txt, sizeof txt);
  return out.write(h, kWavHeaderBytes - 8) && out.write(clm, sizeof clm) &&
         out.write(h + kWavHeaderBytes - 8, 8) && out.write(src, sizeof(int16_t) * kWtSrcSamples);
}

}  // namespace mt
