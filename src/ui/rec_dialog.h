#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "link_msg.h"
#include "param_list.h"
#include "rec_ui.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// INST (SAMPLE) -> SAMPLE menu -> Record...: a take from MIC / LINE (the codec's inputs) or RESAMPLE
// (the synth's own output) into /samples/rec/RECn.wav on the synth board and onto the instrument.
// Rows: Source (MIC / LINE / RESAMPLE; only RESAMPLE without the shield), Gain (MIC +0..63 dB, LINE
// 0..15, RESAMPLE none), Thresh (OFF, -60..-6 dB), Mon (OFF / ON), REC / STOP, Cancel. The header
// shows the state (ARMED blinks), the meter (peak held 1 s, red at -1 dB and up) and the time. While
// a take runs only Gain and Mon change. STOP (also B / long press, or leaving the screen: stop())
// saves with "SAVING NN%", binds sample RECn to the instrument (slices cleared, root 60), toasts
// "REC1 12.4s, 3 CLIPS" and calls the done callback. Cancel drops the take. poll() every UI tick:
// RecLevel every kPollMs (the meter and the board's keepalive), the Gain / Mon changes. Fills the
// work area.
class RecDialog {
 public:
  explicit RecDialog(App& app);
  void open(int instr, std::function<void()> onSaved);
  bool isOpen() const { return open_; }
  bool taking() const { return taking_; }
  void stop();    // a running take is saved and bound; else the window closes
  void cancel();  // the take is dropped, the window closes
  // The project was replaced: the window closes without a word to the board, whose keepalive then
  // seals a running take into its file (not into the store, not bound).
  void detach();
  void poll();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kSource, kGain, kThresh, kMon, kRec, kCancel, kRows };
  static constexpr int kHeaderH = 68;
  static constexpr uint32_t kPollMs = 100, kRetryMs = 500, kBlinkMs = 300;

  bool action(int row);
  void start();
  void bind(const mt::link::RecStopRep& rep, bool cardSlow);
  void send();  // RecSet with the rows' gain / mon / source
  void level(const mt::link::RecLevelRep& rep, uint32_t now);
  void closeWindow();
  int gain() const { return gains_[static_cast<int>(src_)]; }

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  std::function<void()> onSaved_;
  int instr_ = 0;
  mt::RecSource src_ = mt::RecSource::Mic;  // kept across open() until reboot
  int gains_[3] = {mt::recGainDefault(mt::RecSource::Mic), mt::recGainDefault(mt::RecSource::Line), 0};
  int8_t thresh_ = mt::kRecThreshOff;
  bool mon_ = false;
  bool codec_ = true;     // the board's last word: MIC / LINE available
  bool pendingSet_ = false;
  bool taking_ = false;   // RecStart went through, no STOP / CANCEL yet
  bool warned_ = false;   // CARD TOO SLOW / WRITE FAIL toasted for this take
  mt::RecState state_ = mt::RecState::Idle;
  uint16_t number_ = 0;
  uint32_t frames_ = 0, clips_ = 0;
  mt::RecPeakHold hold_;
  uint32_t nextPoll_ = 0;
  bool open_ = false;
};

}  // namespace ui
