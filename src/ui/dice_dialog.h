#pragma once
#include "dialog_actions.h"
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
// No history snapshots are retained after a roll.
// ROLL keeps the window open (NEW seed mode draws again; FIXED repeats the displayed seed); before patterns with notes are written it
// asks OVERWRITE Pxx-Pyy? once per target range while the window stays open. B / a long press closes. The settings are device
// settings (NVS, App::saveSettingsIdle). The Param edits run under engine::lockProject(): they only
// change members.
class DiceDialog {
 public:
  struct Host {
    std::function<void(const mt::DiceOpts&)> roll;  // outside the project lock
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
    kGenre, kStyle, kTarget, kChange, kForm, kFeel, kVariety, kLength, kScope, kSong, kSource, kClean, kSeedMode, kSeed,
    kLockDrums, kLockBass, kLockChords, kLockLead, kLockArp, kLockFx,
    kRoll, kClose, kRows
  };
  enum Scope : uint8_t { kScAll, kScTempo, kScSounds, kScPatterns, kScopes };
  struct Prefs {
    uint8_t genre = 0, style = 0, variety = mt::kDiceVarietyDefault, scope = kScAll, song = 1;
    uint8_t len = 0;  // index into kLens: 0 = AUTO
    uint8_t target = 0, change = 2, form = 1, feel = 35;
    uint8_t source = 0, clean = 1, fixedSeed = 0, locks = 0;
    uint32_t seed = 1;
    bool operator!=(const Prefs& o) const {
      return target != o.target || change != o.change || form != o.form || feel != o.feel || genre != o.genre || style != o.style || variety != o.variety || scope != o.scope || song != o.song ||
             len != o.len || source != o.source || clean != o.clean || fixedSeed != o.fixedSeed ||
             locks != o.locks || seed != o.seed;
    }
  };
  static constexpr int kHeaderH = 28;

  uint8_t layers() const;  // kDice* bits of Scope + Song
  bool patterns() const { return pr_.scope == kScAll || pr_.scope == kScPatterns; }
  void clampPrefs();
  void roll();            // asks before overwriting, then rollNow()
  void rollNow(int first);
  bool action(int row);  // ROLL / CLOSE row: runs it, true when handled

  DialogActions actions_;
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
