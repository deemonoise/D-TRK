#pragma once

namespace ui {

// One geometry for drawing and touch. A negative focus keeps the original equal columns.
struct GridOverviewLayout {
  static constexpr int kWidth = 480, kGutter = 32, kTracks = 8, kField = 32;
  static constexpr int kFocused = 4 * kField;
  int focus;

  int x(int column) const {
    if (focus < 0) return kGutter + column * (kWidth - kGutter) / kTracks;
    const bool after = column > focus;
    const int narrow = column - (after ? 1 : 0);
    return kGutter + narrow * (kWidth - kGutter - kFocused) / (kTracks - 1) + (after ? kFocused : 0);
  }
  int width(int column) const { return x(column + 1) - x(column); }
  int columnAt(int px) const {
    if (px < kGutter || px >= kWidth) return -1;
    for (int col = 0; col < kTracks; ++col)
      if (px < x(col + 1)) return col;
    return -1;
  }
};

}  // namespace ui
