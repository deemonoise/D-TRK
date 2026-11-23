#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

#include "audio/audio.h"
#include "brick_runtime.h"
#include "engine/engine.h"
#include "link/bank_client.h"
#include "rec.h"
#include "sample_set.h"
#include "storage/remote_fs.h"
#include "synth_syn.h"
#include "synth.h"
#include "wt_builtin.h"

#define CHECK(condition)                                                          \
  do {                                                                            \
    if (!(condition)) {                                                           \
      std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
      return 1;                                                                   \
    }                                                                             \
  } while (false)

namespace {
bool fixedWavetableMatchesSampleReads() {
  std::vector<int16_t> table(mt::kWtTableSamples);
  for (size_t i = 0; i < table.size(); ++i)
    table[i] = static_cast<int16_t>(static_cast<int>((i * 179) % 60001) - 30000);
  for (float hz : {61.f, 236.f, 1900.f}) {
    for (float position : {0.f, 17.37f, static_cast<float>(mt::kWtFrames - 1)}) {
      mt::WtOsc block, sample;
      block.setLevel(hz);
      sample.setLevel(hz);
      const uint32_t step = mt::WtOsc::step(hz / mt::kSynthRate);
      for (int i = 0; i < 16; ++i) {
        std::array<float, 17> out{};
        block.renderFixed(table.data(), step, position, out.data(), static_cast<int>(out.size()));
        for (float value : out)
          if (value != sample.next(table.data(), step, position)) return false;
      }
      if (block.next(table.data(), step, position) != sample.next(table.data(), step, position)) return false;
    }
  }
  return true;
}
}  // namespace

int main() {
  CHECK(fixedWavetableMatchesSampleReads());
  char temp[] = "/tmp/dtrk-engine-XXXXXX";
  CHECK(mkdtemp(temp));
  CHECK(setenv("DTRK_DATA", temp, 1) == 0);
  struct TempDirectory {
    const char* path;
    ~TempDirectory() {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  } scratch{temp};
  auto p = std::make_unique<mt::Project>();
  storage::remoteFs();
  audio::begin(p.get());
  engine::begin(p.get());
  CHECK(brick::synth().voiceBudget() < mt::kVoiceBudgetOff);
  CHECK(audio::synthUp() && audio::bankView().builtins == 255);
  const auto* w = mt::wtBuiltinName(0);
  int16_t wt[256];
  CHECK(audio::wtFrame16(w, 0, wt) == audio::BankResult::Ok);
  CHECK(audio::wtAudition(wt) == audio::BankResult::Ok);
  int16_t part[2 * 300]{};
  brick::render(part, 300);
  audio::previewStop();
  CHECK(*std::max_element(part, part + 600) > 20);
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = 48 + i;
  engine::post(engine::Cmd::StartStop);
  CHECK(engine::status().playing);
  std::vector<int16_t> pcm(44100 * 4);
  brick::render(pcm.data(), 88200);
  brick::render(part, 300);
  CHECK(*std::max_element(pcm.begin(), pcm.end()) > 100);
  CHECK(engine::status().loop == 1);
  engine::post(engine::Cmd::Stop);
  CHECK(!engine::status().playing);
  const char* sample = "/samples/test.wav";
  CHECK(mt::kBankEntries > mt::kProjSamples + mt::kProjWavetables + mt::kWtBuiltins);
  auto& d = brick::disk();
  auto* f = d.openWrite(sample);
  CHECK(f);
  uint8_t head[mt::kWavHeaderBytes];
  mt::wavHeader(head, 4096, 44100, 60, mt::sampleCrc(pcm.data(), 4096));
  CHECK(f->write(head, sizeof(head)) && f->write(pcm.data(), 8192) && d.closeWrite());
  mt::ImportOut o;
  CHECK(audio::importToCache(sample, o) == audio::BankResult::Ok && o.frames == 4096);
  CHECK(mt::projSampleSet(*p, "test", o.crc, o.frames) == 0);
  CHECK(audio::bankView().sampleCached(0));
  mt::ImportOut cut;
  CHECK(audio::extractSample(0, o.crc, 50, 2050, true, cut) == audio::BankResult::Ok && cut.frames == 2000);
  CHECK(audio::stretchSample(0, o.crc, 50, 2050, 98304, cut) == audio::BankResult::Ok && cut.frames == 3000);
  CHECK(audio::saveAssets("fixture") == audio::BankResult::Ok && d.exists("/projects/fixture/test.wav"));
  int at = mt::projSampleBank(*p, brick::bank(), 0);
  CHECK(at >= 0 && brick::bank().remove(at));
  audio::BankSets list;
  CHECK(audio::syncProject("fixture", &list) == audio::BankResult::Ok && list.missing == 0 &&
        audio::bankView().sampleCached(0));
  CHECK(audio::previewFile(sample) == audio::BankResult::Ok);
  brick::render(part, 300);
  CHECK(*std::max_element(part, part + 600) > 20);
  CHECK(engine::post(engine::Cmd::StartStop));
  CHECK(brick::importRecording(sample, cut, nullptr, nullptr) == audio::BankResult::Ok && cut.crc == o.crc);
  printf(
      "16-step playback, clock %llu us, peak %d; all eight factory wavetables; WAV/extract/stretch/save/reload "
      "OK\n",
      (unsigned long long)engine::nowUs(), *std::max_element(pcm.begin(), pcm.end()));
}
