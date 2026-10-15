#pragma once
#include <stdint.h>

namespace hw {

// INA226 in the battery positive wire (LiPo+ -> IN+, IN- -> IP5306 B+), I2C 0x40..0x4F on the
// expander bus. Read by the trackio task, the only owner of Wire; readers on any core.
struct BatterySample {
  float volts;   // bus voltage at IN+ (battery terminal)
  float amps;    // + discharge, - charging
  uint32_t ms;   // millis() of the read
};

// INA226 answered at boot.
bool batteryPresent();
// Last sample, consistent across cores. False until the first successful read.
bool batteryLast(BatterySample& out);

// For trackio (owner of Wire): batteryProbe after Wire.begin, then batteryPoll from its
// task every loop; cheap when a read is not due.
bool batteryProbe();
void batteryPoll(uint32_t nowMs);

}  // namespace hw
