// DRUM machines BDA .. HHR, listening renders: 2 s per case (16 bit, 44.1 kHz, mono) straight from
// Drum2Voice (no Synth: no envelope, filter or sends). Writes only with DRUM2_WAV=1, into
// DRUM2_WAV_DIR (default .pio/drum2_wav):
//   DRUM2_WAV=1 pio test -e native -f test_drum2_wav
// Otherwise the test passes and writes nothing.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unity.h>
#include <vector>
#include "synth_drum2.h"
#include "synth_osc.h"
#include "wav.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kFrames = 2 * kSynthRate;

bool enabled() {
  const char* e = getenv("DRUM2_WAV");
  return e && e[0] == '1';
}

const char* dir() {
  const char* d = getenv("DRUM2_WAV_DIR");
  return d && d[0] ? d : ".pio/drum2_wav";
}

std::vector<int16_t> render(Drum2Kind k, const float mac[5], int note, int vel) {
  Drum2Voice v{};
  v.trigger(k, vel / 127.f, false);
  Drum2Params p;
  p.hz = drum2BaseHz(k) * powf(2.f, (note - 60) / 12.f);
  for (int i = 0; i < 5; ++i) p.mac[i] = mac[i];
  std::vector<float> f(kFrames, 0.f);
  for (int i = 0; i < kFrames; i += 32) {
    v.control(p, 32);
    v.render(f.data() + i, 32, 1.f);
  }
  std::vector<int16_t> out(kFrames);
  for (int i = 0; i < kFrames; ++i) out[i] = static_cast<int16_t>(lrintf(f[i] * 32767.f));
  return out;
}

void writeWav(const char* name, const std::vector<int16_t>& d) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.wav", dir(), name);
  uint8_t h[kWavPcmHeaderBytes];
  wavHeaderPcm(h, static_cast<uint32_t>(d.size()), kSynthRate, 1, 16);
  FILE* f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  fwrite(h, 1, sizeof(h), f);
  fwrite(d.data(), sizeof(int16_t), d.size(), f);  // little-endian host
  fclose(f);
}

struct M {
  Drum2Kind k;
  const char* name;
  uint8_t def[5];
  bool tonal;
};
const M kMachines[] = {
    {Drum2Kind::Bda, "BDA", {80, 40, 50, 40, 0}, true},   {Drum2Kind::Sda, "SDA", {55, 64, 70, 0, 0}, true},
    {Drum2Kind::Bds, "BDS", {75, 50, 30, 60, 40}, true},  {Drum2Kind::Sds, "SDS", {55, 60, 70, 40, 0}, true},
    {Drum2Kind::Cpx, "CPX", {60, 64, 64, 64, 0}, false},  {Drum2Kind::Rsx, "RSX", {40, 64, 40, 0, 0}, true},
    {Drum2Kind::Hhr, "HHR", {30, 64, 30, 0, 0}, false},
};

}  // namespace

void test_render_drum2_wavs() {
  if (!enabled()) {
    TEST_PASS_MESSAGE("DRUM2_WAV != 1: nothing written");
    return;
  }
  mkdir(".pio", 0755);
  mkdir(dir(), 0755);
  char file[64];
  int n = 0;
  for (const M& m : kMachines) {
    float d[5], lo[5], hi[5];
    for (int i = 0; i < 5; ++i) d[i] = m.def[i] / 127.f, lo[i] = 0.2f, hi[i] = 0.9f;
    snprintf(file, sizeof(file), "%s_1_default", m.name);
    writeWav(file, render(m.k, d, 60, 127)), ++n;
    snprintf(file, sizeof(file), "%s_2_all020", m.name);
    writeWav(file, render(m.k, lo, 60, 127)), ++n;
    snprintf(file, sizeof(file), "%s_3_all090", m.name);
    writeWav(file, render(m.k, hi, 60, 127)), ++n;
    snprintf(file, sizeof(file), "%s_4_default_vel40", m.name);
    writeWav(file, render(m.k, d, 60, 40)), ++n;
    if (m.tonal)
      for (int note : {48, 72}) {
        snprintf(file, sizeof(file), "%s_5_default_n%d", m.name, note);
        writeWav(file, render(m.k, d, note, 127)), ++n;
      }
  }
  // CPX: all 16 patterns.
  for (int p = 0; p < 16; ++p) {
    float d[5];
    for (int i = 0; i < 5; ++i) d[i] = kMachines[4].def[i] / 127.f;
    d[4] = p / 15.f;
    snprintf(file, sizeof(file), "CPX_6_pattern%02d", p);
    writeWav(file, render(Drum2Kind::Cpx, d, 60, 127)), ++n;
  }
  printf("wrote %d files to %s\n", n, dir());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_render_drum2_wavs);
  return UNITY_END();
}
