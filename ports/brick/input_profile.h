#pragma once
#include "ui_brick_controls.h"

namespace brick {
inline constexpr char kControllerName[] = "TRIMUI Brick Controller";
// KNULLI raw IDs. Volume, system hotkey and front F1/F2 are handled outside the tracker.
inline constexpr int kFirstButtonId = 4;
inline constexpr Button kButtons[] = {Button::B, Button::A, Button::X, Button::Y, Button::L1,
                                      Button::R1, Button::L2, Button::R2, Button::Select, Button::Start};
}  // namespace brick
