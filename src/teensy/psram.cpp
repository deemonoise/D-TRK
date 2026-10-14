#include "psram.h"
#include <Arduino.h>
#include <Audio.h>
#include "link_server.h"
#include "model.h"
#include "psram_cache.h"
#include "stream.h"
#include "synth.h"

extern "C" uint8_t external_psram_size;  // startup.c: 0 = nothing found on CS0

namespace psram {

namespace {

uint32_t* const chip = reinterpret_cast<uint32_t*>(0x70000000u);
constexpr uint32_t kPerfFrames = kPerfSec * static_cast<uint32_t>(mt::kSynthRate);

class Hw final : public mt::PsramHw {
 public:
  volatile bool fault = false;
  void flush(const void* p, uint32_t n) override { arm_dcache_flush_delete(const_cast<void*>(p), n); }
  void drop(const void* p, uint32_t n) override {
    arm_dcache_delete(const_cast<void*>(p), n);
    if (fault) {  // desk: the chip "lost" a bit of what was just written
      fault = false;
      *static_cast<volatile uint32_t*>(const_cast<void*>(p)) ^= 1;
      arm_dcache_flush_delete(const_cast<void*>(p), 32);
    }
  }
} hw;

State st = State::Off;
bool held = false;
mt::PsramArena arena;
mt::PsramWatch watch;
mt::PsramFill* fill = nullptr;
mt::Synth* synth = nullptr;
int16_t* perf = nullptr;

}  // namespace

void begin(const mt::SynthModel& m, mt::Synth& s) {
  synth = &s;
#ifdef MT_NO_PSRAM
  link::log("psram off (build)");
  return;
#endif
  // Only the PJRC layout: startup.c probes the pad under the Teensy (CS0), sets FlexSPI2 up and maps it
  // from 0x70000000 (the MPU region, write-back, by configure_cache()). A chip on the second pad (CS1)
  // alone is not used: without one on CS0 the sample cache and the PERF buffer stay off.
  // Nothing else in the firmware uses EXTMEM / extmem_malloc: the arena takes the chip from its start.
  if (external_psram_size * 1024u * 1024u < mt::kPsramBytes) {
    link::log("psram off: no 8 MB chip on CS0 (startup found %u MB); samples from SD, PERF buffer off",
              external_psram_size);
    return;
  }
  const uint32_t t0 = micros();
  if (!mt::psramSelfTest(chip, mt::kPsramBytes, hw)) {
    link::log("psram off: self-test failed");
    return;
  }
  const uint32_t arenaBytes = mt::kPsramBytes - mt::kPsramGuardBytes;
  arena.init(chip, arenaBytes);
  // PERF first, before any copy: reset() keeps it, the copies take what follows.
  perf = static_cast<int16_t*>(arena.reserve(kPerfFrames * 2 * sizeof(int16_t)));
  watch.init(chip + arenaBytes / 4, hw);
  static mt::PsramFill f(stream::table(), stream::pool(), stream::reader(), arena, hw);
  fill = &f;
  stream::addBackground(fill);
  stream::pool().psramOk = true;
  st = State::On;
  link::log("psram ok: CS0, 8 MB used, self-test %lu us, cache %lu KB", static_cast<unsigned long>(micros() - t0),
            static_cast<unsigned long>(arena.size() / 1024));
  restart(m);
}

int16_t* perfBuffer(uint32_t& frames) {
  frames = st == State::On && perf ? kPerfFrames : 0;
  return frames ? perf : nullptr;
}

void step() {
  if (st != State::On || watch.step(millis())) return;
  stream::pool().psramOk = false;  // Synth drops PSRAM sources at the next block, the fill stops
  st = State::Failed;
  if (synth) {  // PERF stays silent until a reboot (the ESP sees Status::psram)
    AudioNoInterrupts();
    synth->setPerfBuffer(nullptr, 0, false);
    AudioInterrupts();
  }
  link::log("psram failed: the watchdog read back a wrong word; SD only, PERF off until a reboot");
}

void restart(const mt::SynthModel& m) {
  if (st != State::On) return;
  uint32_t crcs[sizeof m.samples / sizeof m.samples[0]];
  int n = 0;
  if (!held)
    for (int i = 0; i < m.sampleCount; ++i) crcs[n++] = m.samples[i].crc;
  fill->restart(crcs, n);
  link::log("psram: %d samples planned, %lu KB", fill->planned(),
            static_cast<unsigned long>(fill->plannedBytes() / 1024));
}

State state() { return st; }

uint8_t fillPct() {
  return fill ? fill->pct() : 100;
}

void fillCounts(int& ready, int& bad, int& planned) {
  ready = fill ? fill->ready() : 0;
  bad = fill ? fill->bad() : 0;
  planned = fill ? fill->planned() : 0;
}

void injectFault() { hw.fault = true; }

void hold(bool on) { held = on; }

}  // namespace psram
