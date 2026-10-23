// LOFI listening renders: ~2 s per case into .pio/lofi_wav/*.wav (16 bit, 32 kHz, mono), through the
// real Synth (factory preset on track 1: envelope, filter, sends apply). Writes only with LOFI_WAV=1:
//   LOFI_WAV=1 pio test -e native -f test_lofi_wav
// Otherwise the test passes and writes nothing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unity.h>
#include <vector>
#include "presets_factory.h"
#include "sample_set.h"
#include "synth.h"
#include "wav.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr const char* kDir = ".pio/lofi_wav";
constexpr int kBlocks = 2 * kSynthRate / Synth::kBlock;  // ~2 s

bool enabled() {
  const char* e = getenv("LOFI_WAV");
  return e && e[0] == '1';
}

int findPreset(const char* name) {
  for (int i = 0; i < factoryCount(InstrType::Lofi); ++i)
    if (strcmp(factoryPreset(InstrType::Lofi, i).name, name) == 0) return i;
  return -1;
}

// Renders `name` (a LOFI factory preset; macro k set to v when k >= 0; formula f with its default
// A B C when f >= 0; the tempo when bpm > 0) at `note` for ~2 s.
std::vector<int16_t> render(const char* name, uint8_t note, int k = -1, uint8_t v = 0, int f = -1, uint16_t bpm = 0) {
  static Project p;
  p.reset();
  p.masterVol = 100;
  p.tracks[0].out = TrackOut::Int;
  p.tracks[0].vol = 127;
  const int i = findPreset(name);
  TEST_ASSERT_TRUE_MESSAGE(i >= 0, name);
  factoryBuild(InstrType::Lofi, i, p.instruments[0]);
  if (f >= 0) {
    p.instruments[0].lofiFormula = static_cast<uint8_t>(f);
    lofiLoadFormulaDefaults(p.instruments[0]);
  }
  if (k >= 0) p.instruments[0].macro[k] = v;
  if (bpm) p.bpm = bpm;
  Synth s(p);
  const uint8_t on[3] = {0x90, note, 127};
  s.event(0, 0, on, 3);
  std::vector<int16_t> out(static_cast<size_t>(kBlocks) * Synth::kBlock);
  for (int b = 0; b < kBlocks; ++b) s.render(out.data() + b * Synth::kBlock);
  return out;
}

void writeWav(const char* file, const std::vector<int16_t>& d) {
  char path[160];
  snprintf(path, sizeof(path), "%s/%s.wav", kDir, file);
  uint8_t h[kWavHeaderBytes];
  const uint32_t frames = static_cast<uint32_t>(d.size());
  wavHeader(h, frames, kSynthRate, 60, sampleCrc(d.data(), frames));
  FILE* f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  fwrite(h, 1, sizeof(h), f);
  fwrite(d.data(), sizeof(int16_t), d.size(), f);  // little-endian host
  fclose(f);
  printf("wrote %s\n", path);
}

// File name: lowercase, spaces -> '_'.
void slug(const char* in, char* out, int n) {
  int j = 0;
  for (int i = 0; in[i] && j < n - 1; ++i) {
    const char c = in[i];
    out[j++] = c == ' ' ? '_' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  out[j] = 0;
}

}  // namespace

void test_render_lofi_wavs() {
  if (!enabled()) {
    TEST_PASS_MESSAGE("LOFI_WAV != 1: nothing written");
    return;
  }
  mkdir(".pio", 0755);
  mkdir(kDir, 0755);
  char base[32], file[64];
  // BYTE: six factory presets at C-3 / C-4 / C-5.
  const char* bytes[] = {"42 MELDY", "VIZNUT", "SIERPNSK", "LOSTSPC", "NIBBLES", "LOOP ARP"};
  const struct { uint8_t note; const char* tag; } notes[] = {{48, "c3"}, {60, "c4"}, {72, "c5"}};
  for (const char* b : bytes)
    for (const auto& n : notes) {
      slug(b, base, sizeof(base));
      snprintf(file, sizeof(file), "byte_%s_%s", base, n.tag);
      writeWav(file, render(b, n.note));
    }
  // BYTE: new library formulas on the 42 MELDY preset at C-4 (free running).
  const struct { int f; const char* tag; } formulas[] = {{26, "tejeez3"}, {28, "red"}, {41, "octbass"}, {43, "melseq"}};
  for (const auto& x : formulas) {
    snprintf(file, sizeof(file), "byte_f%02d_%s_c4", x.f, x.tag);
    writeWav(file, render("42 MELDY", 60, -1, 0, x.f));
  }
  // BYTE SYNC: the SYNC presets at 120 BPM (2 s = one bar), SYNCBEAT also at 90 BPM and an octave up.
  const char* syncs[] = {"SYNCBEAT", "SYNCHATS", "SYNCARP", "SYNCBASS"};
  for (const char* b : syncs) {
    slug(b, base, sizeof(base));
    snprintf(file, sizeof(file), "sync_%s_120", base);
    writeWav(file, render(b, 60, -1, 0, -1, 120));
  }
  writeWav("sync_syncbeat_090", render("SYNCBEAT", 60, -1, 0, -1, 90));
  writeWav("sync_syncarp_120_c5", render("SYNCARP", 72, -1, 0, -1, 120));
  // APC: WIDTH (macro 0) under a period / around it / far above (subharmonics), A-3.
  const uint8_t widths[] = {0, 64, 110};
  for (uint8_t w : widths) {
    snprintf(file, sizeof(file), "apc_width_%03d", w);
    writeWav(file, render("APC", 57, 0, w));
  }
  // APC: SMOOTH (macro 3) off / full.
  writeWav("apc_smooth_000", render("APC", 57, 3, 0));
  writeWav("apc_smooth_127", render("APC", 57, 3, 127));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_render_lofi_wavs);
  return UNITY_END();
}
