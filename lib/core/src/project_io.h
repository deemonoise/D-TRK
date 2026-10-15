#pragma once
#include <stddef.h>
#include <stdint.h>
#include "model.h"

namespace mt {

// .mtp: "MTRK" u16 version u16 reserved, then chunks (id[4] u32 size payload), ending with
// "CRC " (size 4) holding crc32 of every byte before that chunk. Little-endian.
constexpr uint16_t kProjectVersion = 1;

struct ByteSink {
  virtual bool write(const void* d, size_t n) = 0;
};

struct ByteSource {
  virtual bool read(void* d, size_t n) = 0;
  virtual bool skip(size_t n) = 0;
};

uint32_t crc32(const void* d, size_t n, uint32_t prev = 0);

bool saveProject(const Project& p, ByteSink& out);

enum class LoadErr : uint8_t { Ok, BadMagic, BadVersion, Truncated, BadCrc, BadValue };

// What a load changed in an old file (for a message to the user).
struct LoadNotes {
  bool songTransposeDropped = false;  // a legacy chain item had a transpose: the song has none
};

// Resets out first. Values are clamped to valid ranges; unknown chunks are skipped. Old song chunks
// (PROJ chain, CHN2, SCNS) are migrated to the song when the file has no SNG3.
LoadErr loadProject(ByteSource& in, Project& out, LoadNotes* notes = nullptr);

// One TINS record of len bytes (drive, crushBits, crushRate, driveModel, the gate's fields, ...):
// assigns the fields it holds, clamped; the others keep their values (an older, shorter record leaves the defaults).
void readTinsRecord(const uint8_t* b, uint32_t len, TrackCfg& c);

// The sample (SMPL) and wavetable (WTBL) names of a project file, without loading it (~3 KB
// instead of a whole Project). The CRC is checked like loadProject.
struct ProjectFileNames {
  char sample[kProjSamples][kSampleNameMax + 1];
  int samples = 0;
  char wavetable[kProjWavetables][kSampleNameMax + 1];
  int wavetables = 0;
  bool has(const char* name, bool wt) const;  // ignoring case
};
LoadErr readProjectFileNames(ByteSource& in, ProjectFileNames& out);

}  // namespace mt
