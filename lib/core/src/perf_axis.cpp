#include "perf_axis.h"

namespace mt {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
bool bipolar(const PerfRange& r) { return r.lo < 0 && r.hi > 0; }
void deadZone(int len, int& from, int& to) {
  const int mid = len / 2, dz = len / 16;
  from = mid - dz;
  to = mid + dz;
}
}  // namespace

int perfAxisDepth(PerfFx f, int pos, int len) {
  const PerfRange r = perfRange(f);
  if (len <= 0 || r.lo == r.hi) return r.lo;
  pos = clampi(pos, 0, len - 1);
  if (bipolar(r)) {
    int from, to;
    deadZone(len, from, to);
    if (pos < from) return clampi(r.lo + static_cast<int>(static_cast<int64_t>(pos) * -r.lo / from), r.lo, -1);
    if (pos >= to) return clampi(1 + static_cast<int>(static_cast<int64_t>(pos - to) * r.hi / (len - to)), 1, r.hi);
    return 0;
  }
  return perfDepthAt(f, static_cast<int>(static_cast<int64_t>(pos) * perfDepthCount(f) / len));
}

bool perfAxisZoned(PerfFx f) {
  const PerfRange r = perfRange(f);
  return f != PerfFx::None && f < PerfFx::Count && !r.coarse;
}

int perfAxisEdge(PerfFx f, int i, int len) {
  const int n = perfDepthCount(f);
  if (len <= 0) return 0;
  i = clampi(i, 0, n);
  return static_cast<int>((static_cast<int64_t>(i) * len + n - 1) / n);  // the first pos with lo + pos * n / len >= lo + i
}

bool perfAxisDead(PerfFx f, int len, int& from, int& to) {
  if (f == PerfFx::None || f >= PerfFx::Count || !bipolar(perfRange(f))) return false;
  deadZone(len, from, to);
  return true;
}

PerfFx perfXyAxis(const uint8_t xy[2], int a) {
  auto fx = [xy](int i) {
    return xy[i] < static_cast<uint8_t>(PerfFx::Count) ? static_cast<PerfFx>(xy[i]) : PerfFx::None;
  };
  const PerfFx f = fx(a & 1), x = fx(0);
  if ((a & 1) == 1 && (f == x || (perfBuffered(f) && perfBuffered(x)))) return PerfFx::None;
  return f;
}

void perfTrailRow(const PerfTrail& t, int y, uint16_t* dst, int w, uint16_t color) {
  for (int i = 0; i < t.n; ++i) {
    const int r = t.r[i], dy = y - t.y[i];
    if (dy < -r || dy > r) continue;
    int hw = 0;  // half width of the disc on this row
    while ((hw + 1) * (hw + 1) + dy * dy <= r * r) ++hw;
    const int x0 = clampi(t.x[i] - hw, 0, w), x1 = clampi(t.x[i] + hw + 1, 0, w);
    for (int x = x0; x < x1; ++x) dst[x] = color;
  }
}

}  // namespace mt
