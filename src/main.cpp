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
#include "storage/crashlog.h"
#include "storage/storage.h"
#include "ui/app.h"

static constexpr uint32_t kSynthWaitMs = 3000;  // boot: wait for the synth board at most this long

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

#ifdef LINK_ECHO_TEST
namespace slink {
void echoTest();
}
#endif

void setup() {
  Serial.begin(115200);
#ifdef LINK_ECHO_TEST
  delay(2000);
  slink::echoTest();
#endif
  // Before anything else: the sequencer needs a large block of internal RAM; the link, the screen
  // and Wi-Fi fragment it later.
  engine::reserve();
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
  // Safe boot: Shift held at power-on skips the autoload (a project that crashes the device on load).
  // Shift is on the expander: trackioBegin reads the port before returning.
  hw::trackioBegin();
  const bool safeBoot = hw::expanderDown(pins::kShiftBit);

  lcd.init();
  lcd.setRotation(1);
  pinMode(pins::kLcdBacklight, OUTPUT);
  digitalWrite(pins::kLcdBacklight, HIGH);

  // The card is on the synth board: start the link and give the board a moment to answer.
  audio::begin(project);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Synth: connecting...", 16, 16);
  for (const uint32_t t0 = millis(); !audio::synthUp() && millis() - t0 < kSynthWaitMs;) delay(20);
  if (!audio::synthUp()) lcd.drawString("Synth: no answer, going on", 16, 40);

  const bool sd = hw::sdBegin();
  storage::logBoot();  // a crash / watchdog / brownout restart goes into /diag/crashlog.txt (now or later)
  const bool loaded = sd && !safeBoot && storage::autoload(*project, &fromBak, &autoErr);
  if (!loaded) loadDemo(*project);

  hw::inputBegin();
  engine::begin(project);
  app.begin(&lcd, project);
  engine::post(engine::Cmd::ChainEdit, static_cast<uint16_t>(mt::ChainOp::Edit));  // song position display
  // Engine not playing: the synth board brings the samples its bank lacks in from the project's folder
  // (BankSync). It may still be booting (its first boot builds the built-in wavetables): wait a while.
  int missing = 0;
  if (loaded && (project->sampleCount > 0 || project->wavetableCount > 0)) {
    const uint32_t t0 = millis();
    while (!audio::synthUp() && millis() - t0 < 8000) {
      audio::pump();
      delay(20);
    }
  }
  const bool folderFail = loaded && storage::pullSamples(*project, &missing, ui::App::syncProgress, &app) ==
                                        storage::Result::SamplesNotSaved;
  // One toast: autoload error (nothing loaded), or backup / missing samples / folder not written.
  if (safeBoot) {
    app.toast("SAFE BOOT: NOTHING LOADED");
  } else if (autoErr != storage::Result::Ok) {
    char msg[48];
    snprintf(msg, sizeof(msg), "AUTOLOAD: %s", storage::resultText(autoErr));
    app.toast(msg);
  } else if (fromBak || missing > 0 || folderFail) {
    app.loadedToast(fromBak ? "LOADED BACKUP" : nullptr, missing, folderFail);
  }
}

void loop() {
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) app.onInput(ev);
  app.tick();
  audio::pollLog();
  static uint32_t logCheckMs = 0;
  if (millis() - logCheckMs >= 1000) {  // a crash record held back for the card
    logCheckMs = millis();
    storage::flushBootLog();
  }
  vTaskDelay(1);
}
