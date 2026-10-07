#pragma once
#include <stdint.h>

namespace mt {
struct Project;
class Synth;
}

namespace audio {

constexpr int kRate = 32000;
constexpr int kBlock = 128;  // samples, 4 ms

// First thing at boot: takes the synth's internal RAM before anything else can fragment it.
void reserve();
void begin(mt::Project* p);
// The synth state ended up in internal RAM (false: PSRAM, every sample costs more).
bool synthInternal();
bool reverbInternal();  // the reverb buffer in internal RAM (else PSRAM)
// Wi-Fi needs the internal RAM: the reverb buffer moves to PSRAM while it runs and back after
// (when 24 KB stay free). Transport stopped; the reverb tail is lost.
void reverbToPsram();
void reverbToInternal();

// CPU profile (PROJ -> SYS): the render time per stage (mt::Synth::ProfStage) while running.
constexpr int kProfStages = 11;
struct Profile {
  uint32_t blocks;               // blocks rendered while it ran
  float us[kProfStages];         // average per block, us
};
void profileStart();
bool profileRunning();
// Stops it; false when no block was rendered.
bool profileStop(Profile& out);
// Event for an INT track, stamped with its scheduled engine::nowUs() time (at or before now). Called from the engine task only
// (lock-free single-producer queue); a full queue drops the event.
void post(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len);
// Plays note with instrument instr for 300 ms on the synth's preview track. UI task only.
void preview(uint8_t instr, uint8_t note);
// SAMPLE instrument instr: plays slice (whatever its slice mode) at root, held holdMs (up to 25 s)
// or until the next preview. UI task only.
void previewSlice(uint8_t instr, uint8_t slice, uint8_t root, uint32_t holdMs);
// Plays a mono buffer (rate <= kRate) once on top of the synth, at the level of a sample at full
// instrument volume. d must stay valid until previewStop() returns true. UI task only.
void previewBuffer(const int16_t* d, uint32_t frames, uint32_t rate);
// Stops the buffer preview. True once the audio task no longer reads the buffer; false (timed
// out): keep the buffer allocated.
bool previewStop();
bool previewPlaying();
// Block render load since the previous call (status bar CPU readout): total render time, block
// count and longest block, us, and stalls (the DMA ran dry: an audible gap; a single long block
// is covered by the queued DMA blocks); resets them. blocks == 0: nothing rendered. UI task only.
struct Load {
  uint32_t sumUs, blocks, peakUs, stalls;
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
// Audio task parked (silent, synth reset, queues flushed) for the object's lifetime; ok = parked.
// While parked the synth may be driven from the UI task (offline render) and the bank written.
struct Paused {
  bool ok;
  Paused() : ok(pauseForFlash()) {}
  ~Paused() {
    if (ok) resumeAfterFlash();
  }
  Paused(const Paused&) = delete;
  Paused& operator=(const Paused&) = delete;
};
// The live synth, for an offline render while Paused (reset it before and after).
mt::Synth* liveSynth();

}  // namespace audio
