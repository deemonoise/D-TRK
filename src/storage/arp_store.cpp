#include "arp_store.h"
#include <stdio.h>
#include <string.h>

namespace storage {
namespace {

// Longest token "Xs~r^" (5) + blank per step: 32 * 6 - 1 = 191 chars; the rest is slack.
constexpr int kTextMax = mt::kArpPatMax * 7 + 2;

bool arpPath(char* out, size_t cap, const char* name, const char* ext) {
  const int n = snprintf(out, cap, "%s/%s%s", kArpDir, name, ext);
  return n > 0 && static_cast<size_t>(n) < cap;
}

}  // namespace

int listArps(char (*names)[hw::kNameMax], int max) {
  if (!hw::sdReady() || !hw::sdFs().exists(kArpDir)) return 0;
  return hw::sdList(kArpDir, ".arp", names, max, validName);
}

Result loadArp(const char* name, mt::ArpPattern& out) {
  if (!hw::sdReady()) return Result::NoSd;
  char path[192];
  if (!arpPath(path, sizeof(path), name, ".arp")) return Result::BadFile;
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) return Result::NotFound;
  char text[kTextMax + 1];
  const size_t n = f.read(reinterpret_cast<uint8_t*>(text), kTextMax);
  f.close();
  text[n] = '\0';
  return mt::parseArpPattern(text, out) ? Result::Ok : Result::BadFile;
}

Result saveArp(const char* name, const mt::ArpPattern& p) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  fs::FS& fs = hw::sdFs();
  if (!fs.exists("/presets")) fs.mkdir("/presets");
  if (!fs.exists(kArpDir) && !fs.mkdir(kArpDir)) return Result::WriteFail;
  char tmp[192], arp[192], bak[192], text[kTextMax + 1];
  if (!arpPath(tmp, sizeof(tmp), name, ".tmp") || !arpPath(arp, sizeof(arp), name, ".arp") ||
      !arpPath(bak, sizeof(bak), name, ".bak"))
    return Result::BadFile;
  if (!mt::formatArpPattern(p, text, sizeof(text))) return Result::BadFile;
  if (fs.exists(tmp)) fs.remove(tmp);
  fs::File f = fs.open(tmp, FILE_WRITE);
  if (!f) return Result::WriteFail;
  const size_t len = strlen(text);
  bool ok = f.write(reinterpret_cast<const uint8_t*>(text), len) == len && f.write('\n') == 1;
  f.close();
  // The old file moves to .bak first, so a failed rename can put it back.
  bool hadOld = false;
  if (ok && fs.exists(arp)) {
    if (fs.exists(bak)) fs.remove(bak);
    ok = fs.rename(arp, bak);
    hadOld = ok;
  }
  ok = ok && fs.rename(tmp, arp);
  if (!ok) {
    if (fs.exists(tmp)) fs.remove(tmp);
    if (hadOld && !fs.exists(arp)) fs.rename(bak, arp);
    return Result::WriteFail;
  }
  if (hadOld) fs.remove(bak);
  return Result::Ok;
}

}  // namespace storage
