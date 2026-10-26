// DRUM machines BD8 .. CY9 (DrumVoice), listening / analysis renders: 2 s per case (DRUM_WAV_SECS=5: 5 s;
// 16 bit, 44.1 kHz, mono) straight from the voice at amp 1 (no Synth: no envelope, filter or sends). Writes only with
// DRUM_WAV=1, into DRUM_WAV_DIR (default .pio/drum_wav); DRUM_WAV_ONLY=BD9 renders one machine:
//   DRUM_WAV=1 DRUM_WAV_ONLY=BD9 pio test -e native -f test_drum_wav
// Cases: the machine's default macros, then each macro at 0 and 127 with the others at default.
// DRUM_WAV_GRID=1 adds a knob grid as the 909 kick sample names: <name>_T<t>A<a>D<d>, TIME (t), COLOR
// (a) and DECAY (d) at 0, 3, 7, 10 tenths (A at 0 / 10), the rest at default.
// DRUM_WAV_SNARE=1 adds a grid as the 909 snare sample names: <name>_ST<t>T<o>S<s>, COLOR (t: TUNE on
// SD9), CONTOUR (o: N.DEC, the 909's Tone) and SHAPE (s: SNAPPY) at 0, 3, 7, 10 tenths.
// DRUM_WAV_TOM=1 adds the drums as the tom sample names: TOM9_<LT|MT|HT><t>D<d>, COLOR (t: TUNE) and
// DECAY (d) at 0, 3, 7, A (10) tenths, the note for LT / MT / HT; TOM8_<LT|MT|HT|LC|MC|HC> at default
// macros, the note for each drum's pitch.
// DRUM_WAV_PERC=1 adds 8 more hits at default macros, <name>_s<k> (k 1 .. 8, each its own noise), for
// averaging the analysis of the noisy ones (claps).
// DRUM_WAV_HATS=1 adds DECAY steps for the hats and cymbals, <name>_D<v> (v 0, 13, 25 .. 127, the rest at
// default), and for CY9 TUNE at 0, 2, 4, 6, 8, 10 tenths as the 909 sample names: CY9_CSHD<t> (crash,
// RIDE/CR 127) and CY9_RIDED<t> (ride, 0), t 0 .. 8 or A.
// Otherwise the test passes and writes nothing.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unity.h>
#include <vector>
#include "model.h"
#include "synth_drum_machines.h"
#include "synth_osc.h"
#include "wav.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

int gFrames = 2 * kSynthRate;

const char* env(const char* k) {
  const char* e = getenv(k);
  return e && e[0] ? e : nullptr;
}

const char* dir() { return env("DRUM_WAV_DIR") ? env("DRUM_WAV_DIR") : ".pio/drum_wav"; }

std::vector<int16_t> render(uint8_t machine, const float mac[kFmMacros], float note) {
  DrumParams p;
  drumMachine(machine, mac, note, p);
  DrumVoice v;
  v.trigger(false);
  std::vector<float> f(gFrames, 0.f);
  for (int i = 0; i + 32 <= gFrames; i += 32) {
    v.control(p, 32);
    v.render(f.data() + i, 32, 1.f);
  }
  std::vector<int16_t> out(gFrames);
  for (int i = 0; i < gFrames; ++i) {
    const float x = f[i] > 1.f ? 1.f : (f[i] < -1.f ? -1.f : f[i]);
    out[i] = static_cast<int16_t>(lrintf(x * 32767.f));
  }
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

}  // namespace

void test_render_drum_wavs() {
  if (!env("DRUM_WAV") || env("DRUM_WAV")[0] != '1') {
    TEST_PASS_MESSAGE("DRUM_WAV != 1: nothing written");
    return;
  }
  if (env("DRUM_WAV_SECS")) gFrames = (atoi(env("DRUM_WAV_SECS")) > 0 ? atoi(env("DRUM_WAV_SECS")) : 2) * kSynthRate;
  mkdir(".pio", 0755);
  mkdir(dir(), 0755);
  const char* only = env("DRUM_WAV_ONLY");
  char file[96];
  int n = 0;
  for (int mc = 0; mc < kDrum2First; ++mc) {
    const uint8_t m = static_cast<uint8_t>(mc);
    const char* name = drumMachineName(m);
    if (only && strcmp(only, name) != 0) continue;
    Instrument in;
    instrSetType(in, InstrType::Drum);
    drumSetMachine(in, m);
    float d[kFmMacros];
    for (int k = 0; k < kFmMacros; ++k) d[k] = in.macro[k];
    snprintf(file, sizeof(file), "%s_default", name);
    writeWav(file, render(m, d, 60)), ++n;
    for (int k = 0; k < kFmMacros; ++k) {
      if (!drumMacroName(m, k)[0]) continue;
      for (int e : {0, 127}) {
        float x[kFmMacros];
        memcpy(x, d, sizeof x);
        x[k] = static_cast<float>(e);
        snprintf(file, sizeof(file), "%s_%s%d", name, drumMacroName(m, k), e);
        for (char* c = file; *c; ++c)
          if (*c == '/' || *c == '.') *c = '_';
        writeWav(file, render(m, x, 60)), ++n;
      }
    }
    if (env("DRUM_WAV_GRID") && env("DRUM_WAV_GRID")[0] == '1')
      for (int t : {0, 3, 7, 10})
        for (int a : {0, 10})
          for (int dd : {0, 3, 7, 10}) {
            float x[kFmMacros];
            memcpy(x, d, sizeof x);
            x[kMacCon] = t * 12.7f;
            x[kMacCol] = a * 12.7f;
            x[kMacDec] = dd * 12.7f;
            snprintf(file, sizeof(file), "%s_T%dA%dD%d", name, t, a, dd);
            writeWav(file, render(m, x, 60)), ++n;
          }
    if (env("DRUM_WAV_SNARE") && env("DRUM_WAV_SNARE")[0] == '1')
      for (int t : {0, 3, 7, 10})
        for (int o : {0, 3, 7, 10})
          for (int sn : {0, 3, 7, 10}) {
            float x[kFmMacros];
            memcpy(x, d, sizeof x);
            x[kMacCol] = t * 12.7f;
            x[kMacCon] = o * 12.7f;
            x[kMacShp] = sn * 12.7f;
            snprintf(file, sizeof(file), "%s_ST%dT%dS%d", name, t, o, sn);
            writeWav(file, render(m, x, 60)), ++n;
          }
    if (env("DRUM_WAV_PERC") && env("DRUM_WAV_PERC")[0] == '1')
      for (int k = 1; k <= 8; ++k) {
        snprintf(file, sizeof(file), "%s_s%d", name, k);
        writeWav(file, render(m, d, 60)), ++n;
      }
    const bool hat = m == static_cast<uint8_t>(DrumMachine::Hh8) || m == static_cast<uint8_t>(DrumMachine::Hh9) ||
                     m == static_cast<uint8_t>(DrumMachine::Cy8) || m == static_cast<uint8_t>(DrumMachine::Cy9);
    if (hat && env("DRUM_WAV_HATS") && env("DRUM_WAV_HATS")[0] == '1') {
      for (int v = 0; v <= 127; v = v + 13 > 127 && v < 127 ? 127 : v + 13) {
        float x[kFmMacros];
        memcpy(x, d, sizeof x);
        x[kMacDec] = static_cast<float>(v);
        snprintf(file, sizeof(file), "%s_D%d", name, v);
        writeWav(file, render(m, x, 60)), ++n;
      }
      if (m == static_cast<uint8_t>(DrumMachine::Cy9))
        for (int cr = 0; cr < 2; ++cr)
          for (int t = 0; t <= 10; t += 2) {
            float x[kFmMacros];
            memcpy(x, d, sizeof x);
            x[kMacCol] = t * 12.7f;
            x[kMacShp] = cr ? 127.f : 0.f;
            snprintf(file, sizeof(file), "%s_%s%c", name, cr ? "CSHD" : "RIDED", t == 10 ? 'A' : '0' + t);
            writeWav(file, render(m, x, 60)), ++n;
          }
    }
    if (env("DRUM_WAV_TOM") && env("DRUM_WAV_TOM")[0] == '1') {
      if (m == static_cast<uint8_t>(DrumMachine::Tom9)) {
        const char* drum[3] = {"LT", "MT", "HT"};
        const float hz[3] = {63, 81, 94};  // base at TUNE 0; MT = note 60
        for (int k = 0; k < 3; ++k)
          for (int t : {0, 3, 7, 10})
            for (int dd : {0, 3, 7, 10}) {
              float x[kFmMacros];
              memcpy(x, d, sizeof x);
              x[kMacCol] = t * 12.7f;
              x[kMacDec] = dd * 12.7f;
              snprintf(file, sizeof(file), "%s_%s%cD%c", name, drum[k], t == 10 ? 'A' : '0' + t,
                       dd == 10 ? 'A' : '0' + dd);
              writeWav(file, render(m, x, 60.f + 12.f * log2f(hz[k] / 81.f))), ++n;
            }
      }
      if (m == static_cast<uint8_t>(DrumMachine::Tom8)) {
        const char* drum[6] = {"LT", "MT", "HT", "LC", "MC", "HC"};
        const float hz[6] = {86, 127, 171, 191, 264, 367};  // MT = note 60
        for (int k = 0; k < 6; ++k) {
          snprintf(file, sizeof(file), "%s_%s", name, drum[k]);
          writeWav(file, render(m, d, 60.f + 12.f * log2f(hz[k] / 127.f))), ++n;
        }
      }
    }
  }
  printf("wrote %d files to %s\n", n, dir());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_render_drum_wavs);
  return UNITY_END();
}
