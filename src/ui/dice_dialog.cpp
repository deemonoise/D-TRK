#include "dice_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "storage/settings.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int wrapi(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }

const char* const kScopeNames[] = {"ALL", "TEMPO", "SOUNDS", "PATTERNS"};
constexpr uint8_t kScopeLayers[] = {mt::kDiceAll, mt::kDiceTempo, mt::kDiceSounds, mt::kDicePattern};

// Length row: 0 = AUTO (the style's length), else pattern steps.
constexpr uint8_t kLens[] = {0, 16, 32, 64, 128};
constexpr int kLenCount = sizeof(kLens) / sizeof(kLens[0]);

enum : int { kMenuCancel, kMenuOverwrite };

}  // namespace

DiceDialog::DiceDialog(App& app) : app_(app) {
  params_[kGenre] = {"Genre",
                     [this](char* o, int n) {
                       const char* g = mt::diceGenreName(pr_.genre);
                       snprintf(o, n, "%s", g ? g : "-");
                     },
                     [this](int d) {
                       const int g = wrapi(pr_.genre + d, mt::diceGenreCount());
                       if (g != pr_.genre) pr_.style = 0;  // the styles are the genre's
                       pr_.genre = static_cast<uint8_t>(g);
                     }};
  params_[kStyle] = {"Style",
                     [this](char* o, int n) {
                       const char* st = mt::diceStyleName(pr_.genre, pr_.style);
                       snprintf(o, n, "%s", st ? st : "-");
                     },
                     [this](int d) { pr_.style = static_cast<uint8_t>(wrapi(pr_.style + d, mt::diceStyleCount(pr_.genre))); }};
  params_[kVariety] = {"Variety", [this](char* o, int n) { snprintf(o, n, "%u", static_cast<unsigned>(pr_.variety)); },
                       [this](int d) { pr_.variety = static_cast<uint8_t>(clampi(pr_.variety + d, 0, 100)); }};
  params_[kLength] = {"Length",
                      [this](char* o, int n) {
                        const uint8_t l = kLens[pr_.len % kLenCount];
                        if (l) snprintf(o, n, "%u", static_cast<unsigned>(l));
                        else snprintf(o, n, "AUTO");
                      },
                      [this](int d) { pr_.len = static_cast<uint8_t>(clampi(pr_.len + d, 0, kLenCount - 1)); },
                      [this] { return !patterns(); }};
  params_[kScope] = {"Scope", [this](char* o, int n) { snprintf(o, n, "%s", kScopeNames[pr_.scope % kScopes]); },
                     [this](int d) { pr_.scope = static_cast<uint8_t>(clampi(pr_.scope + d, 0, kScopes - 1)); }};
  params_[kSong] = {"Song", [this](char* o, int n) { snprintf(o, n, "%s", pr_.song ? "ON" : "OFF"); },
                    [this](int d) { pr_.song = d > 0; }, [this] { return !patterns(); }};
  params_[kRoll] = {"Roll", [](char* o, int n) { snprintf(o, n, "NEW SEED"); }, nullptr};
  params_[kUndo] = {"Undo dice", nullptr, nullptr, [this] { return !(host_.canUndo && host_.canUndo()); }};
  params_[kClose] = {"Close", [](char* o, int n) { snprintf(o, n, "LONG PRESS"); }, nullptr};
  list_.setParams(params_, kRows);
}

void DiceDialog::clampPrefs() {
  if (pr_.genre >= mt::diceGenreCount()) pr_.genre = 0;
  if (pr_.style >= mt::diceStyleCount(pr_.genre)) pr_.style = 0;
  if (pr_.variety > 100) pr_.variety = mt::kDiceVarietyDefault;
  if (pr_.scope >= kScopes) pr_.scope = kScAll;
  pr_.song = pr_.song ? 1 : 0;
  if (pr_.len >= kLenCount) pr_.len = 0;
}

void DiceDialog::loadPrefs() {
  const Prefs d;
  pr_.genre = storage::loadSetting("dicegen", d.genre);
  pr_.style = storage::loadSetting("dicesty", d.style);
  pr_.variety = storage::loadSetting("dicevar", d.variety);
  pr_.scope = storage::loadSetting("dicescp", d.scope);
  pr_.song = storage::loadSetting("dicesong", d.song);
  const uint8_t steps = storage::loadSetting("dicelen", kLens[d.len]);  // NVS keeps the steps (0 = AUTO)
  pr_.len = 0;
  for (int i = 0; i < kLenCount; ++i)
    if (kLens[i] == steps) pr_.len = static_cast<uint8_t>(i);
  clampPrefs();
  saved_ = pr_;
}

void DiceDialog::savePrefs() {
  if (!(pr_ != saved_)) return;
  if (pr_.genre != saved_.genre) storage::saveSetting("dicegen", pr_.genre);
  if (pr_.style != saved_.style) storage::saveSetting("dicesty", pr_.style);
  if (pr_.variety != saved_.variety) storage::saveSetting("dicevar", pr_.variety);
  if (pr_.scope != saved_.scope) storage::saveSetting("dicescp", pr_.scope);
  if (pr_.song != saved_.song) storage::saveSetting("dicesong", pr_.song);
  if (pr_.len != saved_.len) storage::saveSetting("dicelen", kLens[pr_.len % kLenCount]);
  saved_ = pr_;
}

uint8_t DiceDialog::layers() const {
  uint8_t l = kScopeLayers[pr_.scope % kScopes];
  if (patterns() && pr_.song) l |= mt::kDiceSong;
  return l;
}

void DiceDialog::open() {
  clampPrefs();
  confirmed_ = -1;  // a new session asks again
  list_.setEdit(false);
  list_.setSel(kRoll);
  open_ = true;
}

void DiceDialog::close() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  if (pr_ != saved_) app_.settingsChanged();
}

void DiceDialog::roll() {
  list_.setEdit(false);
  const int edit = app_.editPattern();
  const int first = mt::diceFirstPattern(edit);
  // Read-only, outside the lock: the UI is the only writer of the patterns.
  if (patterns() && first != confirmed_ && mt::diceOverwrites(app_.project(), edit) != 0) {
    char title[32];
    snprintf(title, sizeof(title), "OVERWRITE P%02d-P%02d?", first + 1, first + mt::kDicePats);
    const MenuItem items[] = {{"Cancel", kMenuCancel}, {"Overwrite", kMenuOverwrite}};
    app_.menu().open(title, items, 2, [this, first](int id) {
      if (id == kMenuOverwrite) rollNow(first);
    });
    return;
  }
  rollNow(first);
}

void DiceDialog::rollNow(int first) {
  if (patterns()) confirmed_ = first;  // the range now holds this session's roll: re-rolls do not ask
  mt::DiceOpts o;
  o.genre = pr_.genre;
  o.style = pr_.style;
  o.variety = pr_.variety;
  o.len = kLens[pr_.len % kLenCount];
  o.layers = layers();
  if (host_.roll) host_.roll(o);
  if (pr_ != saved_) app_.settingsChanged();
  app_.invalidate();
}

void DiceDialog::undo() {
  list_.setEdit(false);
  if (host_.canUndo && !host_.canUndo()) return;
  if (host_.undo) host_.undo();
  app_.invalidate();
}

bool DiceDialog::action(int row) {
  if (row == kRoll) roll();
  else if (row == kUndo) undo();
  else if (row == kClose) close();
  else return false;
  app_.invalidate();
  return true;
}

void DiceDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  switch (ev.type) {
    case InputType::EncLong:
      close();
      app_.invalidate();
      return;
    case InputType::EncClick:
      if (!list_.editing() && action(list_.sel())) return;
      break;
    default: break;
  }
  list_.onInput(ev);
}

void DiceDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    const int by = y0_ + kBtnY;
    if (ev.y >= by && ev.y < by + kBtnH) {
      if (ev.x >= kUndoX && ev.x < kUndoX + kUndoW) action(kUndo);
      else if (ev.x >= kCloseX && ev.x < kCloseX + kCloseW) action(kClose);
      else if (ev.x >= kRollX && ev.x < kRollX + kRollW) action(kRoll);
    }
    return;
  }
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    if (r == kRoll || r == kUndo || r == kClose) {
      list_.setSel(r);
      action(r);
      return;
    }
  }
  list_.onTouch(ev);
}

void DiceDialog::drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel, bool dim) {
  s.fillRect(x, y, w, kBtnH, sel ? kPlayBg : kMenuBg);
  s.drawRect(x, y, w, kBtnH, sel ? kCursor : kDim);
  s.setTextColor(dim ? kDim : (sel ? kCursor : kText));
  s.drawString(label, x + (w - static_cast<int>(strlen(label)) * kCharW) / 2, y + (kBtnH - kCharH) / 2);
}

void DiceDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  char buf[32];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  const int first = mt::diceFirstPattern(app_.editPattern());
  if (patterns()) snprintf(buf, sizeof(buf), "DICE P%02d-P%02d", first + 1, first + mt::kDicePats);
  else snprintf(buf, sizeof(buf), "DICE");
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  const bool canUndo = host_.canUndo && host_.canUndo();
  drawButton(s, kUndoX, y0 + kBtnY, kUndoW, "UNDO", list_.sel() == kUndo, !canUndo);
  drawButton(s, kCloseX, y0 + kBtnY, kCloseW, "CLOSE", list_.sel() == kClose, false);
  drawButton(s, kRollX, y0 + kBtnY, kRollW, "ROLL", list_.sel() == kRoll, false);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
