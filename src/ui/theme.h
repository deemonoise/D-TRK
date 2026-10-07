#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"

namespace ui {

// Colour theme (RGB565). The k... colours below are references into the current theme, so a theme
// change takes effect on the next redraw; read them when drawing, never copy them into statics.
struct Theme {
  const char* name;  // up to 9 characters
  uint16_t bg;        // screen background
  uint16_t beatBg;    // beat rows, headers, inactive page tabs
  uint16_t playBg;    // playing row, active tab / page
  uint16_t statusBg;  // status bar and tab bar
  uint16_t selBg;     // selection
  uint16_t menuBg;    // menus and dialogs
  uint16_t text;
  uint16_t dim;       // labels, inactive, grey items
  uint16_t cursor;    // cursor, highlight, menu border
  uint16_t edit;      // cursor while editing
  uint16_t red, green, yellow, cyan;  // states: rec / clip / mute, ok, solo / fill / warning, markers
  uint16_t keyWhite, keyBlack;        // the lane pad / keyboard keys
};

extern Theme gTheme;  // the current one

int themeCount();
const Theme& themeAt(int i);  // i clamped to 0..themeCount() - 1
void applyTheme(int i);       // copies theme i into gTheme

inline const uint16_t& kBg = gTheme.bg;
inline const uint16_t& kBeatBg = gTheme.beatBg;
inline const uint16_t& kPlayBg = gTheme.playBg;
inline const uint16_t& kCursor = gTheme.cursor;
inline const uint16_t& kEditCursor = gTheme.edit;
inline const uint16_t& kText = gTheme.text;
inline const uint16_t& kDim = gTheme.dim;
inline const uint16_t& kStatusBg = gTheme.statusBg;
inline const uint16_t& kSelBg = gTheme.selBg;
inline const uint16_t& kMenuBg = gTheme.menuBg;
inline const uint16_t& kMenuBorder = gTheme.cursor;
inline const uint16_t& kRed = gTheme.red;
inline const uint16_t& kGreen = gTheme.green;
inline const uint16_t& kYellow = gTheme.yellow;
inline const uint16_t& kCyan = gTheme.cyan;
inline const uint16_t& kKeyWhite = gTheme.keyWhite;
inline const uint16_t& kKeyBlack = gTheme.keyBlack;

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
