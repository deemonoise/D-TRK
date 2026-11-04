#include "sample_extract.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "file_rules.h"
#include "sample_set.h"
#include "slices.h"

namespace mt {

uint32_t sampleEditFrames(uint32_t frames, uint32_t ratioQ16) {
  if (!frames || ratioQ16<32768 || ratioQ16>131072) return 0;
  uint64_t out=(static_cast<uint64_t>(frames)*ratioQ16+32768)/65536;
  if (!out) out=1;
  return out <= (UINT32_MAX-512u)/2 ? static_cast<uint32_t>(out) : 0;
}

bool sampleStretchRatio(int sourceBpm10, int targetBpm10, uint32_t& ratioQ16) {
  if (sourceBpm10<200 || sourceBpm10>4800 || targetBpm10<200 || targetBpm10>4800 ||
      sourceBpm10*2<targetBpm10 || sourceBpm10>targetBpm10*2) return false;
  ratioQ16=static_cast<uint32_t>((static_cast<uint64_t>(sourceBpm10)*65536+targetBpm10/2)/targetBpm10);
  return true;
}

bool sampleExtractRegion(const Instrument& in, uint32_t frames, int slice, uint32_t& from, uint32_t& to) {
  if (!frames) return false;
  uint32_t start = fracToFrame(in.start, frames);
  if (start >= frames) start = frames - 1;
  uint32_t end = fracToFrame(in.end, frames);
  if (end <= start) end = start + 1;
  from = start;
  to = end;
  if (slice < 0) return true;
  uint32_t a, b;
  if (!sliceRegion(in, slice, frames, a, b)) return false;
  from = a > start ? a : start;
  to = b < end ? b : end;
  return from < to;
}

bool sampleExtractName(const SynthModel& p, const char* base, char out[kSampleNameMax + 1]) {
  const char* stem = base && projectBaseValid(base) ? base : "SAMPLE";
  if (projSampleFind(p, stem) < 0) {
    strcpy(out, stem);
    return true;
  }
  for (int suffix = 1; suffix <= kProjSamples; ++suffix) {
    char tail[8];
    snprintf(tail, sizeof tail, "_%d", suffix);
    snprintf(out, kSampleNameMax + 1, "%.*s%s", kSampleNameMax - static_cast<int>(strlen(tail)), stem, tail);
    if (projSampleFind(p, out) < 0) return true;
  }
  out[0] = 0;
  return false;
}

SampleExtractResult sampleExtractCheck(const SynthModel& p, const SampleExtractTarget& target, const char* name) {
  if (target.instrument < 0 || target.instrument >= kInstruments || target.index < 0 ||
      target.index >= p.sampleCount || target.from >= target.to || target.to > target.source.frames ||
      !sampleEditFrames(target.to-target.from,target.ratioQ16))
    return SampleExtractResult::BadSource;
  const ProjSample& current = p.samples[target.index];
  const Instrument& in = p.instruments[target.instrument];
  if (in.type != InstrType::Sample || current.crc != target.source.crc || current.frames != target.source.frames ||
      strcasecmp(current.name, target.source.name) != 0 || strcasecmp(in.sample, target.source.name) != 0)
    return SampleExtractResult::BadSource;
  if (!name || !projectBaseValid(name)) return SampleExtractResult::BadName;
  if (projSampleFind(p, name) >= 0) return SampleExtractResult::NameTaken;
  if (p.sampleCount >= kProjSamples) return SampleExtractResult::Full;
  return SampleExtractResult::Ok;
}

SampleExtractResult sampleExtractCommit(Project& p, const SampleExtractTarget& target, const char* name,
                                        BankResult result, const ImportOut& out, bool cancelled) {
  if (cancelled || result == BankResult::Cancelled) return SampleExtractResult::Cancelled;
  if (result != BankResult::Ok) return SampleExtractResult::JobFailed;
  const SampleExtractResult ready = sampleExtractCheck(p, target, name);
  if (ready != SampleExtractResult::Ok) return ready;
  if (out.frames != sampleEditFrames(target.to-target.from,target.ratioQ16) || !out.rate || out.rate > 44100)
    return SampleExtractResult::JobFailed;
  // All failure paths are above: append and bind together while the caller holds the project lock.
  ProjSample& added = p.samples[p.sampleCount++];
  added = ProjSample{};
  strcpy(added.name, name);
  added.crc = out.crc;
  added.frames = out.frames;
  Instrument& in = p.instruments[target.instrument];
  strcpy(in.sample, name);
  in.start = 0;
  in.end = 0xFFFF;
  in.loop = static_cast<uint8_t>(LoopMode::Off);
  in.loopStart = 0;
  in.sliceMode = static_cast<uint8_t>(SliceMode::Off);
  sliceClear(in);
  return SampleExtractResult::Ok;
}

}  // namespace mt
