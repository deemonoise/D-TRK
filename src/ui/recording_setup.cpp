#include "app.h"
#include <stdio.h>
namespace ui {
bool App::recordPermit() const {
  const auto st = engine::status();
  return st.playing && st.recordActive && !st.countIn && st.pattern == editPattern();
}
void App::syncRecordArm() {
  if (!recordBatch_) engine::post(engine::Cmd::PracticeArm, grid_.notesArmed() || motionRec_ ? 1 : 0);
}
void App::setRecording(bool notes, bool motion) {
  recordBatch_ = true;
  grid_.setNoteRecording(notes);
  setMotionRecording(motion);
  recordBatch_ = false;
  syncRecordArm();
}
void App::setPracticePrefs(mt::PracticePrefs prefs) {
  practicePrefs_ = mt::practicePrefs(mt::practiceBits(prefs), prefs.clickLevel);
  engine::post(engine::Cmd::PracticeSettings, mt::practiceBits(practicePrefs_), practicePrefs_.clickLevel);
  settingsChanged();
  invalidate();
}
void App::openRecordingSetup(bool child, bool refresh) {
  char count[28], click[28], length[28], level[28];
  static const char* const modes[] = {"OFF", "REC", "PLAY+REC"};
  snprintf(count,sizeof(count),"Count-in: %s",practicePrefs_.countBars == 0 ? "OFF" : practicePrefs_.countBars == 1 ? "1 BAR" : "2 BARS");
  snprintf(click,sizeof(click),"Metronome: %s",modes[practicePrefs_.clickMode]);
  snprintf(length,sizeof(length),"Record: %s",practicePrefs_.onePass ? "ONE PASS" : "LOOP");
  snprintf(level,sizeof(level),"Click volume: %u",practicePrefs_.clickLevel);
  const MenuItem items[] = {{count,0},{click,1},{level,2},{length,3}};
  auto cb = [this](int id) {
    if (id == 0) {
      const MenuItem choices[] = {{"OFF",0},{"1 BAR (4 BEATS)",1},{"2 BARS (8 BEATS)",2}};
      menu_.openChild("COUNT-IN",choices,3,[this](int n) {
        auto p = practicePrefs_; p.countBars = n; setPracticePrefs(p); menu_.back(); openRecordingSetup(false,true);
      });
    } else if (id == 1) {
      const MenuItem choices[] = {{"OFF",0},{"REC ONLY",1},{"PLAY + REC",2}};
      menu_.openChild("METRONOME",choices,3,[this](int n) {
        auto p = practicePrefs_; p.clickMode = n; setPracticePrefs(p); menu_.back(); openRecordingSetup(false,true);
      });
    } else if (id == 2) {
      const MenuItem choices[] = {{"MUTE",0},{"LOW (32)",32},{"MEDIUM (64)",64},{"HIGH (96)",96},{"MAX (127)",127}};
      menu_.openChild("CLICK VOLUME",choices,5,[this](int n) {
        auto p = practicePrefs_; p.clickLevel = n; setPracticePrefs(p); menu_.back(); openRecordingSetup(false,true);
      });
    } else {
      const MenuItem choices[] = {{"LOOP",0},{"ONE PASS",1}};
      menu_.openChild("RECORD LENGTH",choices,2,[this](int n) {
        auto p = practicePrefs_; p.onePass = n != 0; setPracticePrefs(p); menu_.back(); openRecordingSetup(false,true);
      });
    }
  };
  if (refresh) menu_.replace("RECORDING SETUP",items,4,cb);
  else if (child) menu_.openChild("RECORDING SETUP",items,4,cb);
  else menu_.open("RECORDING SETUP",items,4,cb);
}
}
