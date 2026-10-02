#include <Arduino.h>
#include <new>
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "model.h"
#include "storage/storage.h"
#include "ui/app.h"

static LGFX lcd;
static mt::Project* project;
static ui::App app;

// Simple beat so Play produces something right after flashing.
static void loadDemo(mt::Project& p) {
  mt::Pattern& pat = p.patterns[0];
  for (int i = 0; i < 16; i += 4) pat.steps[0][i].note = 36;
  pat.steps[1][4].note = 38;
  pat.steps[1][12].note = 38;
  for (int i = 2; i < 16; i += 4) pat.steps[2][i].note = 42;
}

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
  if (!hw::sdBegin() || !storage::autoload(*project, &fromBak, &autoErr)) loadDemo(*project);

  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(200);

  hw::inputBegin();
  engine::begin(project);
  app.begin(&lcd, project);
  engine::post(engine::Cmd::ChainEdit, static_cast<uint16_t>(mt::ChainOp::Edit));  // song position display
  if (fromBak) app.toast("LOADED BACKUP");
  if (autoErr != storage::Result::Ok) {
    static char msg[40];
    snprintf(msg, sizeof(msg), "AUTOLOAD: %s", storage::resultText(autoErr));
    app.toast(msg);
  }
}

void loop() {
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) app.onInput(ev);
  app.tick();
  vTaskDelay(1);
}
