#include <Arduino.h>
#include <new>
#include "audio/audio.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/pins.h"
#include "hw/sdcard.h"
#include "hw/trackio.h"
#include "model.h"
#include "storage/storage.h"
#include "ui/app.h"

static LGFX lcd;
static mt::Project* project;
static ui::App app;

// Simple 808 beat so Play produces something right after flashing. DRUM machines play their own
// pitch at C-4.
static void loadDemo(mt::Project& p) {
  static const mt::DrumMachine kKit[] = {mt::DrumMachine::Bd8, mt::DrumMachine::Sd8, mt::DrumMachine::Hh8};
  for (int t = 0; t < 3; ++t) {
    mt::instrSetType(p.instruments[t], mt::InstrType::Drum);
    mt::drumSetMachine(p.instruments[t], static_cast<uint8_t>(kKit[t]));
  }
  mt::Pattern& pat = p.patterns[0];
  for (int i = 0; i < 16; i += 4) pat.steps[0][i].note = 60;
  pat.steps[1][4].note = 60;
  pat.steps[1][12].note = 60;
  for (int i = 2; i < 16; i += 4) pat.steps[2][i].note = 60;
}

#ifdef WT_BENCH
namespace audio {
void wtBench();
}
#endif

void setup() {
  Serial.begin(115200);
  void* mem = heap_caps_malloc(sizeof(mt::Project), MALLOC_CAP_SPIRAM);
  if (!mem) mem = heap_caps_malloc(sizeof(mt::Project), MALLOC_CAP_8BIT);
  if (!mem) {
    Serial.println("FATAL: no memory for Project");
    for (;;) vTaskDelay(1000);
  }
  project = new (mem) mt::Project();
  // Engine not running yet: load straight into the live project.
  bool fromBak = false;
  storage::Result autoErr = storage::Result::Ok;
  const bool loaded = hw::sdBegin() && storage::autoload(*project, &fromBak, &autoErr);
  if (!loaded) loadDemo(*project);

  lcd.init();
  lcd.setRotation(1);
  pinMode(pins::kLcdBacklight, OUTPUT);
  digitalWrite(pins::kLcdBacklight, HIGH);

  hw::inputBegin();
  hw::trackioBegin();
  engine::begin(project);
  audio::begin(project);
  app.begin(&lcd, project);
  engine::post(engine::Cmd::ChainEdit, static_cast<uint16_t>(mt::ChainOp::Edit));  // song position display
  // Bank mounted by audio::begin, engine not playing: bring in the samples the cache lacks.
  int missing = 0;
  const bool folderFail = loaded && storage::pullSamples(*project, &missing, ui::App::syncProgress, &app) ==
                                        storage::Result::SamplesNotSaved;
  // One toast: autoload error (nothing loaded), or backup / missing samples / folder not written.
  if (autoErr != storage::Result::Ok) {
    char msg[48];
    snprintf(msg, sizeof(msg), "AUTOLOAD: %s", storage::resultText(autoErr));
    app.toast(msg);
  } else if (fromBak || missing > 0 || folderFail) {
    app.loadedToast(fromBak ? "LOADED BACKUP" : nullptr, missing, folderFail);
  }
#ifdef WT_BENCH
  audio::wtBench();
  app.toast("WT BENCH: /midi/bench.mid");
#endif
}

void loop() {
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) app.onInput(ev);
  app.tick();
  audio::pollLog();
  vTaskDelay(1);
}
