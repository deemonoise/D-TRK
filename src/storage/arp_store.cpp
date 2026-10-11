#include "arp_store.h"
#include "text_store.h"

namespace storage {
namespace {

constexpr const char* kArpExt = ".arp";
constexpr int kTextMax = mt::kArpTextMax + 2;  // formatArpPattern's longest text + slack

}  // namespace

int listArps(char (*names)[hw::kNameMax], int max) { return listTexts(kArpDir, kArpExt, names, max); }

Result loadArp(const char* name, mt::ArpPattern& out) {
  char text[kTextMax + 1];
  const Result r = loadText(kArpDir, name, kArpExt, text, sizeof(text));
  if (r != Result::Ok) return r;
  return mt::parseArpPattern(text, out) ? Result::Ok : Result::BadFile;
}

Result saveArp(const char* name, const mt::ArpPattern& p) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  char text[kTextMax + 1];
  if (!mt::formatArpPattern(p, text, sizeof(text))) return Result::BadFile;
  return saveText(kArpDir, name, kArpExt, text);
}

}  // namespace storage
