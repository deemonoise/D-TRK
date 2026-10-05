#pragma once
#include <stdint.h>

namespace mt {
struct Project;
}

namespace audio {

constexpr int kRate = 32000;
constexpr int kBlock = 128;  // samples, 4 ms

void begin(mt::Project* p);
// Event for an INT track, stamped with its scheduled engine::nowUs() time (at or before now). Called from the engine task only
// (lock-free single-producer queue); a full queue drops the event.
void post(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len);
// Plays note with instrument instr for 300 ms on the synth's preview track. UI task only.
void preview(uint8_t instr, uint8_t note);
// Plays a mono buffer (rate <= kRate) once on top of the synth, at the level of a sample at full
// instrument volume. d must stay valid until previewStop() returns true. UI task only.
void previewBuffer(const int16_t* d, uint32_t frames, uint32_t rate);
// Stops the buffer preview. True once the audio task no longer reads the buffer; false (timed
// out): keep the buffer allocated.
bool previewStop();
bool previewPlaying();
// Block render load since the previous call (status bar CPU readout): total render time, block
// count and longest block, us; resets them. blocks == 0: nothing rendered. UI task only.
struct Load {
  uint32_t sumUs, blocks, peakUs;
};
Load takeLoad();
// Prints the audio task's counters (lost / late events, bench) now and then. UI task: the audio
// task itself never prints (USB CDC writes may block it).
void pollLog();
// Sample bank partition mapped into the data address space, or nullptr (bank.cpp).
const uint8_t* samplesBase();
uint32_t samplesSize();
// Flash writes to the bank (bank.cpp, UI task): pause parks the audio task with the I2S buffers
// silent, so nothing reads mapped flash meanwhile. False: the task did not park in time.
bool pauseForFlash();
void resumeAfterFlash();

}  // namespace audio
