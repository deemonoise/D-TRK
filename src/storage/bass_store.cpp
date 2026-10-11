#include "bass_store.h"
#include "text_store.h"

namespace storage {
namespace {

constexpr const char* kBassExt = ".bas";
constexpr int kTextMax = mt::kBassTextMax + 2;  // formatBassPattern's longest text + slack

}  // namespace

int listBass(char (*names)[hw::kNameMax], int max) { return listTexts(kBassDir, kBassExt, names, max); }

Result loadBass(const char* name, mt::BassPattern& out) {
  char text[kTextMax + 1];
  const Result r = loadText(kBassDir, name, kBassExt, text, sizeof(text));
  if (r != Result::Ok) return r;
  return mt::parseBassPattern(text, out) ? Result::Ok : Result::BadFile;
}

Result saveBass(const char* name, const mt::BassPattern& p) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  char text[kTextMax + 1];
  if (!mt::formatBassPattern(p, text, sizeof(text))) return Result::BadFile;
  return saveText(kBassDir, name, kBassExt, text);
}

}  // namespace storage
