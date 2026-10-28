#pragma once
#include <functional>
#include <stdint.h>
#include "dice.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "param_list.h"
#include "touch.h"

namespace ui {

class App;

// The DICE window over PROJ (header DICE / a long encoder press), fills the work area: Genre, Style
// (of the genre; a new genre starts at its first style), Variety 0..100 (Shift x10), Length AUTO / 16 / 32 / 64 / 128
// (AUTO = the style's length), Scope ALL / TEMPO
// / SOUNDS / PATTERNS, Song ON / OFF (grey unless Scope writes patterns), then ROLL / UNDO / CLOSE.
// ROLL keeps the window open (another ROLL = a new seed); before patterns with notes are written it
// asks OVERWRITE Pxx-Pyy? once per target range while the window stays open. UNDO = the host's one
// undo step (grey when there is nothing to undo). B / a long press closes. The settings are device
// settings (NVS, App::saveSettingsIdle). The Param edits run under engine::lockProject(): they only
// change members.
class DiceDialog {
 public:
  struct Host {
    std::function<void(const mt::DiceOpts&)> roll;  // outside the project lock
    std::function<void()> undo;
    std::function<bool()> canUndo;
  };
  explicit DiceDialog(App& app);
  void setHost(Host h) { host_ = std::move(h); }
  void loadPrefs();  // NVS, at boot
  void savePrefs();  // NVS: the changed settings (App::saveSettingsIdle: stopped, idle)
  void open();
  bool isOpen() const { return open_; }
  void close();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kGenre, kStyle, kVariety, kLength, kScope, kSong, kRoll, kUndo, kClose, kRows };
  enum Scope : uint8_t { kScAll, kScTempo, kScSounds, kScPatterns, kScopes };
  struct Prefs {
    uint8_t genre = 0, style = 0, variety = mt::kDiceVarietyDefault, scope = kScAll, song = 1;
    uint8_t len = 0;  // index into kLens: 0 = AUTO
    bool operator!=(const Prefs& o) const {
      return genre != o.genre || style != o.style || variety != o.variety || scope != o.scope || song != o.song ||
             len != o.len;
    }
  };
  static constexpr int kHeaderH = 28;
  static constexpr int kBtnY = 2, kBtnH = 20;
  static constexpr int kUndoX = 248, kUndoW = 64;
  static constexpr int kCloseX = 320, kCloseW = 72;
  static constexpr int kRollX = 400, kRollW = 64;

  uint8_t layers() const;  // kDice* bits of Scope + Song
  bool patterns() const { return pr_.scope == kScAll || pr_.scope == kScPatterns; }
  void clampPrefs();
  void roll();            // asks before overwriting, then rollNow()
  void rollNow(int first);
  void undo();
  bool action(int row);  // ROLL / UNDO / CLOSE row: runs it, true when handled
  void drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel, bool dim);

  App& app_;
  Host host_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  Prefs pr_, saved_;
  int confirmed_ = -1;  // first pattern of the range OVERWRITE was answered (or rolled) for, -1 = none
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
