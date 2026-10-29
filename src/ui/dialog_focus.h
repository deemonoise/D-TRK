#pragma once
namespace ui {
// Parameters, then the primary action and Cancel; both actions remain reachable from either end.
inline int dialogNextFocus(int current, int delta, int rows) {
  const int count = (rows < 0 ? 0 : rows) + 2;
  return ((current % count + delta % count) % count + count) % count;
}
}
