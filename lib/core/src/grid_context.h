#pragma once
#include "edit_ops.h"
namespace mt {
struct GridContext {
  int pattern = 0, track = 0, step = 0, length = kDefaultSteps;
  bool selected = false;
  Sel selection{};
  Sel editScope() const { return selected ? selection : makeSel(track,step,track,step); }
  Sel processScope() const { return selected ? selection : trackScope(); }
  Sel trackScope() const { return makeSel(track,0,track,length-1); }
};
}
