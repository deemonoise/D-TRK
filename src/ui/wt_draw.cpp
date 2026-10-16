#include "wt_draw.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "link/bank_client.h"
#include "menu.h"
#include "project_io.h"
#include "storage/storage.h"
#include "wt_picker.h"

namespace ui {

namespace {

constexpr int N = mt::kWtFrameLen;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

const char* const kBtnLabel[] = {"SIN", "TRI", "SAW", "SQR", "SMTH", "KEY", "DEL", "OK", "CANCEL"};

struct FileSink final : mt::ByteSink {
  fs::File* f;
  bool write(const void* d, size_t n) override { return f->write(static_cast<const uint8_t*>(d), n) == n; }
};

}  // namespace

bool WtDraw::open(const char* source, std::function<void(const char*)> onDone) {
  close();
  keys_ = static_cast<mt::WtKey*>(heap_caps_malloc(sizeof(mt::WtKey) * mt::kWtKeysMax, MALLOC_CAP_SPIRAM));
  if (!keys_) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return false;
  }
  onDone_ = std::move(onDone);
  n_ = 2;
  keys_[0].frame = 0;
  keys_[1].frame = mt::kWtFrames - 1;
  for (int k = 0; k < 2; ++k) {
    int8_t pts[N];
    if (source && source[0] && audio::wtFrame(source, keys_[k].frame, pts))
      for (int i = 0; i < N; ++i) keys_[k].pts[i] = pts[i] / 128.0f;
    else
      mt::wtShape(mt::WtShape::Sin, keys_[k].pts);
  }
  // Default name: the source without the built-in "*", else DRAW.
  const char* b = source && source[0] ? (source[0] == '*' ? source + 1 : source) : "";
  if (!storage::sanitize(b, base_)) strlcpy(base_, "DRAW", sizeof(base_));
  sel_ = pos_ = 0;
  cur_ = N / 2;
  focus_ = kFRuler;
  btn_ = kOk;
  editing_ = changed_ = false;
  strokeId_ = dragId_ = 0xFFFF;
  dragKey_ = -1;
  app_.invalidate();
  return true;
}

void WtDraw::close() {
  if (!keys_) return;
  kb_.close();
  heap_caps_free(keys_);
  keys_ = nullptr;
  onDone_ = nullptr;
  app_.invalidate();
}

int WtDraw::keyAt(int frame) const {
  for (int i = 0; i < n_; ++i)
    if (keys_[i].frame == frame) return i;
  return -1;
}

void WtDraw::selectKey(int i) {
  sel_ = clampi(i, 0, n_ - 1);
  pos_ = keys_[sel_].frame;
}

bool WtDraw::addKey(int frame) {
  if (keyAt(frame) >= 0) {
    selectKey(keyAt(frame));
    return false;
  }
  if (n_ >= mt::kWtKeysMax) {
    app_.toast("KEYS FULL");
    return false;
  }
  mt::WtKey& k = keys_[n_];
  mt::wtKeyAt(keys_, n_, frame, k.pts);
  k.frame = static_cast<uint8_t>(frame);
  ++n_;
  mt::wtSortKeys(keys_, n_);
  selectKey(keyAt(frame));
  changed_ = true;
  return true;
}

float* WtDraw::editPts() {
  int i = keyAt(pos_);
  if (i < 0) {
    if (!addKey(pos_)) return nullptr;
    i = keyAt(pos_);
  }
  sel_ = i;
  return keys_[i].pts;
}

void WtDraw::moveKey(int i, int frame) {
  if (i <= 0 || i >= n_ - 1) return;  // frames 0 and 63 stay
  frame = clampi(frame, keys_[i - 1].frame + 1, keys_[i + 1].frame - 1);
  if (frame == keys_[i].frame) return;
  keys_[i].frame = static_cast<uint8_t>(frame);
  selectKey(i);
  changed_ = true;
}

void WtDraw::setPoint(int pt, float v) {
  float* f = editPts();
  if (!f) return;
  pt = clampi(pt, 0, N - 1);
  v = clampf(v);
  if (lastPt_ >= 0 && lastPt_ != pt) {  // a line from the stroke's last point
    const int d = pt > lastPt_ ? 1 : -1;
    for (int p = lastPt_ + d; p != pt; p += d) f[p] = lastV_ + (v - lastV_) * (p - lastPt_) / float(pt - lastPt_);
  }
  f[pt] = v;
  lastPt_ = pt;
  lastV_ = v;
  changed_ = true;
}

void WtDraw::brush(int d) {
  float* f = editPts();
  if (!f) return;
  const float target = clampf(f[cur_] + d * 0.04f);
  for (int o = -3; o <= 4; ++o) {
    const int p = (cur_ + o + N) % N;
    const float w = 1.0f - (o < 0 ? -o : o) / 4.0f;
    f[p] += (target - f[p]) * w;
  }
  changed_ = true;
}

void WtDraw::press(int b) {
  switch (b) {
    case kSin:
    case kTri:
    case kSaw:
    case kSqr:
      if (float* f = editPts()) {
        mt::wtShape(static_cast<mt::WtShape>(b - kSin), f);
        changed_ = true;
      }
      break;
    case kSmth:
      if (float* f = editPts()) {
        mt::wtSmooth(f);
        changed_ = true;
      }
      break;
    case kKey: addKey(pos_); break;
    case kDel: {
      const int i = keyAt(pos_);
      if (i <= 0 || i >= n_ - 1) {
        app_.toast("KEYS 0 / 63 STAY");
        break;
      }
      for (int j = i; j + 1 < n_; ++j) keys_[j] = keys_[j + 1];
      --n_;
      changed_ = true;  // pos_ stays: the interpolation there is shown
      sel_ = clampi(i - 1, 0, n_ - 1);
      break;
    }
    case kOk: ok(); return;
    case kCancel: cancel(); return;
    default: break;
  }
  app_.invalidate();
}

void WtDraw::cancel() {
  if (!changed_) {
    close();
    return;
  }
  enum : int { kNo, kYes };
  const MenuItem items[] = {{"Keep drawing", kNo}, {"Discard", kYes}};
  app_.menu().open("DISCARD KEYS?", items, 2, [this](int i) {
    if (i == kYes) close();
  });
}

void WtDraw::ok() {
  const engine::Status& st = app_.status();
  if (st.playing || st.paused) {  // the editor stays open: drawing goes on while it plays
    app_.toast(audio::bankResultText(audio::BankResult::Busy));
    return;
  }
  kb_.open("TABLE NAME:", base_, [this](const char* t) {
    char nm[mt::kSampleNameMax + 1];
    if (!storage::sanitize(t, nm)) strlcpy(nm, "DRAW", sizeof(nm));
    save(nm);
  });
  app_.invalidate();
}

void WtDraw::save(const char* name) {
  if (!hw::sdReady()) {
    app_.toast(audio::bankResultText(audio::BankResult::NoSd));
    return;
  }
  if (app_.project().wavetableCount >= mt::kProjWavetables) {
    app_.toast("TABLE LIST FULL");
    return;
  }
  strlcpy(base_, name, sizeof(base_));
  snprintf(path_, sizeof(path_), "/wavetables/%s.wav", base_);
  auto write = [this] {
    int16_t* src = static_cast<int16_t*>(heap_caps_malloc(sizeof(int16_t) * mt::kWtSrcSamples, MALLOC_CAP_SPIRAM));
    if (!src) {
      app_.toast(storage::resultText(storage::Result::NoMemory));
      return;
    }
    mt::wtFromKeys(keys_, n_, src);
    app_.showBusy("WT WRITE...");
    fs::FS& fs = hw::sdFs();
    char tmp[sizeof(path_) + 4];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path_);
    if (!fs.exists("/wavetables")) fs.mkdir("/wavetables");
    if (fs.exists(tmp)) fs.remove(tmp);
    fs::File f = fs.open(tmp, FILE_WRITE);
    bool ok = static_cast<bool>(f);
    if (ok) {
      FileSink sink;
      sink.f = &f;
      ok = mt::wtWavWrite(src, sink);
      f.close();
    }
    heap_caps_free(src);
    if (ok) {
      if (fs.exists(path_)) fs.remove(path_);
      ok = fs.rename(tmp, path_);
    }
    if (!ok) {
      if (fs.exists(tmp)) fs.remove(tmp);
      hw::sdRecover();
      app_.endProgress();
      app_.toast(storage::resultText(storage::Result::WriteFail));
      return;
    }
    char nm[mt::kSampleNameMax + 1];
    const bool imported = wtImportFile(app_, path_, base_, nm);
    app_.endProgress();
    if (!imported) return;  // it toasted; the keys stay for another try
    std::function<void(const char*)> cb = onDone_;
    close();
    if (cb) cb(nm);
  };
  if (hw::sdFs().exists(path_)) {
    enum : int { kNo, kYes };
    const MenuItem items[] = {{"Cancel", kNo}, {"Overwrite", kYes}};
    app_.menu().open("FILE EXISTS", items, 2, [write](int i) {
      if (i == kYes) write();
    });
    return;
  }
  write();
}

void WtDraw::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!keys_) return;
  if (kb_.isOpen()) {
    kb_.onInput(ev);
    app_.invalidate();
    return;
  }
  app_.invalidate();
  switch (ev.type) {
    case InputType::EncLong: cancel(); return;
    case InputType::EncTurn:
      if (ev.shift) {
        editing_ = false;
        focus_ = clampi(focus_ + (ev.delta > 0 ? 1 : -1), kFRuler, kFBtn);
        return;
      }
      if (focus_ == kFRuler) selectKey(sel_ + ev.delta);
      else if (focus_ == kFBtn) btn_ = ((btn_ + ev.delta) % kBtns + kBtns) % kBtns;
      else if (editing_) brush(ev.delta);
      else cur_ = ((cur_ + ev.delta * 2) % N + N) % N;
      return;
    case InputType::EditTurn:
      if (focus_ == kFRuler) moveKey(keyAt(pos_), pos_ + ev.delta);
      else if (focus_ == kFCanvas) brush(ev.delta);
      return;
    case InputType::EncClick:
      if (focus_ == kFRuler) focus_ = kFCanvas;
      else if (focus_ == kFCanvas) editing_ = !editing_;
      else press(btn_);
      return;
    default: return;
  }
}

void WtDraw::onTouch(const TouchEvent& ev) {
  if (!keys_) return;
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    app_.invalidate();
    return;
  }
  app_.invalidate();
  const int canTop = y0_ + kCanTop, rulTop = y0_ + kRulTop, btnTop = y0_ + kAreaH - kBtnH;
  if (ev.type == TouchType::LongPress) return;
  // Buttons.
  if (ev.type == TouchType::Tap && ev.y >= btnTop) {
    btn_ = clampi(ev.x / kBtnW, 0, kBtns - 1);
    focus_ = kFBtn;
    press(btn_);
    return;
  }
  // Ruler: tap selects a key / shows a frame; a drag from a key mark moves it.
  const int y0 = ev.type == TouchType::Tap ? ev.y : ev.y0;
  if (y0 >= rulTop - 4 && y0 < rulTop + kRulH + 4) {
    focus_ = kFRuler;
    const int frame = clampi((ev.x - kCanX) / kRulStep, 0, mt::kWtFrames - 1);
    if (ev.type == TouchType::Tap) {
      int best = -1;
      for (int i = 0; i < n_; ++i)
        if (abs(keys_[i].frame - frame) <= 1 && (best < 0 || abs(keys_[i].frame - frame) < abs(keys_[best].frame - frame)))
          best = i;
      if (best >= 0) selectKey(best);
      else pos_ = frame;
      return;
    }
    if (ev.id != dragId_) {
      dragId_ = ev.id;
      const int f0 = clampi((ev.x0 - kCanX) / kRulStep, 0, mt::kWtFrames - 1);
      dragKey_ = -1;
      for (int i = 1; i + 1 < n_; ++i)
        if (abs(keys_[i].frame - f0) <= 1) dragKey_ = i;
    }
    if (dragKey_ >= 0) moveKey(dragKey_, frame);
    return;
  }
  // Canvas.
  if (y0 >= canTop && y0 < canTop + kCanH && ev.x >= kCanX - 8 && ev.x < kCanX + kCanW + 8) {
    focus_ = kFCanvas;
    if (ev.id != strokeId_ || ev.type == TouchType::Tap) {
      strokeId_ = ev.id;
      lastPt_ = -1;
    }
    const int pt = (ev.x - kCanX) * N / kCanW;
    const float v = 1.0f - 2.0f * (ev.y - canTop) / float(kCanH);
    cur_ = clampi(pt, 0, N - 1);
    setPoint(pt, v);
  }
}

void WtDraw::draw(LGFX_Sprite& s, int y0) {
  if (!keys_) return;
  y0_ = y0;
  if (kb_.isOpen()) {
    kb_.draw(s, y0);
    return;
  }
  char buf[48];
  s.fillRect(0, y0, kScreenW, kCanTop - 4, kBeatBg);
  const int k = keyAt(pos_);
  snprintf(buf, sizeof(buf), "DRAW %s  FRAME %d %s  KEYS %d", base_, pos_, k >= 0 ? "KEY" : "", n_);
  s.setTextColor(kText);
  s.drawString(buf, 8, y0 + (kCanTop - 4 - kCharH) / 2);
  // Canvas: the neighbouring keys as ghosts, the shown frame on top.
  const int top = y0 + kCanTop, mid = top + kCanH / 2;
  s.drawRect(kCanX - 1, top - 1, kCanW + 2, kCanH + 2, focus_ == kFCanvas ? kCursor : kDim);
  s.drawFastHLine(kCanX, mid, kCanW, kMenuBg);
  auto plot = [&](const float* f, uint16_t c) {
    int px = 0, py = 0;
    for (int i = 0; i < N; ++i) {
      const int x = kCanX + i * kCanW / N;
      const int y = mid - static_cast<int>(clampf(f[i]) * (kCanH / 2 - 2));
      if (i) s.drawLine(px, py, x, y, c);
      px = x;
      py = y;
    }
  };
  int a = -1, b = -1;
  for (int i = 0; i < n_; ++i) {
    if (keys_[i].frame < pos_) a = i;
    if (keys_[i].frame > pos_ && b < 0) b = i;
  }
  if (a >= 0) plot(keys_[a].pts, kDim);
  if (b >= 0) plot(keys_[b].pts, kDim);
  static float shown[N];
  mt::wtKeyAt(keys_, n_, pos_, shown);
  plot(shown, k >= 0 ? kGreen : kCyan);
  if (focus_ == kFCanvas) {
    const int cx = kCanX + cur_ * kCanW / N;
    s.drawFastVLine(cx, top, kCanH, editing_ ? kEditCursor : kCursor);
  }
  // Ruler 0..63: key marks, the shown frame.
  const int rt = y0 + kRulTop;
  s.drawRect(kCanX - 1, rt - 1, kCanW + 2, kRulH + 2, focus_ == kFRuler ? kCursor : kDim);
  for (int i = 0; i < n_; ++i) {
    const int x = kCanX + keys_[i].frame * kRulStep;
    s.fillRect(x, rt + 2, kRulStep, kRulH - 4, i == sel_ && k >= 0 ? kCursor : kText);
  }
  s.fillTriangle(kCanX + pos_ * kRulStep + kRulStep / 2, rt + kRulH + 1, kCanX + pos_ * kRulStep - 2, rt + kRulH + 7,
                 kCanX + pos_ * kRulStep + kRulStep + 2, rt + kRulH + 7, kGreen);
  // Buttons.
  const int bt = y0 + kAreaH - kBtnH;
  for (int i = 0; i < kBtns; ++i) {
    const bool sel = focus_ == kFBtn && btn_ == i;
    const int x = i * kBtnW;
    s.fillRect(x + 1, bt + 1, kBtnW - 2, kBtnH - 2, sel ? kPlayBg : kMenuBg);
    s.drawRect(x + 1, bt + 1, kBtnW - 2, kBtnH - 2, sel ? kCursor : kDim);
    s.setTextColor(sel ? kCursor : kText);
    const char* l = kBtnLabel[i];
    const int w = static_cast<int>(strlen(l)) * kCharW;
    s.drawString(l, x + (kBtnW - (w < kBtnW - 2 ? w : kBtnW - 2)) / 2, bt + (kBtnH - kCharH) / 2);
  }
}

}  // namespace ui
