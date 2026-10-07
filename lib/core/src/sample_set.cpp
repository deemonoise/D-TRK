#include "sample_set.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "file_rules.h"
#include "project_io.h"
#include "wt_mip.h"

namespace mt {

static_assert(kProjSamples == kBankEntries, "every listed sample must fit the bank cache");
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "sampleCrc hashes int16 data as stored bytes");

namespace {

// Keys of a project's samples and wavetables, worked out once for a pass over the bank.
struct UsedKeys {
  uint32_t crc[kProjSamples];
  uint32_t frames[kProjSamples];
  uint32_t wt[kProjWavetables];
  int n = 0, nw = 0;
  explicit UsedKeys(const Project& p) {
    for (; n < p.sampleCount && n < kProjSamples; ++n) {
      crc[n] = p.samples[n].crc;
      frames[n] = p.samples[n].frames;
    }
    for (; nw < p.wavetableCount && nw < kProjWavetables; ++nw) wt[nw] = p.wavetables[nw].crc;
  }
  bool has(const BankEntry& e) const {
    if (e.name[0] == '*') return true;  // built-in wavetable
    if (isWtKey(e.name)) {
      if (e.frames != static_cast<uint32_t>(kWtTableSamples)) return false;
      const uint32_t c = static_cast<uint32_t>(strtoul(e.name + 1, nullptr, 16));
      for (int i = 0; i < nw; ++i)
        if (wt[i] == c) return true;
      return false;
    }
    if (!isSampleKey(e.name)) return false;
    const uint32_t c = static_cast<uint32_t>(strtoul(e.name, nullptr, 16));
    for (int i = 0; i < n; ++i)
      if (crc[i] == c && frames[i] == e.frames) return true;
    return false;
  }
};

// Bank entry that is only cache of data identified by its name.
bool isKeyed(const char* name) { return isSampleKey(name) || isWtKey(name); }

}  // namespace

void sampleKey(uint32_t crc, char out[kSampleNameMax + 1]) {
  snprintf(out, kSampleNameMax + 1, "%08x", static_cast<unsigned>(crc));
}

bool isSampleKey(const char* n) {
  for (int i = 0; i < 8; ++i)
    if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return false;
  return n[8] == 0;
}

void wtKey(uint32_t crc, char out[kSampleNameMax + 1]) {
  snprintf(out, kSampleNameMax + 1, "w%08x", static_cast<unsigned>(crc));
}

bool isWtKey(const char* n) { return n[0] == 'w' && isSampleKey(n + 1); }

uint32_t sampleCrc(const int16_t* d, uint32_t frames, uint32_t prev) {
  // Flash and both targets are little-endian: the bytes of d are the stored bytes.
  return crc32(d, static_cast<size_t>(frames) * 2, prev);
}

int projSampleFind(const Project& p, const char* name) {
  for (int i = 0; i < p.sampleCount; ++i)
    if (strcasecmp(p.samples[i].name, name) == 0) return i;
  return -1;
}

int projWtFind(const Project& p, const char* name) {
  for (int i = 0; i < p.wavetableCount; ++i)
    if (strcasecmp(p.wavetables[i].name, name) == 0) return i;
  return -1;
}

int projWtSet(Project& p, const char* name, uint32_t crc) {
  if (name[0] == '*' || !projectBaseValid(name)) return -1;
  int i = projWtFind(p, name);
  if (i < 0) {
    if (p.wavetableCount >= kProjWavetables) return -1;
    i = p.wavetableCount++;
    strcpy(p.wavetables[i].name, name);
  }
  p.wavetables[i].crc = crc;
  return i;
}

void projWtRemove(Project& p, int i) {
  if (i < 0 || i >= p.wavetableCount) return;
  for (int j = i; j + 1 < p.wavetableCount; ++j) p.wavetables[j] = p.wavetables[j + 1];
  p.wavetables[--p.wavetableCount] = ProjWavetable{};
}

int projWtPrune(Project& p) {
  int n = 0;
  for (int i = p.wavetableCount - 1; i >= 0; --i) {
    bool used = false;
    for (const Instrument& m : p.instruments)
      for (const char* w : m.synWt) used = used || strcasecmp(w, p.wavetables[i].name) == 0;
    if (!used) {
      projWtRemove(p, i);
      ++n;
    }
  }
  return n;
}

int projWtBank(const Project& p, const SampleBank& b, int i) {
  if (i < 0 || i >= p.wavetableCount) return -1;
  char k[kSampleNameMax + 1];
  wtKey(p.wavetables[i].crc, k);
  const int j = b.find(k);
  return j >= 0 && b.entry(j)->frames == static_cast<uint32_t>(kWtTableSamples) ? j : -1;
}

int projSampleSet(Project& p, const char* name, uint32_t crc, uint32_t frames) {
  if (!projectBaseValid(name)) return -1;
  int i = projSampleFind(p, name);
  if (i < 0) {
    if (p.sampleCount >= kProjSamples) return -1;
    i = p.sampleCount++;
    strcpy(p.samples[i].name, name);
  }
  p.samples[i].crc = crc;
  p.samples[i].frames = frames;
  return i;
}

void projSampleRemove(Project& p, int i) {
  if (i < 0 || i >= p.sampleCount) return;
  for (int k = i; k + 1 < p.sampleCount; ++k) p.samples[k] = p.samples[k + 1];
  p.samples[--p.sampleCount] = ProjSample{};
}

bool projSampleRename(Project& p, int i, const char* name) {
  if (i < 0 || i >= p.sampleCount || !projectBaseValid(name)) return false;
  const int other = projSampleFind(p, name);
  if (other >= 0 && other != i) return false;
  for (Instrument& in : p.instruments) {
    if (strcasecmp(in.sample, p.samples[i].name) == 0) strcpy(in.sample, name);
    for (KitLane& l : in.kit)
      if (strcasecmp(l.sample, p.samples[i].name) == 0) strcpy(l.sample, name);
  }
  strcpy(p.samples[i].name, name);
  return true;
}

int projSampleUser(const Project& p, const char* name) {
  for (int i = 0; i < kInstruments; ++i) {
    const Instrument& in = p.instruments[i];
    if (in.type == InstrType::Sample && strcasecmp(in.sample, name) == 0) return i;
    if (in.type == InstrType::Kit)
      for (const KitLane& l : in.kit)
        if (l.instr >= kInstruments && strcasecmp(l.sample, name) == 0) return i;
  }
  return -1;
}

int projSampleBank(const Project& p, const SampleBank& b, int i) {
  if (i < 0 || i >= p.sampleCount) return -1;
  char k[kSampleNameMax + 1];
  sampleKey(p.samples[i].crc, k);
  const int j = b.find(k);
  return j >= 0 && b.entry(j)->frames == p.samples[i].frames ? j : -1;
}

bool bankEntryUsed(const Project& p, const BankEntry& e) { return UsedKeys(p).has(e); }

bool bankMakeRoom(SampleBank& b, const Project& p, uint32_t frames, BankProgress cb, void* ctx) {
  if (frames == 0 || frames > b.capacity() / 2) return false;  // never fits: keep the cache
  const UsedKeys used(p);
  const uint32_t need = (frames * 2 + kBankAlign - 1) / kBankAlign * kBankAlign;
  bool compacted = false;  // since the last eviction: compacting again changes nothing
  for (;;) {
    if (b.fits(frames)) return true;
    // Enough in total but fragmented: close the holes before evicting more.
    if (!compacted && b.count() < kBankEntries && b.freeBytes() >= need) {
      if (!b.compact(cb, ctx)) return false;
      compacted = true;
      if (b.fits(frames)) return true;
    }
    // Largest unused, keyed entries before legacy ones.
    int best = -1;
    bool bestKey = false;
    for (int i = 0; i < b.count(); ++i) {
      const BankEntry& e = *b.entry(i);
      if (used.has(e)) continue;
      const bool key = isKeyed(e.name);
      if (best < 0 || (key && !bestKey) || (key == bestKey && e.frames > b.entry(best)->frames)) {
        best = i;
        bestKey = key;
      }
    }
    if (best < 0 || !b.remove(best)) return false;
    compacted = false;
  }
}

int bankClearUnused(SampleBank& b, const Project& p) {
  const UsedKeys used(p);
  int n = 0;
  for (int i = b.count() - 1; i >= 0; --i)
    if (isKeyed(b.entry(i)->name) && !used.has(*b.entry(i)) && b.remove(i)) ++n;
  return n;
}

uint32_t bankUnusedBytes(const SampleBank& b, const Project& p) {
  const UsedKeys used(p);
  uint32_t sum = 0;
  for (int i = 0; i < b.count(); ++i) {
    const BankEntry& e = *b.entry(i);
    if (!used.has(e)) sum += (e.frames * 2 + kBankAlign - 1) / kBankAlign * kBankAlign;
  }
  return sum;
}

namespace {

// crc of bank entry i's data, read in pieces. False on a read error.
bool entryCrc(const SampleBank& b, int i, uint32_t& crc) {
  const uint32_t frames = b.entry(i)->frames;
  int16_t buf[256];
  crc = 0;
  for (uint32_t f = 0; f < frames; f += 256) {
    const uint32_t n = frames - f < 256 ? frames - f : 256;
    if (!b.readData(i, f, buf, n)) return false;
    crc = sampleCrc(buf, n, crc);
  }
  return true;
}

}  // namespace

int migrateSamples(Project& p, SampleBank& b, LegacyIndex& idx) {
  int missing = 0;
  for (int t = 0; t < kInstruments; ++t) {
    const char* name = p.instruments[t].sample;
    if (!name[0] || projSampleFind(p, name) >= 0) continue;
    bool seen = false;  // an earlier instrument with this name was already counted missing
    for (int u = 0; u < t && !seen; ++u) seen = strcasecmp(p.instruments[u].sample, name) == 0;
    if (seen) continue;
    if (!projectBaseValid(name)) {  // cannot be a file in the project folder
      ++missing;
      continue;
    }
    LegacySample s{};
    const int i = b.find(name);
    if (i >= 0) {
      const BankEntry e = *b.entry(i);
      uint32_t crc;
      if (!entryCrc(b, i, crc)) {
        ++missing;
        continue;
      }
      char k[kSampleNameMax + 1];
      sampleKey(crc, k);
      if (strcasecmp(e.name, k) == 0) {  // the key of its own data: already plain cache
        projSampleSet(p, name, crc, e.frames);
        continue;
      }
      strcpy(s.name, e.name);
      snprintf(s.project, sizeof(s.project), "%s", p.name);
      s.crc = crc;
      s.frames = e.frames;
      // Recorded first: a renamed entry is never left without its old name in the index.
      if (!idx.add(s)) {
        ++missing;
        continue;
      }
      const int dup = b.find(k);
      const bool ok = dup >= 0 && b.entry(dup)->frames == e.frames ? b.remove(i)  // same data cached
                                                                   : b.rename(i, k);
      if (!ok) {
        ++missing;
        continue;
      }
    } else if (!idx.find(name, s)) {
      ++missing;
      continue;
    }
    projSampleSet(p, name, s.crc, s.frames);
  }
  return missing;
}

}  // namespace mt
