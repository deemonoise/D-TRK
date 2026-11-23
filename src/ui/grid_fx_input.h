#pragma once
#include "model.h"
#include "hw/input.h"

namespace ui {
// These FX values select a named option or a packed musical pattern one entry at a time.
inline bool numericFxValue(mt::Fx fx) {
  using mt::Fx;
  switch (fx) {
    case Fx::CND: case Fx::CHD: case Fx::DTY: case Fx::LFW: case Fx::LFT:
    case Fx::RAT: case Fx::ARM: case Fx::ARS:
      return false;
    default: return true;
  }
}
inline bool coarseFxEdit(const hw::InputEvent& ev, [[maybe_unused]] mt::Fx fx) {
#ifdef DTRK_BRICK
  if (ev.axis != hw::InputAxis::Encoder && !numericFxValue(fx)) return false;
#endif
  return hw::coarseEdit(ev);
}
}  // namespace ui
