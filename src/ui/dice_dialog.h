#pragma once
#include "dialog_actions.h"
#include "dice_dialog_state.h"
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
// / SOUNDS / PATTERNS, Song ON / OFF, source BUILTIN / MIDI / HYBRID, Clean, seed and role locks.
// Target/Change/Form/Feel and generation settings scroll above the ROLL / CLOSE footer.
// One roll can be undone through the host until the project is replaced.
// ROLL keeps the window open (NEW seed mode draws again; FIXED repeats the displayed seed); before patterns with notes are written it
// asks OVERWRITE Pxx-Pyy? once per target range while the window stays open. B / a long press closes. The settings are device
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
  void usedSeed(uint32_t seed);  // remember the successful roll for FIXED/replay
  bool isOpen() const { return open_; }
  void close();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int {
    kGenre, kStyle, kTarget, kChange, kForm, kFeel, kDensity, kVariety, kLength, kScope, kSong, kSource, kClean, kSeedMode, kSeed,
    kLockDrums, kLockBass, kLockChords, kLockLead, kLockArp, kLockFx,
    kUndo, kRoll, kClose, kRows
  };
  using Prefs = dice_dialog::Prefs;
  static constexpr int kHeaderH = 28;

  bool patterns() const { return dice_dialog::patterns(pr_); }
  void clampPrefs();
  void roll();            // asks before overwriting, then rollNow()
  void rollNow(const mt::DiceOpts& opts, dice_dialog::Range range);
  bool action(int row);  // ROLL / CLOSE row: runs it, true when handled

  DialogActions actions_;
  App& app_;
  Host host_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  Prefs pr_, saved_;
  dice_dialog::Range confirmed_;  // full range answered or rolled in this session
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
