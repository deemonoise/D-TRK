#pragma once
#include <stdint.h>

namespace mt {
struct Project;
}

// The sound runs on the synth board; this is its client over the link (src/link): events, previews,
// the sound state mirror and the figures the synth reports in its Status frames.
namespace audio {

constexpr int kRate = 44100;
constexpr int kBlock = 128;  // samples, 2.9 ms

// Starts the link; p is the project whose SynthModel is mirrored to the synth board.
void begin(mt::Project* p);
// UI task, every frame: sends the changed parts of the sound state (StateSet), takes their replies,
// re-sends everything when the synth board rebooted.
void pump();
// The synth board has the current sound state (nothing dirty, nothing in flight).
bool synced();
// The project whose SynthModel is mirrored (nullptr before begin).
const mt::Project* mirrorProject();

// The synth board answers (Hello seen, a Status within the last second).
bool synthUp();
// It speaks our protocol with the same SynthModel layout (else nothing is mirrored to it).
bool versionOk();
const char* synthFw();     // "-" before the first Hello
uint16_t synthProtocol();  // 0 before the first Hello
// Link health since boot (PROJ -> SYS): events lost / late on the synth, frames with a bad CRC here,
// request retries, events this side could not queue.
struct LinkCounters {
  uint32_t lost, late, crcErrors, retries, dropped;
};
LinkCounters linkCounters();

// CPU profile (PROJ -> SYS): the synth board's render time per stage (mt::Synth::ProfStage) while
// running. UI task, blocking (a link request each). profileStart leaves it off when the board does
// not answer.
constexpr int kProfStages = 11;
struct Profile {
  uint32_t blocks;        // blocks rendered while it ran
  float us[kProfStages];  // average per block, us
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
// Synth load since the previous call (status bar CPU readout), from its Status frames: block time
// summed and longest, us, block count, and stalls (an audible gap). blocks == 0: no Status came.
// UI task only.
struct Load {
  uint32_t sumUs, blocks, peakUs, stalls;
};
Load takeLoad();

// Output scope (MIX tab): the last n (<= kScopeLen) samples of the synth's output, oldest first
// (sent at half the rate and 8 bits, each repeated twice), and the peak since the last call
// (0..32767; clip = 32767). Only while the meters are on.
constexpr int kScopeLen = 512;
void scopeRead(int16_t* out, int n);
int16_t scopePeak();
// Level meters (MIX): each pattern track's peak since the last call (1.0 = full scale at MAIN 100 %).
void trackPeaks(float out[16]);
void setMeters(bool on);  // MIX shown: measure them (costs the synth some render time)
// Phones: live output level 0..100 % (attenuation only), applied on the synth board after the scope.
// A device setting: Render WAV and the project's master do not see it.
void setPhones(uint8_t pct);

// Prints the synth's Log lines and the link counters now and then. UI task.
void pollLog();

}  // namespace audio
