#include "preset_paths.h"
#include <stdio.h>
#include <string.h>
#include "presets_factory.h"

namespace mt {
namespace {

constexpr const char* kTop = "/presets/";
constexpr size_t kTopLen = 9;

// Length of dir's "/presets/<TYPE>" prefix, 0 when it has none.
size_t rootLen(const char* dir) {
  if (strncmp(dir, kTop, kTopLen) != 0) return 0;
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    if (!presetTypeHas(static_cast<InstrType>(t))) continue;
    const char* r = presetRoot(static_cast<InstrType>(t));
    const size_t n = strlen(r);
    if (strncmp(dir, r, n) == 0 && (dir[n] == 0 || dir[n] == '/')) return n;
  }
  return 0;
}

}  // namespace

bool presetTypeHas(InstrType t) { return t < InstrType::Count && t != InstrType::Kit; }

const char* presetTypeName(InstrType t) {
  switch (t) {
    case InstrType::Kit: return "KIT";
    case InstrType::Sample: return "SAMPLE";
    case InstrType::Fm: return "FM";
    case InstrType::Drum: return "DRUM";
    case InstrType::Synth: return "SYNTH";
    default: return "CHIP";
  }
}

const char* presetRoot(InstrType t) {
  switch (t) {
    case InstrType::Sample: return "/presets/SAMPLE";
    case InstrType::Fm: return "/presets/FM";
    case InstrType::Drum: return "/presets/DRUM";
    case InstrType::Synth: return "/presets/SYNTH";
    case InstrType::Kit: return "/presets/KIT";
    default: return "/presets/CHIP";
  }
}

int presetDepth(const char* dir) {
  const size_t n = rootLen(dir);
  if (n == 0) return -1;
  int depth = 0;
  for (const char* p = dir + n; *p; ++p) depth += *p == '/';
  return depth;
}

const char* presetSubPath(const char* dir) {
  const size_t n = rootLen(dir);
  return n ? dir + n : dir;
}

bool presetJoin(char* out, size_t cap, const char* dir, const char* name, const char* ext) {
  const int n = snprintf(out, cap, "%s/%s%s", dir, name, ext);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool presetUp(char* dir, char* left, size_t leftCap) {
  const size_t n = rootLen(dir);
  if (n == 0 || dir[n] == 0) return false;
  char* slash = strrchr(dir, '/');
  if (left && leftCap) snprintf(left, leftCap, "%s", slash + 1);
  *slash = 0;
  return true;
}

int factoryCategoryCount(InstrType t) {
  int k = 0;
  while (factoryCategory(t, k)) ++k;
  return k;
}

// Linear scans: the table is small and this runs on UI events only.
const char* factoryCategory(InstrType t, int k) {
  const int n = factoryCount(t);
  int seen = 0;
  for (int i = 0; i < n; ++i) {
    const char* c = factoryPreset(t, i).category;
    bool first = true;
    for (int j = 0; j < i && first; ++j) first = strcmp(factoryPreset(t, j).category, c) != 0;
    if (!first) continue;
    if (seen++ == k) return c;
  }
  return nullptr;
}

}  // namespace mt
