#include "sample_tools.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "bank.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "file_rules.h"
#include "hw/sdcard.h"
#include "sample_set.h"
#include "storage/storage.h"
#include "time_stretch.h"
#include "wav.h"

namespace audio {
namespace {
constexpr uint32_t kPiece = 1024;

SampleToolResult checked(mt::SampleExtractResult result) {
  using R = mt::SampleExtractResult;
  switch (result) {
    case R::Ok: return SampleToolResult::Ok;
    case R::BadName: return SampleToolResult::BadName;
    case R::NameTaken: return SampleToolResult::NameTaken;
    case R::Full: return SampleToolResult::Full;
    case R::Cancelled: return SampleToolResult::Cancelled;
    default: return SampleToolResult::BadSource;
  }
}
struct Block {
  int16_t* data = static_cast<int16_t*>(heap_caps_malloc(kPiece * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  ~Block() { heap_caps_free(data); }
};
struct Source : mt::TimeStretchReader {
  int index = -1;
  uint32_t from = 0, frames = 0, rate = 0;
  uint8_t root = 60;
  bool read(uint32_t first, uint32_t count, int16_t* out) override {
    return first <= frames && count <= frames-first && bank().readData(index, from+first, out, count);
  }
};
SampleToolResult sourceFor(const mt::Project& project, const mt::SampleExtractTarget& target, Source& out) {
  if (!bankMounted()) return SampleToolResult::NoBank;
  if (target.index < 0 || target.index >= project.sampleCount || target.from >= target.to ||
      target.to > target.source.frames) return SampleToolResult::BadSource;
  const mt::ProjSample& sample = project.samples[target.index];
  if (sample.crc != target.source.crc || sample.frames != target.source.frames ||
      strcmp(sample.name, target.source.name)) return SampleToolResult::BadSource;
  out.index = mt::projSampleBank(project, bank(), target.index);
  const mt::BankEntry* entry = bank().entry(out.index);
  if (!entry || !entry->rate || entry->rate > mt::kWavMaxRate || entry->frames != sample.frames)
    return SampleToolResult::BadSource;
  out.from = target.from; out.frames = target.to-target.from;
  out.rate = entry->rate; out.root = entry->root;
  return SampleToolResult::Ok;
}
bool report(SampleToolProgress progress, uint32_t done, uint32_t total, void* ctx) {
  return !progress || progress(done, total, ctx);
}
SampleToolResult verify(const char* path, const mt::SampleExtractOutput& expected, int16_t* block,
                         SampleToolProgress progress, void* ctx) {
  fs::File file = hw::sdFs().open(path, FILE_READ);
  if (!file) return SampleToolResult::ReadFail;
  hw::FileSource input(file);
  mt::WavInfo info;
  if (mt::wavParse(input, info) != mt::WavErr::Ok || info.bits != 16 || info.channels != 1 ||
      info.frames() != expected.frames || info.rate != expected.rate || !info.hasCrc || info.crc != expected.crc ||
      file.size() != info.dataOffset + expected.frames * 2u || !file.seek(info.dataOffset)) {
    file.close(); return SampleToolResult::ReadFail;
  }
  uint32_t crc = 0;
  for (uint32_t done = 0; done < expected.frames;) {
    const uint32_t count = expected.frames-done < kPiece ? expected.frames-done : kPiece;
    if (file.read(reinterpret_cast<uint8_t*>(block), count*2) != count*2) {
      file.close(); return SampleToolResult::ReadFail;
    }
    crc = mt::sampleCrc(block, count, crc); done += count;
    if (!report(progress, done, expected.frames, ctx)) { file.close(); return SampleToolResult::Cancelled; }
  }
  file.close();
  return crc == expected.crc ? SampleToolResult::Ok : SampleToolResult::ReadFail;
}
struct ImportProgress {
  SampleToolProgress callback;
  void* ctx;
  bool cancelled = false;
  static void call(uint32_t done, uint32_t total, void* value) {
    auto& self = *static_cast<ImportProgress*>(value);
    if (!self.cancelled && !report(self.callback, done, total, self.ctx)) self.cancelled = true;
  }
};
SampleToolResult publish(mt::Project& project, const mt::SampleExtractTarget& target, const char* name,
                         const char* temporary, const char* destination, const mt::SampleExtractOutput& output,
                         SampleToolProgress progress, void* ctx) {
  ImportOut imported{};
  ImportProgress importing{progress, ctx};
  const BankResult result = importToCache(temporary, project, imported, &output.crc, ImportProgress::call, &importing);
  if (importing.cancelled) return SampleToolResult::Cancelled;
  if (result != BankResult::Ok) {
    if (result == BankResult::Full) return SampleToolResult::Full;
    if (result == BankResult::NoMemory) return SampleToolResult::NoMemory;
    return SampleToolResult::WriteFail;
  }
  if (imported.crc != output.crc || imported.frames != output.frames) return SampleToolResult::ReadFail;
  if (!report(progress, 1, 1, ctx)) return SampleToolResult::Cancelled;
  if (hw::sdFs().exists(destination)) return SampleToolResult::NameTaken;
  if (!hw::sdFs().rename(temporary, destination)) return SampleToolResult::WriteFail;
  engine::lockProject();
  const auto committed = mt::sampleExtractCommit(project, target, name, mt::SampleExtractResult::Ok, output);
  engine::unlockProject();
  if (committed != mt::SampleExtractResult::Ok) hw::sdFs().remove(destination);
  return checked(committed);
}
SampleToolResult create(mt::Project& project, const mt::SampleExtractTarget& target, const char* name,
                        bool normalize, bool stretching, SampleToolProgress progress, void* ctx) {
  const auto ready = checked(mt::sampleExtractCheck(project, target, name));
  if (ready != SampleToolResult::Ok) return ready;
  if (!hw::sdReady()) return SampleToolResult::NoSd;
  if (!mt::projectBaseValid(project.name)) return SampleToolResult::BadName;
  if (!storage::stopEngine()) return SampleToolResult::Busy;
  Source source;
  if (auto result = sourceFor(project, target, source); result != SampleToolResult::Ok) return result;
  const uint32_t frames = mt::sampleEditFrames(source.frames, stretching ? target.ratioQ16 : 65536);
  if (!frames) return SampleToolResult::BadRatio;
  if (uint64_t(frames)*sizeof(int16_t) > bank().capacity()) return SampleToolResult::Full;
  char directory[40], destination[72], temporary[80];
  snprintf(directory, sizeof(directory), "/projects/%s", project.name);
  snprintf(destination, sizeof(destination), "%s/%s.wav", directory, name);
  snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
  auto& fs = hw::sdFs();
  if (fs.exists(destination)) return SampleToolResult::NameTaken;
  if (!fs.exists(directory) && !fs.mkdir(directory)) return SampleToolResult::WriteFail;
  Block block;
  if (!block.data) return SampleToolResult::NoMemory;
  int32_t peak = 0;
  if (normalize) {
    for (uint32_t done = 0; done < source.frames;) {
      const uint32_t count = source.frames-done < kPiece ? source.frames-done : kPiece;
      if (!source.read(done, count, block.data)) return SampleToolResult::ReadFail;
      peak = mt::sampleExtractPeak(block.data, count, peak);
      done += count;
      if (!report(progress, done, source.frames, ctx)) return SampleToolResult::Cancelled;
    }
  }
  if (fs.exists(temporary) && !fs.remove(temporary)) return SampleToolResult::WriteFail;
  fs::File file = fs.open(temporary, FILE_WRITE);
  if (!file) return SampleToolResult::WriteFail;
  struct Cleanup { fs::File& file; const char* path; ~Cleanup() { file.close(); if (hw::sdFs().exists(path)) hw::sdFs().remove(path); } } cleanup{file, temporary};
  uint8_t header[mt::kWavHeaderBytes];
  mt::wavHeader(header, frames, source.rate, source.root, 0);
  if (file.write(header, sizeof(header)) != sizeof(header)) return SampleToolResult::WriteFail;
  mt::TimeStretch stretch;
  if (stretching && stretch.start(source.frames, source.rate, frames) != mt::TimeStretchResult::Running)
    return SampleToolResult::NoMemory;
  uint32_t crc = 0;
  for (uint32_t done = 0; done < frames;) {
    uint32_t count = frames-done < kPiece ? frames-done : kPiece;
    if (stretching) {
      const auto state = stretch.step(source, block.data, count, count);
      if (state != mt::TimeStretchResult::Running && state != mt::TimeStretchResult::Done) return SampleToolResult::ReadFail;
      if (!count) return SampleToolResult::ReadFail;
    } else if (!source.read(done, count, block.data)) return SampleToolResult::ReadFail;
    if (normalize) mt::sampleExtractNormalize(block.data, count, peak);
    if (file.write(reinterpret_cast<uint8_t*>(block.data), count*2) != count*2) return SampleToolResult::WriteFail;
    crc = mt::sampleCrc(block.data, count, crc); done += count;
    if (!report(progress, done, frames, ctx)) return SampleToolResult::Cancelled;
  }
  mt::wavHeader(header, frames, source.rate, source.root, crc);
  if (!file.seek(0) || file.write(header, sizeof(header)) != sizeof(header)) return SampleToolResult::WriteFail;
  file.close();
  const mt::SampleExtractOutput output{crc, frames, source.rate, source.root};
  if (auto result = verify(temporary, output, block.data, progress, ctx); result != SampleToolResult::Ok) return result;
  return publish(project, target, name, temporary, destination, output, progress, ctx);
}
}

const char* sampleToolResultText(SampleToolResult result) {
  switch (result) {
    case SampleToolResult::Ok: return "OK";
    case SampleToolResult::NoSd: return "NO SD CARD";
    case SampleToolResult::NoBank: return "NO SAMPLE BANK";
    case SampleToolResult::BadSource: return "SOURCE CHANGED / MISSING";
    case SampleToolResult::BadName: return "BAD SAMPLE NAME";
    case SampleToolResult::NameTaken: return "SAMPLE NAME EXISTS";
    case SampleToolResult::Full: return "SAMPLE LIST / BANK FULL";
    case SampleToolResult::Busy: return "ENGINE BUSY";
    case SampleToolResult::Cancelled: return "CANCELLED";
    case SampleToolResult::ReadFail: return "SAMPLE READ FAILED";
    case SampleToolResult::WriteFail: return "SAMPLE WRITE FAILED";
    case SampleToolResult::NoMemory: return "NO MEMORY";
    case SampleToolResult::BadRatio: return "STRETCH RANGE 50-200%";
  }
  return "SAMPLE ERROR";
}
SampleToolResult extractSample(mt::Project& p, const mt::SampleExtractTarget& target, const char* name,
                               bool normalize, SampleToolProgress progress, void* ctx) {
  return create(p, target, name, normalize, false, progress, ctx);
}
SampleToolResult stretchSample(mt::Project& p, const mt::SampleExtractTarget& target, const char* name,
                               SampleToolProgress progress, void* ctx) {
  return create(p, target, name, false, true, progress, ctx);
}
SampleToolResult detectSampleTempo(const mt::Project& project, const mt::SampleExtractTarget& target,
                                   mt::TempoEstimate& estimate, SampleToolProgress progress, void* ctx) {
  estimate = {};
  if (!storage::stopEngine()) return SampleToolResult::Busy;
  Source source;
  if (auto result = sourceFor(project, target, source); result != SampleToolResult::Ok) return result;
  Block block;
  if (!block.data) return SampleToolResult::NoMemory;
  void* memory = heap_caps_malloc(sizeof(mt::OnsetScan), MALLOC_CAP_SPIRAM);
  if (!memory) return SampleToolResult::NoMemory;
  auto* scan = new (memory) mt::OnsetScan(source.frames, source.rate, mt::kMaxOnsets);
  struct ScanLease { mt::OnsetScan* scan; ~ScanLease() { scan->~OnsetScan(); heap_caps_free(scan); } } lease{scan};
  for (uint32_t done=0; done<source.frames;) {
    const uint32_t count = source.frames-done < kPiece ? source.frames-done : kPiece;
    if (!source.read(done, count, block.data)) return SampleToolResult::ReadFail;
    scan->feed(block.data, count); done += count;
    if (!report(progress, done, source.frames, ctx)) return SampleToolResult::Cancelled;
  }
  mt::Onset onsets[mt::kMaxOnsets];
  const int count = scan->finish(onsets);
  estimate = mt::estimateTempo(onsets, count, source.rate, source.frames);
  return SampleToolResult::Ok;
}
}
