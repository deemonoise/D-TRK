#pragma once
#include <cstdint>
namespace mt {
enum class BankResult : uint8_t;
struct ImportOut;
class Synth;
class SampleBank;
struct BankDisk;
class RecCapture;
}  // namespace mt

namespace brick {
// The host supplies exactly 44100 Hz signed 16-bit interleaved stereo to the device.
// Call audio::begin and engine::begin with the same project before the first render.
void render(int16_t* stereo, int frames);
mt::Synth& synth();
mt::SampleBank& bank();
mt::BankDisk& disk();
mt::BankResult importRecording(const char* path, mt::ImportOut& out, void (*cb)(uint32_t, uint32_t, void*),
                               void* ctx);
void setCapture(mt::RecCapture* capture);
// Input capture from an SDL device at 44100 Hz, stereo signed 16-bit.
void monitorInput(bool on);
void captureInput(const int16_t* stereo, int frames);
}  // namespace brick
