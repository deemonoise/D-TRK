#include "prog_store.h"
#include "text_store.h"

namespace storage {
namespace {

constexpr const char* kProgExt = ".prg";
constexpr int kTextMax = mt::kProgTextMax + 2;  // formatProgression's longest text + slack

}  // namespace

int listProgs(char (*names)[hw::kNameMax], int max) { return listTexts(kProgDir, kProgExt, names, max); }

Result loadProg(const char* name, mt::Progression& out) {
  char text[kTextMax + 1];
  const Result r = loadText(kProgDir, name, kProgExt, text, sizeof(text));
  if (r != Result::Ok) return r;
  return mt::parseProgression(text, out) ? Result::Ok : Result::BadFile;
}

Result saveProg(const char* name, const mt::Progression& p) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  char text[kTextMax + 1];
  if (!mt::formatProgression(p, text, sizeof(text))) return Result::BadFile;
  return saveText(kProgDir, name, kProgExt, text);
}

}  // namespace storage
