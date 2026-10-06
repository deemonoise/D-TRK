#pragma once
#include <stdint.h>
#include "model.h"
#include "sample_bank.h"

namespace mt {

// The project's sample list and its bank cache. Bank entries are named by the crc32 of their
// data, so equal data shared by projects is one entry; the project maps its own names to crcs.

// Bank entry name for sample data: crc32 as 8 lower-case hex digits.
void sampleKey(uint32_t crc, char out[kSampleNameMax + 1]);
bool isSampleKey(const char* name);  // exactly 8 lower-case hex digits
// crc32 of frames int16 samples, little-endian bytes (as stored in flash). Chained via prev.
uint32_t sampleCrc(const int16_t* d, uint32_t frames, uint32_t prev = 0);

int projSampleFind(const Project& p, const char* name);  // ignoring case, -1 if absent
// Adds or, for an existing name, replaces crc / frames. -1: list full or bad name (names are file
// names in the project folder: projectBaseValid).
int projSampleSet(Project& p, const char* name, uint32_t crc, uint32_t frames);
// Removes entry i; instruments keep the name (they become "missing").
void projSampleRemove(Project& p, int i);
// New name for entry i; instruments that used the old name follow. False: bad or taken name.
bool projSampleRename(Project& p, int i, const char* name);

// Wavetables: bank entries named "w" + crc32 (8 lower-case hex) of the canonical source, frames
// kWtTableSamples, rate 0; built-ins "*NAME" are never evicted or cleared.
void wtKey(uint32_t crc, char out[kSampleNameMax + 1]);
bool isWtKey(const char* name);
int projWtFind(const Project& p, const char* name);  // ignoring case, -1
// Adds or replaces; -1 if full or the name is invalid (projectBaseValid) or starts with '*'.
int projWtSet(Project& p, const char* name, uint32_t crc);
void projWtRemove(Project& p, int i);
// Removes entries no instrument references (any osc, any mode). Count removed.
int projWtPrune(Project& p);
int projWtBank(const Project& p, const SampleBank& b, int i);  // bank index or -1

// Bank index of project sample i (key and frames match), -1 if not cached.
int projSampleBank(const Project& p, const SampleBank& b, int i);
// True if bank entry e is used by p (built-in wavetables "*NAME" always are).
bool bankEntryUsed(const Project& p, const BankEntry& e);
// Makes room for a sample of frames (SampleBank::fits): compacts when the free space is enough
// but fragmented, otherwise removes the largest bank entry not used by p and tries again. Entries
// with key names go first; old plain-named ones only when no keyed one is left (they may still
// belong to old projects not migrated yet).
// False: still no room (entries already removed stay removed: they are only cache).
bool bankMakeRoom(SampleBank& b, const Project& p, uint32_t frames, BankProgress cb = nullptr,
                  void* ctx = nullptr);
// Removes every keyed bank entry not used by p (old plain-named ones may still belong to old
// projects not migrated yet: only bankMakeRoom evicts them, as a last resort). Count removed.
int bankClearUnused(SampleBank& b, const Project& p);
// Bank space (whole sectors) of the entries not used by p, old plain-named ones included.
uint32_t bankUnusedBytes(const SampleBank& b, const Project& p);

// Legacy sample (old bank name) -> data identity, kept in /projects/legacy.idx on the device.
struct LegacySample {
  char name[kSampleNameMax + 1];
  uint32_t crc, frames;
  char project[kSampleNameMax + 1];  // project migrated first: its folder gets the file ("" unknown)
};
struct LegacyIndex {
  virtual bool find(const char* name, LegacySample& out) = 0;  // ignoring case
  virtual bool add(const LegacySample& s) = 0;
};
// For a project without a sample list (old file): every distinct sample name of its instruments is
// looked up as a bank entry (an old one is recorded in idx, then renamed to its key; one already named
// by the key of its data is taken as is) or in idx; found ones are added to p.samples. Names not found,
// not valid sample names or failing to migrate are left to show as missing. Returns how many distinct
// names those are.
int migrateSamples(Project& p, SampleBank& b, LegacyIndex& idx);

}  // namespace mt
