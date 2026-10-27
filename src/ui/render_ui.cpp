#include "render_ui.h"
#include "app.h"
#include "engine/engine.h"
#include "hw/sdcard.h"

namespace ui {

bool renderReady(App& app) {
  if (app.rec().busy()) {
    renderResultToast(app, storage::Result::RecBusy);
    return false;
  }
  const engine::Status st = engine::status();
  if (st.playing || st.paused) {  // the render resets the synth
    if (!storage::stopEngine()) {
      renderResultToast(app, storage::Result::EngineBusy);
      return false;
    }
    app.toast("STOPPED");
  }
  if (!hw::sdReady()) {
    renderResultToast(app, storage::Result::NoSd);
    return false;
  }
  return true;
}

void renderResultToast(App& app, storage::Result r) {
  using storage::Result;
  switch (r) {
    case Result::Ok:
    case Result::Capped: return;
    case Result::EngineBusy:
    case Result::AudioBusy: app.toast("BUSY"); return;
    case Result::NoSynth: app.toast("NO SYNTH"); return;
    case Result::DiskFull: app.toast("CARD FULL"); return;
    case Result::BankFull:
      app.toast(app.project().sampleCount >= mt::kProjSamples ? "SAMPLE LIST FULL" : "BANK FULL");
      return;
    case Result::WriteFail:
    case Result::ReadFail: hw::sdRecover(); break;
    default: break;
  }
  app.toast(storage::resultText(r));  // NO SD CARD, REC BUSY, SILENT, CANCELLED, NO MEMORY...
}

}  // namespace ui
