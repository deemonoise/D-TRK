#pragma once
#include <string.h>
#include "theme.h"

namespace ui {

// The row of page tabs under a screen header (as INST's): the current page highlighted, a tap picks
// the page under the finger.
struct PageBar {
  static constexpr int kH = 24;

  static int width(int n) { return n > 0 ? kScreenW / n : kScreenW; }
  static int at(int x, int n) {
    const int i = x / width(n);
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
  }
  static void draw(LGFX_Sprite& s, int y, const char* const* names, int n, int cur) {
    const int w = width(n), ty = y + (kH - 4 - kCharH) / 2;
    for (int i = 0; i < n; ++i) {
      const bool on = i == cur;
      s.fillRect(i * w + 1, y, w - 2, kH - 4, on ? kPlayBg : kBeatBg);
      s.setTextColor(on ? kCursor : kDim);
      s.drawString(names[i], i * w + (w - static_cast<int>(strlen(names[i])) * kCharW) / 2, ty);
    }
  }
};

}  // namespace ui
