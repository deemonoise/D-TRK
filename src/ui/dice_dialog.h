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

// Guide groups/styles, 16 named variants, six dynamic track roles and scoped generation.
// ROLL asks once before replacing the displayed pattern range. Settings persist in NVS.
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
  void usedSeed(uint32_t seed,uint8_t variant);  // remember seed and AUTO selection
  bool isOpen() const { return open_; }
  void close();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int {
    kGenre, kStyle, kVariant, kKey, kTarget, kChange, kForm, kFeel, kDensity, kVariety, kLength, kScope, kSong, kClean, kSeedMode, kSeed,
    kLockDrums, kLockBass, kLockChords, kLockLead, kLockArp, kLockFx,
    kRoll, kClose, kRows
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
  dice_dialog::LastRoll last_;
  dice_dialog::Range confirmed_;  // full range answered or rolled in this session
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
