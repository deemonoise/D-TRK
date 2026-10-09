#include "sample_tools.h"
#include "bank.h"
#include "bank_ops.h"
#include "onset.h"
#include "wave_peaks.h"

using namespace mt::link;
using mt::BankResult;

namespace tools {

namespace {

// Onsets of the last sample asked for: the ESP fetches them in pieces (and retries).
mt::Onset onsets[mt::kMaxOnsets];
int nOnsets = -1;  // -1 = none computed
const int16_t* onsData = nullptr;
uint32_t onsFrames = 0, onsGen = 0;

template <class M>
int put(const M& m, uint8_t* p) {
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? w.size() : 0;
}

uint8_t res(BankResult r) { return static_cast<uint8_t>(r); }

int peaks(Reader& r, uint8_t* p) {
  static WavePeaksRep m;
  m = WavePeaksRep();
  WavePeaksReq q;
  if (!decode(r, q) || q.cols > WavePeaksRep::kMaxCols || !q.width) {
    m.result = res(BankResult::ReadFail);
    return put(m, p);
  }
  uint32_t frames, rate, gen;
  const int16_t* d = bank::sampleData(q.index, frames, rate, gen);
  if (!d) {
    m.result = res(BankResult::OpenFail);
    return put(m, p);
  }
  m.cols = q.cols;
  mt::wavePeaks(d, frames, q.col0, q.span, q.width, q.cols, m.mn, m.mx);
  return put(m, p);
}

int onsetList(Reader& r, uint8_t* p) {
  static OnsetsRep m;
  m = OnsetsRep();
  OnsetsReq q;
  if (!decode(r, q)) {
    m.result = res(BankResult::ReadFail);
    return put(m, p);
  }
  uint32_t frames, rate, gen;
  const int16_t* d = bank::sampleData(q.index, frames, rate, gen);
  if (!d) {
    m.result = res(BankResult::OpenFail);
    return put(m, p);
  }
  if (nOnsets < 0 || d != onsData || frames != onsFrames || gen != onsGen) {
    nOnsets = mt::detectOnsets(d, frames, rate, onsets, mt::kMaxOnsets);
    onsData = d;
    onsFrames = frames;
    onsGen = gen;
  }
  m.total = static_cast<uint8_t>(nOnsets);
  for (int i = q.skip; i < nOnsets && m.n < OnsetsRep::kMax; ++i, ++m.n) {
    m.pos[m.n] = onsets[i].pos;
    m.strength[m.n] = onsets[i].strength;
  }
  return put(m, p);
}

}  // namespace

bool isRequest(Msg t) { return t == Msg::WavePeaks || t == Msg::Onsets; }

int handle(Msg t, const uint8_t* p, int n, uint8_t* out) {
  Reader r(p, n);
  return t == Msg::WavePeaks ? peaks(r, out) : onsetList(r, out);
}

}  // namespace tools
