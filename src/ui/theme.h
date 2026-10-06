#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"

namespace ui {

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kBeatBg = 0x10A2;
constexpr uint16_t kPlayBg = 0x3186;
constexpr uint16_t kCursor = 0xFFE0;
constexpr uint16_t kEditCursor = 0xF800;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kStatusBg = 0x18C3;
constexpr uint16_t kSelBg = 0x2945;
constexpr uint16_t kMenuBg = 0x2104;
constexpr uint16_t kMenuBorder = kCursor;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kCyan = 0x07FF;

constexpr int kScreenW = 480;
constexpr int kScreenH = 320;
constexpr int kStatusH = 24;
constexpr int kTabH = 24;
constexpr int kAreaY = kStatusH;
constexpr int kAreaH = kScreenH - kStatusH - kTabH;  // 272
constexpr int kTabY = kAreaY + kAreaH;               // 296
constexpr int kCharW = 8;
constexpr int kCharH = 16;

inline const lgfx::IFont* font() { return &fonts::AsciiFont8x16; }

}  // namespace ui
