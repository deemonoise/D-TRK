#pragma once
#include "model.h"
namespace mt {
constexpr int kAutomationParams = 17;
Fx automationFx(int index);
bool automationCommand(Fx fx);
void automationStrip(Step& step);
int automationSlotsUsed(const Step& step);
int automationValue(const TrackCfg &t, const Instrument &i, Fx fx);
int automationGet(const Step &step, Fx fx);
bool automationSet(Step &step, Fx fx, uint8_t value);
void automationClear(Step &step, Fx fx);
bool automationRamp(Step *steps, int len, Fx fx, int from, int to, uint8_t a,
                    uint8_t b);
struct AutomationClip {
  int len = 0;
  int16_t value[kMaxSteps]{};
};
void automationCopy(const Step *steps, int len, Fx fx, AutomationClip &clip);
bool automationPaste(Step *steps, int len, Fx fx, const AutomationClip &clip);
// Values touched in the current recording pass; unmodified parameters remain
// absent.
struct MotionLatch {
  int16_t value[kAutomationParams];
  MotionLatch() { clear(); }
  void clear();
  void beginPass(uint32_t pass);
  void touch(const int16_t *before, const int16_t *after);
  bool write(Step &step) const; // atomic, false if the touched locks do not fit
 private:
  void clearValues();
  uint32_t pass_ = 0;
  bool hasPass_ = false;
};
int automationStep(const Pattern &p, int track, int pos, uint32_t loop,
                   uint8_t phase);
} // namespace mt
