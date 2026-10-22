#include "xy_pad.h"
#include <stdio.h>
#include <stdlib.h>
#include "perf_pad.h"
#include "theme.h"

namespace ui {

void XyPad::touch(int x, int y, uint32_t now) {
  down_ = true;
  x_ = static_cast<int16_t>(x);
  y_ = static_cast<int16_t>(y);
  if (n_ > 0) {
    const Pt& last = pts_[(head_ + kPts - 1) % kPts];
    if (abs(x - last.x) + abs(y - last.y) < 6 && now - last.ms < 30) return;  // dense enough
  }
  pts_[head_] = Pt{x_, y_, now};
  head_ = (head_ + 1) % kPts;
  if (n_ < kPts) ++n_;
}

int XyPad::radius(const Pt& p, uint32_t now) const {
  const uint32_t age = now - p.ms;
  if (age >= kTrailMs) return 0;
  return 2 + static_cast<int>((kTrailR - 2) * (kTrailMs - age) / kTrailMs);
}

bool XyPad::animating(uint32_t now) const {
  for (int i = 0; i < n_; ++i)
    if (radius(pts_[i], now) > 0) return true;
  return false;
}

void XyPad::trail(mt::PerfTrail& out, int y0, uint32_t now) const {
  out.n = 0;
  for (int i = 0; i < n_; ++i) out.add(pts_[i].x, pts_[i].y + y0, radius(pts_[i], now));
  if (down_) out.add(x_, y_ + y0, kTrailR);
}

void XyPad::draw(LGFX_Sprite& s, int y0, int h, const PerfPad& perf, uint32_t now, bool trail) const {
  const int w = kScreenW;
  s.fillRect(0, y0, w, h, kBg);
  const mt::PerfFx fx = perf.xyAxis(0), fy = perf.xyAxis(1);
  int a, b;
  if (mt::perfAxisDead(fx, w, a, b)) s.fillRect(a, y0, b - a, h, kBeatBg);
  if (mt::perfAxisDead(fy, h, a, b)) s.fillRect(0, y0 + h - b, w, b - a, kBeatBg);  // pos 0 at the bottom
  if (mt::perfAxisZoned(fx)) {
    const mt::PerfRange r = mt::perfRange(fx);
    for (int i = 1; i <= r.hi - r.lo; ++i) s.drawFastVLine(mt::perfAxisEdge(fx, i, w), y0, h, kBeatBg);
  }
  if (mt::perfAxisZoned(fy)) {
    const mt::PerfRange r = mt::perfRange(fy);
    for (int i = 1; i <= r.hi - r.lo; ++i) s.drawFastHLine(0, y0 + h - mt::perfAxisEdge(fy, i, h), w, kBeatBg);
  }
  if (down_) {
    s.drawFastVLine(x_, y0, h, kDim);
    s.drawFastHLine(0, y0 + y_, w, kDim);
  }
  if (trail) {
    for (int i = 0; i < n_; ++i)
      if (const int r = radius(pts_[i], now)) s.fillCircle(pts_[i].x, y0 + pts_[i].y, r, kYellow);
    if (down_) s.fillCircle(x_, y0 + y_, kTrailR, kYellow);
  }
  // Text last: over the zones and the trail.
  perf.drawBadge(s, 4, y0 + 4);  // PERF + the held effects
  s.setTextColor(kDim);
  s.drawString("XY  B: EXIT", w - 11 * kCharW - 4, y0 + 4);
  auto label = [&](const char* axis, mt::PerfFx f, int y) {
    char txt[40], val[12];
    const bool held = f != mt::PerfFx::None && perf.stack().held(f);
    if (f == mt::PerfFx::None) snprintf(txt, sizeof(txt), "%s ---", axis);
    else {
      if (held) mt::perfValue(f, perf.stack().depth(f), val, sizeof(val));
      else snprintf(val, sizeof(val), "--");
      snprintf(txt, sizeof(txt), "%s %s %s", axis, mt::perfName(f), val);
    }
    s.setTextColor(held ? kYellow : kDim);
    s.drawString(txt, 4, y);
  };
  label("Y", fy, y0 + 4 + kCharH + 4);
  label("X", fx, y0 + h - kCharH - 4);
}

}  // namespace ui
