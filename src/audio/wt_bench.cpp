// Diagnostics (-DWT_BENCH, env wt32-wtbench): times the SYNTH oscillators once at boot and writes
// /midi/bench.mid (text) to the SD card. Wavetables read from the flash bank and from a RAM copy, with the
// data cache warm and flushed between blocks.
#ifdef WT_BENCH
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>
#include "audio/bank.h"
#include "hw/sdcard.h"
#include "synth.h"
#include "synth_filter.h"
#include "synth_syn.h"
#include "wt_mip.h"

namespace audio {
namespace {

constexpr int kN = 128;
constexpr int kBlocks = 200;
constexpr size_t kEvict = 128 * 1024;
volatile uint32_t evictSink;

void evict(const uint8_t* buf) {
  uint32_t a = 0;
  for (size_t i = 0; i < kEvict; i += 32) a += buf[i];
  evictSink = a;
}

// nv voices, tables tab[v % nt], notes spread; us per block for all voices (render only).
uint32_t run(const int16_t* const* tab, int nt, int nv, uint8_t m0, uint8_t m1, bool cold, const uint8_t* ev,
             bool fixed = false) {
  static mt::SynVoice v[8];
  static float buf[kN];
  mt::SynParams p[8];
  for (int i = 0; i < nv; ++i) {
    p[i].mode[0] = m0, p[i].mode[1] = m1;
    p[i].wt[0] = p[i].wt[1] = tab ? tab[i % nt] : nullptr;
    p[i].hz[0] = 110.f * (1 + i * 0.37f), p[i].hz[1] = p[i].hz[0] * 1.5f;
    p[i].mix = 0.5f, p[i].sub = 0.3f;
    v[i].trigger();
  }
  int64_t sum = 0;
  for (int b = 0; b < kBlocks; ++b) {
    if (cold) evict(ev);
    for (int i = 0; i < nv; ++i) p[i].shape[0] = p[i].shape[1] = fixed ? 0.f : (b % 100) * 0.01f;
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < nv; ++i) {
      v[i].control(p[i], kN);
      v[i].render(buf, kN, 0.5f);
    }
    sum += esp_timer_get_time() - t0;
  }
  return static_cast<uint32_t>(sum / kBlocks);
}

}  // namespace

void wtBench() {
  const mt::WtSource* src = wavetableSource();
  const char* names[4] = {"*SAWSQR", "*FORMANT", "*SINSAW", "*BELL"};
  const int16_t* fl[4];
  for (int i = 0; i < 4; ++i) fl[i] = src ? src->findWt(names[i]) : nullptr;
  const size_t tb = mt::kWtTableSamples * sizeof(int16_t);
  int16_t* ramI = static_cast<int16_t*>(heap_caps_malloc(tb, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  int16_t* ramP = static_cast<int16_t*>(heap_caps_malloc(tb, MALLOC_CAP_SPIRAM));
  uint8_t* ev = static_cast<uint8_t*>(heap_caps_malloc(kEvict, MALLOC_CAP_SPIRAM));
  if (ev) memset(ev, 1, kEvict);
  if (fl[0] && ramI) memcpy(ramI, fl[0], tb);
  if (fl[0] && ramP) memcpy(ramP, fl[0], tb);
  const int16_t* ti[1] = {ramI};
  const int16_t* tp[1] = {ramP};
  const uint8_t saw = static_cast<uint8_t>(mt::SynOsc::Saw), wt = static_cast<uint8_t>(mt::SynOsc::Wt);

  char out[1600];
  int o = 0;
  o += snprintf(out + o, sizeof(out) - o, "us per 4 ms block (4000 us), %d blocks; tables %s\n", kBlocks,
                fl[0] ? "ok" : "MISSING");
  o += snprintf(out + o, sizeof(out) - o, "internal RAM copy %s, free internal %u\n", ramI ? "ok" : "none",
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  auto line = [&](const char* name, uint32_t us) {
    o += snprintf(out + o, sizeof(out) - o, "%-28s %5u\n", name, static_cast<unsigned>(us));
  };
  line("1v saw+saw", run(nullptr, 1, 1, saw, saw, false, ev));
  line("8v saw+saw", run(nullptr, 1, 8, saw, saw, false, ev));
  if (fl[0]) {
    line("1v wt+wt flash warm", run(fl, 1, 1, wt, wt, false, ev));
    if (ev) line("1v wt+wt flash cold", run(fl, 1, 1, wt, wt, true, ev));
    line("8v wt+wt flash 4 tables warm", run(fl, 4, 8, wt, wt, false, ev));
    if (ev) line("8v wt+wt flash 4 tables cold", run(fl, 4, 8, wt, wt, true, ev));
    line("8v wt+saw flash 4 tables", run(fl, 4, 8, wt, saw, false, ev));
  }
  // Frames 0 and 1 only (shape 0): flash vs an internal RAM copy of them.
  int16_t* fr = static_cast<int16_t*>(heap_caps_malloc(2 * mt::kWtFramePts * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (fl[0] && fr) {
    memcpy(fr, fl[0], 2 * mt::kWtFramePts * 2);
    const int16_t* tf[1] = {fr};
    const uint8_t sq = static_cast<uint8_t>(mt::SynOsc::Square);
    line("1v sqr+sqr", run(nullptr, 1, 1, sq, sq, false, ev));
    line("1v wt+wt flash frame0", run(fl, 1, 1, wt, wt, false, ev, true));
    line("1v wt+wt iram frame0", run(tf, 1, 1, wt, wt, false, ev, true));
    line("8v wt+wt flash frame0", run(fl, 1, 8, wt, wt, false, ev, true));
    line("8v wt+wt iram frame0", run(tf, 1, 8, wt, wt, false, ev, true));
    line("8v wt+saw iram frame0", run(tf, 1, 8, wt, saw, false, ev, true));
  }
  heap_caps_free(fr);
  if (ramI) {
    line("1v wt+wt iram warm", run(ti, 1, 1, wt, wt, false, ev));
    if (ev) line("1v wt+wt iram cold", run(ti, 1, 1, wt, wt, true, ev));
    line("8v wt+wt iram 1 table", run(ti, 1, 8, wt, wt, false, ev));
  }
  if (ramP) {
    line("8v wt+wt psram 1 table warm", run(tp, 1, 8, wt, wt, false, ev));
    if (ev) line("8v wt+wt psram 1 table cold", run(tp, 1, 8, wt, wt, true, ev));
  }
  {
    mt::Svf f;
    static float buf[kN];
    for (auto& x : buf) x = 0.1f;
    const int64_t t0 = esp_timer_get_time();
    for (int b = 0; b < kBlocks; ++b)
      for (int c = 0; c < kN / 32; ++c) {
        f.set(mt::Svf::Mode::Lp, 500.f + c * 100.f + b, 4.f);
        for (int i = 0; i < 32; ++i) buf[c * 32 + i] = f.process(buf[c * 32 + i]);
      }
    line("1v svf", static_cast<uint32_t>((esp_timer_get_time() - t0) / kBlocks));
  }
  heap_caps_free(ramI);
  heap_caps_free(ramP);
  heap_caps_free(ev);
  Serial.print(out);
  fs::File f = hw::sdFs().open("/midi/bench.mid", "w");  // text; .mid: reachable over Wi-Fi
  if (f) {
    f.write(reinterpret_cast<const uint8_t*>(out), o);
    f.close();
  }
}

}  // namespace audio
#endif
