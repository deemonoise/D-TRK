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

// The one recorder, owned by App (App::rec()): a take from MIC / LINE (the codec's inputs) or RESAMPLE
// (the synth's own output) into /samples/rec/RECn.wav on the synth board. Two modes:
// - Sample (INST (SAMPLE) -> SAMPLE menu -> Record..., open()): the take goes into the store and onto
//   the instrument, at most 5 min; leaving INST stops and saves it (InstScreen::onLeave).
// - Sd (MIX: the REC button / encoder long press, or the status bar's REC, openSd()): the take is only
//   the card's file, no import ("REC -> SD"), up to 4 GB or the card's space. It records on in the
//   background when the window closes (hide()) or the tab changes: App polls every frame (poll()), the
//   status bar shows "REC mm:ss". The header shows the time left (the take's left, or the card's free
//   space before the start).
// One take at a time: opening the other mode meanwhile toasts REC BUSY. The settings (Source, the
// gains, Thresh, Mon) are device settings (prefs(), App keeps them in NVS), shared by both modes.
// Rows: Source (MIC / LINE / RESAMPLE; only RESAMPLE without the shield), Gain (MIC +0..63 dB, LINE
// 0..15, RESAMPLE none), Thresh (OFF, -60..-6 dB), Mon (OFF / ON), Cancel. Below them the big START /
// STOP button (a silent tap for mic takes; the encoder's long press and an A tap do the same): START
// NOW, ARM with a Thresh, STOP. RESAMPLE with the transport stopped: START waits for Play ("ARMED: PLAY", blinking;
// App calls onPlay() before it posts the start, so the first note is in the take); a tap again = READY.
// The header shows the state (ARMED blinks), the meter (peak held 1 s, red at -1 dB and
// up) and the time. While a take runs only Gain and Mon change. STOP (also B: stop(), and a take that
// ends itself) saves with "SAVING NN%", the window staying open in READY for the next take (B in READY
// closes it);
// Sample binds sample RECn to the instrument (slices cleared, root 60), toasts "REC1 12.4s, 3 CLIPS" and
// calls the done callback; Sd toasts "REC12.WAV 3:41 SAVED" (mt::recSdToast). Cancel (mt::recCancelAct)
// drops a take, an arm or a start not yet answered ("DROP THE TAKE", "CANCEL ARM"; the .tmp deleted) or
// ends a wait for Play ("CANCEL WAIT"), the window staying open in READY; in READY it closes ("CLOSE").
// poll() every UI tick: RecLevel every kPollMs while the window is open or a take runs (the meter and
// the board's keepalive), the Gain / Mon changes, a take that ended on its own. Fills the work area.
class RecDialog {
 public:
  enum class Mode : uint8_t { Sample, Sd };
  explicit RecDialog(App& app);
  // Sample mode on instrument instr; REC BUSY (false) while a take runs.
  bool open(int instr, std::function<void()> onSaved);
  // Sd mode, the cursor on Source; a running Sd take: its window again. REC BUSY (false) during a Sample take.
  bool openSd();
  bool isOpen() const { return open_; }
  bool shownIn(Mode m) const { return open_ && mode_ == m; }  // the window, for the screen that shows it
  Mode mode() const { return mode_; }
  bool taking() const { return taking_; }
  bool sdTaking() const { return taking_ && mode_ == Mode::Sd; }  // the status bar's REC
  bool recording() const { return taking_ && state_ == mt::RecState::Rec; }
  uint32_t frames() const { return taking_ ? frames_ : 0; }
  void stop();    // a running take is saved (Sample: and bound), the window stays in READY; else it closes
  // The window goes (the tab changed): a Sd take records on; without a take as closing.
  void hide();
  void cancel();  // the Cancel row: a take / arm / wait dropped (the window stays, READY); READY: it closes
  // The project was replaced: a Sample window closes without a word to the board, whose keepalive then
  // seals a running take into its file (not into the store, not bound). Sd: nothing changes.
  void detach();
  // Device settings: loaded at boot (setPrefs), saved by App once they stay put (prefs()).
  mt::RecPrefs prefs() const;
  void setPrefs(const mt::RecPrefs& p);
  // The transport starts from stopped (App, before it posts the start): a START waiting for Play
  // (RESAMPLE) sends its RecStart now, so the take already runs at the first note.
  void onPlay();
  bool waitsPlay() const { return waitPlay_ && open_; }
  void poll();
  // A tapped alone (no turn while held, App's AbKeys) in the open window: as the big button (toggle()).
  void aTap();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kSource, kGain, kThresh, kMon, kCancel, kRows };
  static constexpr int kHeaderH = 68, kBtnMargin = 8;
  static constexpr uint32_t kPollMs = 100, kRetryMs = 500, kBlinkMs = 300, kAdoptMs = 3000;

  bool action(int row);  // the Cancel row's click
  // The big button's tap and the encoder's long press: READY starts (ARM with a Thresh), a take stops
  // (saved; an arm ends); nothing while a start is unanswered.
  void toggle();
  mt::RecBtn btnState() const;
  static mt::RecRect btnRect();  // screen coordinates: below the rows, the work area's bottom
  void drawBtn(LGFX_Sprite& s, int y0, uint32_t now);
  void start();
  void bind(const mt::link::RecStopRep& rep, bool cardSlow);
  void send();  // RecSet with the rows' gain / mon / source
  void level(const mt::link::RecLevelRep& rep, uint32_t now);
  void closeWindow();
  void ready();  // after a take (saved / dropped): READY in the window kept open; hidden: as closing
  void reset();  // a fresh window: no take, the meter cleared
  void sdEnd(const mt::link::RecStopRep& rep, mt::link::RecResult r);
  void edited();  // a setting changed by hand: App saves it
  uint32_t leftFrames() const;  // before the take ends on its own; 0 = unknown
  int gain() const { return gains_[static_cast<int>(src_)]; }

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  std::function<void()> onSaved_;
  int instr_ = 0;
  Mode mode_ = Mode::Sample;
  mt::RecSource src_ = mt::RecSource::Mic;   // the source in use (RESAMPLE without the shield)
  mt::RecSource want_ = mt::RecSource::Mic;  // the one picked (the setting): src_ again on open
  int gains_[3] = {mt::recGainDefault(mt::RecSource::Mic), mt::recGainDefault(mt::RecSource::Line), 0};
  int8_t thresh_ = mt::kRecThreshOff;
  bool mon_ = false;
  bool codec_ = true;     // the board's last word: MIC / LINE available
  bool pendingSet_ = false;
  bool taking_ = false;   // RecStart went through, no STOP / CANCEL yet
  bool waitPlay_ = false;  // START with RESAMPLE while stopped: the take starts with Play (onPlay)
  bool warned_ = false;   // CARD TOO SLOW / WRITE FAIL toasted for this take
  mt::RecState state_ = mt::RecState::Idle;
  uint16_t number_ = 0;
  uint32_t frames_ = 0, clips_ = 0;
  uint32_t left_ = 0;    // Sd take: the board's frames left
  int32_t freeMb_ = -1;  // Sd before the start: the card's free space, -1 unknown
  uint32_t adoptUntil_ = 0;
  const char* adoptMsg_ = "NO SYNTH";  // the toast when no take came of it  // Sd start with no reply: polls adopt the take the board may have armed
  mt::RecPeakHold hold_;
  uint32_t nextPoll_ = 0;
  bool open_ = false;
};

}  // namespace ui
