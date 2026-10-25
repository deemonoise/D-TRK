#pragma once
#include <stdint.h>

// The Teensy Audio Shield's SGTL5000 (I2C on pins 18 / 19, address 0x0A): line out and the headphone
// amplifier. Without the shield (a plain I2S DAC) the Phones level stays a digital gain on the output.
namespace codec {

// setup(), after AudioMemory (the I2S clocks, MCLK included, already run): enables the chip if it
// answers. false: no shield.
bool begin();
bool present();
// Phones 0..100 %: the SGTL5000 headphone volume (the line out keeps its level).
void phones(uint8_t pct);

// Recording input (the ADC feeds AudioInputI2S): MIC (the shield's mic pad, mono on the left) or LINE
// IN. The ADC high-pass filter stays on (no DC offset in the takes). No-ops without the shield.
enum class Input : uint8_t { Mic, Line };
void input(Input in);
void micGain(int db);     // 0..63 dB (preamp steps + ADC gain), clamped
void lineLevel(int lvl);  // line-in level 0..15 (0 = 3.12 Vpp full scale, 15 = 0.24 Vpp), clamped

}  // namespace codec
