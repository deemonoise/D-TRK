#include "tuning_store.h"
#include <string.h>
#include "text_store.h"

namespace storage {
namespace {

constexpr const char* kSclExt = ".scl";
char text[mt::kSclFileMax + 2];  // one byte over the limit tells a too large file

}  // namespace

int listTunings(char (*names)[hw::kNameMax], int max) { return listTexts(kTunDir, kSclExt, names, max); }

Result loadTuning(const char* name, mt::TuningTable& t, mt::ParseErr& err, int& errLine) {
  err = mt::ParseErr::Ok;
  errLine = 0;
  if (!validName(name)) return Result::BadFile;
  const Result r = loadText(kTunDir, name, kSclExt, text, sizeof(text));
  if (r != Result::Ok) return r;
  const int n = static_cast<int>(strlen(text));
  if (n > mt::kSclFileMax) {
    err = mt::ParseErr::TooBig;
    return Result::BadFile;
  }
  mt::SclTable s;
  err = mt::parseScl(text, n, s, &errLine);
  if (err != mt::ParseErr::Ok) return Result::BadFile;
  mt::sclToTuning(s, name, t);
  return Result::Ok;
}

}  // namespace storage
