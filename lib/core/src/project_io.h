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

// Resets out first. Values are clamped to valid ranges; unknown chunks are skipped.
LoadErr loadProject(ByteSource& in, Project& out);

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
