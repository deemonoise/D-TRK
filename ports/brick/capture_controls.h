#pragma once

namespace brick {
// Only the launcher that leased KNULLI's built-in audiocodec supplies DTRK_BRICK_ADC.
// The recorder opens the capture path while a MIC/LINE meter or take is active.
bool acquireCaptureControls(bool microphone);
void releaseCaptureControls();
// For the directly leased Brick microphone, the saved gain's midpoint is unity.
int microphoneGainDb(int gain);
}  // namespace brick
