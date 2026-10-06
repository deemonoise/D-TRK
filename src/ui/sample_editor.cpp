#include "sample_editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "engine/engine.h"
#include "note_name.h"
#include "sample_set.h"
#include "slices.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Region fractions (/0xFFFF) are edited in 0.1 % steps.
int toPermille(uint16_t v) { return (v * 1000 + 0x7FFF) / 0xFFFF; }
uint16_t fromPermille(int p) { return static_cast<uint16_t>((p * 0xFFFF + 500) / 1000); }
void permille(uint16_t v, char* o, int n) {
  const int p = toPermille(v);
  snprintf(o, n, "%d.%d%%", p / 10, p % 10);
}

}  // namespace

SampleEditor::SampleEditor(App& app, int y) : app_(app), list_(y + kWaveH + kToolH), y_(y) {
  rows_[kMarkerRow] = {"Marker", [this](char* o, int n) {
                         char p[12];
                         permille(markerFrac(sel_), p, sizeof(p));
                         if (sel_ >= kSlice0) snprintf(o, n, "#%d %s", sel_ - kSlice0 + 1, p);
                         else snprintf(o, n, "%c %s", "SEL"[sel_], p);
                       },
                       {}};
  rows_[kSampleRow] = {"Sample",
                       [this](char* o, int n) { snprintf(o, n, "%s", inst().sample[0] ? inst().sample : "---"); },
                       [this](int d) {
                         // Project list order, "---" (none) before the first; picking one takes the root and
                         // loop of its cached data. Another sample drops the slices.
                         const mt::Project& p = app_.project();
                         const int cur = mt::projSampleFind(p, inst().sample);
                         const int i = clampi(cur + d, -1, p.sampleCount - 1);
                         if (i == cur) return;
                         mt::sliceClear(inst());
                         if (i < 0) {
                           inst().sample[0] = 0;
                           return;
                         }
                         snprintf(inst().sample, sizeof(inst().sample), "%s", p.samples[i].name);
                         const int j = audio::bankMounted() ? mt::projSampleBank(p, audio::bank(), i) : -1;
                         if (const mt::BankEntry* e = j >= 0 ? audio::bank().entry(j) : nullptr) {
                           inst().root = e->root > 127 ? 127 : e->root;
                           inst().loop = e->loop < static_cast<uint8_t>(mt::LoopMode::Count) ? e->loop : 0;
                         }
                       },
                       {}, [this] { return sampleMissing(); }};
  rows_[kRoot] = {"Root", [this](char* o, int n) {
                    char nn[4];
                    mt::noteName(inst().root, nn);
                    snprintf(o, n, "%s", nn);
                  },
                  [this](int d) { inst().root = static_cast<uint8_t>(clampi(inst().root + d, 0, 127)); }};
  rows_[kStart] = {"Start", [this](char* o, int n) { permille(inst().start, o, n); },
                   [this](int d) {
                     inst().start = fromPermille(clampi(toPermille(inst().start) + d, 0, toPermille(inst().end)));
                   }};
  rows_[kEnd] = {"End", [this](char* o, int n) { permille(inst().end, o, n); },
                 [this](int d) {
                   inst().end = fromPermille(clampi(toPermille(inst().end) + d, toPermille(inst().start), 1000));
                 }};
  rows_[kLoop] = {"Loop", [this](char* o, int n) {
                    static const char* const kNames[] = {"OFF", "FWD", "PING"};
                    snprintf(o, n, "%s", kNames[inst().loop < 3 ? inst().loop : 0]);
                  },
                  [this](int d) {
                    inst().loop = static_cast<uint8_t>(
                        clampi(inst().loop + d, 0, static_cast<int>(mt::LoopMode::Count) - 1));
                  }};
  auto noLoop = [this] { return inst().loop == static_cast<uint8_t>(mt::LoopMode::Off); };
  rows_[kLoopStart] = {"Loop start", [this](char* o, int n) { permille(inst().loopStart, o, n); },
                       [this](int d) {
                         inst().loopStart = fromPermille(clampi(toPermille(inst().loopStart) + d, 0, 1000));
                       },
                       noLoop};
  rows_[kReverse] = {"Reverse", [this](char* o, int n) { snprintf(o, n, "%s", inst().reverse ? "ON" : "OFF"); },
                     [this](int d) { inst().reverse = d > 0; }};
  rows_[kSlices] = {"Slices", [this](char* o, int n) {
                      static const char* const kNames[] = {"OFF", "NOTE", "FX"};
                      snprintf(o, n, "%s", kNames[inst().sliceMode < 3 ? inst().sliceMode : 0]);
                    },
                    [this](int d) {
                      inst().sliceMode = static_cast<uint8_t>(
                          clampi(inst().sliceMode + d, 0, static_cast<int>(mt::SliceMode::Count) - 1));
                    }};
  rows_[kChop] = {"Chop", [this](char* o, int n) { snprintf(o, n, "%s", inst().chopMode ? "TRANS" : "EQUAL"); },
                  [this](int d) { inst().chopMode = d > 0; }};
  // Label set in draw(): "Chop N" (EQUAL) or "Sens" (TRANS).
  rows_[kAmount] = {"Chop N",
                    [this](char* o, int n) {
                      if (inst().chopMode) snprintf(o, n, "%u%%", inst().chopThresh);
                      else snprintf(o, n, "%u", inst().chopN);
                    },
                    [this](int d) {
                      mt::Instrument& m = inst();
                      if (m.chopMode) m.chopThresh = static_cast<uint8_t>(clampi(m.chopThresh + d, 0, 100));
                      else m.chopN = static_cast<uint8_t>(clampi(m.chopN + d, 2, mt::kMaxSlices));
                    }};
  list_.setParams(rows_, kRows);
  list_.setVisibleRows(kListRows);
  list_.setWrap(false);
  list_.setOnEdit([this] { app_.markDirty(); });
}

mt::Instrument& SampleEditor::inst() { return app_.project().instruments[instr_ < 0 ? 0 : instr_]; }

int SampleEditor::bankIndex() {
  if (!audio::bankMounted() || !inst().sample[0]) return -1;
  const mt::Project& p = app_.project();
  return mt::projSampleBank(p, audio::bank(), mt::projSampleFind(p, inst().sample));
}

bool SampleEditor::sampleMissing() { return inst().sample[0] && bankIndex() < 0; }

void SampleEditor::sync() {
  const int i = bankIndex();
  const mt::BankEntry* e = i >= 0 ? audio::bank().entry(i) : nullptr;
  const int16_t* d = e ? audio::bank().data(i) : nullptr;
  const uint32_t frames = d ? e->frames : 0;
  const uint32_t gen = audio::bank().generation();
  gen_ = gen;  // a new bank generation only invalidates the wave / onset caches
  if (d != data_ || frames != frames_) {
    data_ = d;
    frames_ = frames;
    rate_ = d ? e->rate : 0;
    zoom_ = 0;
    viewCol_ = 0;
    sel_ = kS;
  }
  if (!visible(sel_)) sel_ = kS;
}

void SampleEditor::bind(int instr) {
  if (instr != instr_) {
    instr_ = instr;
    zoom_ = 0;
    viewCol_ = 0;
    sel_ = kS;
  }
  sync();
}

void SampleEditor::enter(bool last) {
  sync();
  list_.setEdit(false);
  list_.setSel(last ? kRows - 1 : 0);
}

// --- markers ---

bool SampleEditor::visible(int id) {
  if (id == kS || id == kE) return true;
  if (id == kL) return inst().loop != static_cast<uint8_t>(mt::LoopMode::Off);
  return id >= kSlice0 && id - kSlice0 < inst().sliceCount;
}

void SampleEditor::region(uint32_t& from, uint32_t& to) {
  const uint32_t len = frames_;
  from = mt::fracToFrame(inst().start, len);
  to = mt::fracToFrame(inst().end, len);
  if (from >= len) from = len ? len - 1 : 0;
  if (to > len) to = len;
  if (to <= from) to = from + 1;
}

uint32_t SampleEditor::markerFrame(int id) {
  const mt::Instrument& m = inst();
  if (id == kS) return mt::fracToFrame(m.start, frames_);
  if (id == kE) return mt::fracToFrame(m.end, frames_);
  if (id == kL) {
    // Loop start is a fraction of Start..End; reverse mirrors it (see Synth::startSample).
    uint32_t from, to;
    region(from, to);
    uint32_t lf = from + static_cast<uint32_t>(static_cast<uint64_t>(m.loopStart) * (to - from) / 0xFFFF);
    if (lf >= to) lf = to - 1;
    return m.reverse ? to - (lf - from) : lf;
  }
  return mt::fracToFrame(m.slices[id - kSlice0], frames_);
}

uint16_t SampleEditor::markerFrac(int id) {
  const mt::Instrument& m = inst();
  if (id == kS) return m.start;
  if (id == kE) return m.end;
  if (id == kL) return frames_ ? mt::frameToFrac(markerFrame(kL), frames_) : m.loopStart;
  return id - kSlice0 < m.sliceCount ? m.slices[id - kSlice0] : 0;
}

void SampleEditor::setMarkerFrame(int id, uint32_t f, int dir) {
  const uint32_t len = frames_;
  if (!len || !visible(id)) return;
  if (f > len) f = len;
  mt::Instrument& m = inst();
  engine::lockProject();
  if (id == kS || id == kE) {
    uint16_t& v = id == kS ? m.start : m.end;
    int p = mt::frameToFrac(f, len);
    if (dir && p == v) p += dir;
    v = static_cast<uint16_t>(id == kS ? clampi(p, 0, m.end) : clampi(p, m.start, 0xFFFF));
  } else if (id == kL) {
    uint32_t from, to;
    region(from, to);
    if (f < from) f = from;
    if (f > to) f = to;
    if (m.reverse) {
      f = from + (to - f);
      dir = -dir;
    }
    const uint32_t span = to - from;
    int p = static_cast<int>((static_cast<uint64_t>(f - from) * 0xFFFF + span / 2) / span);
    if (dir && p == m.loopStart) p += dir;
    m.loopStart = static_cast<uint16_t>(clampi(p, 0, 0xFFFF));
  } else {
    const int i = id - kSlice0;
    int p = mt::frameToFrac(f, len);
    if (dir && p == m.slices[i]) p += dir;
    mt::sliceMove(m, i, p);
  }
  engine::unlockProject();
  app_.markDirty();
}

int SampleEditor::stepMarker(int dir) {
  const uint32_t cur = markerFrame(sel_);
  int best = -1;
  uint32_t bf = 0;
  for (int id = 0; id < kMarkers; ++id) {
    if (id == sel_ || !visible(id)) continue;
    const uint32_t f = markerFrame(id);
    // Frame order, ties by id.
    const bool after = f > cur || (f == cur && id > sel_);
    if (after != (dir > 0)) continue;
    const bool better = best < 0 || (dir > 0 ? (f < bf || (f == bf && id < best)) : (f > bf || (f == bf && id > best)));
    if (better) best = id, bf = f;
  }
  return best < 0 ? sel_ : best;
}

int SampleEditor::nearestMarker(int x) {
  if (!frames_) return -1;
  int best = -1, bd = kGrab + 1;
  for (int id = 0; id < kMarkers; ++id) {
    if (!visible(id)) continue;
    int d = xOf(markerFrame(id)) - x;
    d = d < 0 ? -d : d;
    if (d < bd || (d == bd && id == sel_)) best = id, bd = d;
  }
  return best;
}

void SampleEditor::selectMarker(int id) {
  sel_ = id;
  if (list_.sel() != kMarkerRow) {
    list_.setEdit(false);
    list_.setSel(kMarkerRow);
  }
  if (frames_) showFrame(markerFrame(id));
}

// --- view ---

// The deepest zoom shows kScreenW frames (1 frame per column) once the sample is that long.
int SampleEditor::zMax() const {
  int z = 0;
  while (z < 31 && (frames_ >> z) > static_cast<uint32_t>(kScreenW)) ++z;
  return z;
}

uint32_t SampleEditor::viewLen() const {
  const uint32_t l = frames_ >> zoom_;
  return l > static_cast<uint32_t>(kScreenW) ? l : kScreenW;
}

// Column grid of the zoom: grid column g starts at frame g * viewLen / kScreenW. The view starts on a
// grid column, so screen column c always covers the same frames whatever the scroll position, and
// scrolling by whole columns can reuse the cached ones.
uint32_t SampleEditor::gridFrame(uint32_t g) const {
  return static_cast<uint32_t>(static_cast<uint64_t>(g) * viewLen() / kScreenW);
}

uint32_t SampleEditor::maxCol() const {
  const uint32_t vl = viewLen();
  if (frames_ <= vl) return 0;
  // Last g with gridFrame(g) <= frames_ - vl.
  return static_cast<uint32_t>((static_cast<uint64_t>(frames_ - vl + 1) * kScreenW - 1) / vl);
}

void SampleEditor::setCol(int64_t g) {
  const int64_t hi = maxCol();
  viewCol_ = static_cast<uint32_t>(g < 0 ? 0 : (g > hi ? hi : g));
}

void SampleEditor::setView(int64_t start) {
  setCol(start < 0 ? 0 : start * kScreenW / static_cast<int64_t>(viewLen()));
}

void SampleEditor::setZoom(int z) {
  zoom_ = clampi(z, 0, zMax());
  setView(static_cast<int64_t>(markerFrame(sel_)) - viewLen() / 2);
}

void SampleEditor::showFrame(uint32_t f) {
  const int x = xOf(f);
  if (x < 0 || x >= kScreenW) setView(static_cast<int64_t>(f) - viewLen() / 2);
}

uint32_t SampleEditor::frameAt(int x) const {
  const uint32_t f = gridFrame(viewCol_ + clampi(x, 0, kScreenW));
  return f < frames_ ? f : frames_;
}

int SampleEditor::xOf(uint32_t f) const {
  // Grid column holding f: the last g with gridFrame(g) <= f.
  const uint64_t g = ((static_cast<uint64_t>(f) + 1) * kScreenW - 1) / viewLen();
  return static_cast<int>(static_cast<int64_t>(g) - viewCol_);
}

uint32_t SampleEditor::snap(uint32_t f) {
  ensureOnsets();
  const int i = mt::nearestOnset(onsets_, nOnsets_, f);
  return i >= 0 ? onsets_[i].pos : f;
}

void SampleEditor::ensureOnsets() {
  if (data_ == onsData_ && frames_ == onsFrames_ && gen_ == onsGen_) return;
  onsData_ = data_;
  onsFrames_ = frames_;
  onsGen_ = gen_;
  nOnsets_ = data_ ? mt::detectOnsets(data_, frames_, rate_, onsets_, mt::kMaxOnsets) : 0;
}

// --- input ---

uint8_t SampleEditor::previewNote() {
  const mt::Instrument& m = inst();
  if (m.sliceMode == static_cast<uint8_t>(mt::SliceMode::Note) && sel_ >= kSlice0 && sel_ - kSlice0 < m.sliceCount)
    return static_cast<uint8_t>(clampi(m.root + sel_ - kSlice0, 0, 127));
  return 60;
}

void SampleEditor::playSlice(int k) {
  sync();
  const mt::Instrument& m = inst();
  const int cnt = m.sliceCount < mt::kMaxSlices ? m.sliceCount : mt::kMaxSlices;
  if (!cnt) {
    app_.toast("NO SLICES");
    return;
  }
  const int base = sel_ >= kSlice0 && sel_ - kSlice0 < cnt ? sel_ - kSlice0 : 0;
  const int i = base + k;
  char msg[16];
  if (i >= cnt) {
    snprintf(msg, sizeof(msg), "NO SLICE %d", i + 1);
    app_.toast(msg);
    return;
  }
  if (!frames_ || !rate_) return;
  uint32_t from, to;
  if (!mt::sliceRegion(m, i, frames_, from, to)) return;
  // Played at the root: only transpose / fine change its length.
  const float tune = m.transpose + m.fine * 0.01f;
  const float ms = (to - from) * 1000.f / rate_ * exp2f(-tune / 12.f);
  audio::previewSlice(static_cast<uint8_t>(instr_), static_cast<uint8_t>(i), m.root, static_cast<uint32_t>(ms) + 50);
  snprintf(msg, sizeof(msg), "SLICE %d", i + 1);
  app_.toast(msg);
}

int SampleEditor::onInput(const hw::InputEvent& ev) {
  sync();
  if (list_.sel() == kMarkerRow && list_.editing() && ev.type == hw::InputType::EncTurn) {
    if (!frames_ || !ev.delta) return 0;
    const int dir = ev.delta < 0 ? -1 : 1;
    uint32_t f = markerFrame(sel_);
    if (ev.shift) {
      ensureOnsets();
      for (int k = 0; k < (ev.delta < 0 ? -ev.delta : ev.delta); ++k) {
        const int i = mt::stepOnset(onsets_, nOnsets_, f, dir);
        if (i < 0) break;
        f = onsets_[i].pos;
      }
      if (f == markerFrame(sel_)) return 0;  // no onset that way
      setMarkerFrame(sel_, f);
    } else {
      const uint32_t px = viewLen() / kScreenW;
      const int64_t t = static_cast<int64_t>(f) + static_cast<int64_t>(ev.delta) * (px > 1 ? px : 1);
      setMarkerFrame(sel_, static_cast<uint32_t>(t < 0 ? 0 : (t > frames_ ? frames_ : t)), dir);
    }
    showFrame(markerFrame(sel_));
    return 0;
  }
  return list_.onInput(ev);
}

void SampleEditor::tool(int t, bool longPress) {
  mt::Instrument& m = inst();
  if (longPress) {
    if (t != kClr || !m.sliceCount) return;
    engine::lockProject();
    mt::sliceClear(m);
    engine::unlockProject();
    app_.markDirty();
    selectMarker(kS);
    return;
  }
  switch (t) {
    case kPrev:
    case kNext: selectMarker(stepMarker(t == kPrev ? -1 : 1)); break;
    case kZoomOut:
    case kZoomIn:
      if (frames_) setZoom(zoom_ + (t == kZoomIn ? 1 : -1));
      break;
    case kChopBtn: {
      const bool trans = m.chopMode == static_cast<uint8_t>(mt::ChopMode::Trans);
      if (trans && !frames_) return;  // needs the data
      if (trans) ensureOnsets();
      engine::lockProject();
      if (trans) mt::chopTransients(m, onsets_, nOnsets_, frames_, rate_);
      else mt::chopEqual(m);
      engine::unlockProject();
      app_.markDirty();
      if (m.sliceCount) selectMarker(kSlice0);
      break;
    }
    case kClr: {
      if (sel_ < kSlice0) return;
      const int i = sel_ - kSlice0;
      engine::lockProject();
      mt::sliceRemove(m, i);
      engine::unlockProject();
      app_.markDirty();
      selectMarker(i > 0 ? kSlice0 + i - 1 : kS);
      break;
    }
    default: break;
  }
}

void SampleEditor::onTouch(const TouchEvent& ev) {
  sync();
  const int waveY1 = y_ + kWaveH, toolY1 = waveY1 + kToolH;
  if (ev.type == TouchType::HDrag) {
    if (ev.id != dragId_) {  // a new gesture: what does it move?
      dragId_ = ev.id;
      drag_ = Drag::None;
      if (ev.y0 >= y_ && ev.y0 < waveY1 && frames_) {
        const int id = nearestMarker(ev.x0);
        if (id >= 0) {
          selectMarker(id);
          drag_ = Drag::Move;
        } else if (zoom_ > 0) {
          drag_ = Drag::Scroll;
        }
      }
    }
    if (drag_ == Drag::Move && visible(sel_)) {
      uint32_t f = frameAt(ev.x);
      if (app_.shift()) f = snap(f);
      setMarkerFrame(sel_, f);
      showFrame(markerFrame(sel_));  // a snapped marker may land off screen
    } else if (drag_ == Drag::Scroll) {
      setCol(static_cast<int64_t>(viewCol_) - ev.dx);  // 1 px = 1 column
    }
    return;
  }
  if (ev.y >= y_ && ev.y < waveY1) {
    if (ev.type == TouchType::Tap) {
      const int id = nearestMarker(ev.x);
      if (id >= 0) selectMarker(id);
    } else if (ev.type == TouchType::LongPress && frames_) {
      uint32_t f = frameAt(ev.x);
      if (app_.shift()) f = snap(f);
      engine::lockProject();
      const int i = mt::sliceInsert(inst(), mt::frameToFrac(f, frames_));
      engine::unlockProject();
      if (i < 0) return;  // full or taken
      app_.markDirty();
      selectMarker(kSlice0 + i);
    }
    return;
  }
  if (ev.y >= waveY1 && ev.y < toolY1) {
    if (ev.type == TouchType::Tap || ev.type == TouchType::LongPress)
      tool(clampi(ev.x / kToolW, 0, kClr), ev.type == TouchType::LongPress);
    return;
  }
  list_.onTouch(ev);
}

// --- drawing ---

void SampleEditor::updateWave() {
  const uint32_t g0 = viewCol_, vl = viewLen();
  const bool same = data_ == waveData_ && frames_ == waveFrames_ && gen_ == waveGen_ && vl == waveLen_;
  if (same && g0 == waveCol_) return;
  // Scrolled by fewer than kScreenW columns: shift the cache, compute only the exposed columns.
  int from = 0, to = kScreenW;
  if (same) {
    const int64_t k = static_cast<int64_t>(g0) - waveCol_;
    if (k > 0 && k < kScreenW) {
      memmove(waveMin_, waveMin_ + k, kScreenW - k);
      memmove(waveMax_, waveMax_ + k, kScreenW - k);
      from = kScreenW - static_cast<int>(k);
    } else if (k < 0 && -k < kScreenW) {
      memmove(waveMin_ - k, waveMin_, kScreenW + k);
      memmove(waveMax_ - k, waveMax_, kScreenW + k);
      to = static_cast<int>(-k);
    }
  }
  waveData_ = data_;
  waveFrames_ = frames_;
  waveGen_ = gen_;
  waveCol_ = g0;
  waveLen_ = vl;
  if (!data_ || !frames_) return;
  // Long views: at most kProbe evenly spaced frames per column (flash reads are not free).
  constexpr uint32_t kProbe = 256;
  for (int x = from; x < to; ++x) {
    const uint32_t a = gridFrame(g0 + x);
    uint32_t b = gridFrame(g0 + x + 1);
    if (b <= a) b = a + 1;
    if (a >= frames_) {  // past the end of a short sample
      waveMin_[x] = 1;
      waveMax_[x] = 0;
      continue;
    }
    const uint32_t stride = (b - a) > kProbe ? (b - a) / kProbe : 1;
    int lo = 32767, hi = -32768;
    for (uint32_t k = a; k < b && k < frames_; k += stride) {
      const int v = data_[k];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    waveMin_[x] = static_cast<int8_t>(lo >> 8);
    waveMax_[x] = static_cast<int8_t>(hi >> 8);
  }
}

void SampleEditor::drawWave(LGFX_Sprite& s, int y) {
  s.fillRect(0, y, kScreenW, kWaveH, kBeatBg);
  updateWave();
  if (!data_) {
    const char* msg = inst().sample[0] ? "MISSING" : "NO SAMPLE";
    s.setTextColor(inst().sample[0] ? kRed : kDim);
    s.drawString(msg, (kScreenW - static_cast<int>(strlen(msg)) * kCharW) / 2, y + (kWaveH - kCharH) / 2);
    return;
  }
  const uint32_t fs = markerFrame(kS), fe = markerFrame(kE);
  const int mid = y + kWaveH / 2;
  constexpr int kHalf = kWaveH / 2 - 1;
  for (int x = 0; x < kScreenW; ++x) {
    if (waveMin_[x] > waveMax_[x]) continue;
    const uint32_t a = gridFrame(viewCol_ + x);
    const int y1 = mid - waveMax_[x] * kHalf / 128;
    const int y2 = mid - waveMin_[x] * kHalf / 128;
    s.drawFastVLine(x, y1, y2 - y1 + 1, a >= fs && a < fe ? kText : kDim);
  }
  auto line = [&](int id, uint16_t c) {
    int x = xOf(markerFrame(id));
    if (x == kScreenW) x = kScreenW - 1;  // End at the very end of the view
    if (x < 0 || x >= kScreenW) return x;
    if (id == sel_) s.fillRect(x - 1, y, 3, kWaveH, c);
    else s.drawFastVLine(x, y, kWaveH, c);
    return x;
  };
  // Slices with their numbers (1-based), skipping labels too close to the previous one.
  s.setTextColor(kCyan);
  int lastLabel = -kLabelGap;
  char buf[8];
  for (int i = 0; i < inst().sliceCount; ++i) {
    const int x = line(kSlice0 + i, kCyan);
    if (x < 0 || x >= kScreenW || x - lastLabel < kLabelGap) continue;
    snprintf(buf, sizeof(buf), "%d", i + 1);
    s.drawString(buf, x + 3, y + 2);
    lastLabel = x;
  }
  if (visible(kL)) line(kL, kYellow);
  line(kS, kGreen);
  line(kE, kRed);
  if (zoom_ > 0 && viewLen() == static_cast<uint32_t>(kScreenW)) snprintf(buf, sizeof(buf), "1:1");
  else snprintf(buf, sizeof(buf), "x%u", 1u << zoom_);
  s.setTextColor(kDim);
  s.drawString(buf, kScreenW - 4 - static_cast<int>(strlen(buf)) * kCharW, y + 2);
}

void SampleEditor::drawTools(LGFX_Sprite& s, int y) {
  static const char* const kNames[] = {"<", ">", "-", "+", "CHOP", "CLR"};
  const int ty = y + (kToolH - 4 - kCharH) / 2 + 2;
  s.setTextColor(kCursor);
  for (int i = 0; i <= kClr; ++i) {
    s.fillRect(i * kToolW + 2, y + 2, kToolW - 4, kToolH - 4, kPlayBg);
    s.drawString(kNames[i], i * kToolW + (kToolW - static_cast<int>(strlen(kNames[i])) * kCharW) / 2, ty);
  }
}

void SampleEditor::draw(LGFX_Sprite& s, int y) {
  y_ = y;
  sync();
  rows_[kAmount].label = inst().chopMode ? "Sens" : "Chop N";
  drawWave(s, y);
  drawTools(s, y + kWaveH);
  list_.draw(s, y + kWaveH + kToolH);
}

}  // namespace ui
