#pragma once
#include "storage/storage.h"

namespace ui {

class App;

// Before any render on the synth board (Resample, Export, one-shot, KIT bake): a REC take refuses it
// (REC BUSY), a playing / paused transport is stopped ("STOPPED"), the card must be there. False: the
// reason toasted.
bool renderReady(App& app);
// The one toast for a failed render (the same texts for all): BUSY, NO SYNTH, CARD FULL, REC BUSY,
// SILENT, SAMPLE LIST FULL / BANK FULL...; a card read / write failure: hw::sdRecover() first. Ok,
// Capped: nothing (the caller says what it made).
void renderResultToast(App& app, storage::Result r);

}  // namespace ui
