#pragma once
#include "ui_brick_controls.h"

namespace rg35xxsp {
using brick::Button;
inline constexpr char kControllerName[] = "Anbernic RG35XX-SP Controller";
// KNULLI raw IDs. Directions arrive on hat 0. The system hotkey is ignored.
inline constexpr int kFirstButtonId = 3;
inline constexpr Button kButtons[] = {Button::A, Button::B, Button::Y, Button::X, Button::L1, Button::R1,
                                      Button::Select, Button::Start, Button::Count, Button::L2, Button::R2};
}  // namespace rg35xxsp
