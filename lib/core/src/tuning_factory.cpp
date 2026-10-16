#include "tuning_factory.h"
#include <string.h>
#include "scala.h"

namespace mt {

namespace {

struct Factory {
  const char* name;
  const char* scl;
};

// Sources: the Scala archive's well-known scales (ptolemy / pyth_12 / meanquar / werck3 / bohlen-p),
// equal divisions computed here, gamelan scales as common approximations (real sets vary by ensemble).
const Factory kFactory[] = {
    {"JUST 5-LIMIT", "5-limit just chromatic\n12\n16/15\n9/8\n6/5\n5/4\n4/3\n45/32\n3/2\n8/5\n5/3\n9/5\n15/8\n2/1\n"},
    {"PYTHAGOREAN",
     "Pythagorean, wolf G#-Eb\n12\n256/243\n9/8\n32/27\n81/64\n4/3\n729/512\n3/2\n128/81\n27/16\n16/9\n243/128\n2/1\n"},
    {"MEANTONE 1/4",
     "1/4-comma meantone, Eb..G#\n12\n76.049\n193.157\n310.265\n386.314\n503.422\n579.471\n696.578\n772.627\n"
     "889.735\n1006.843\n1082.892\n2/1\n"},
    {"WERCKMEISTER3",
     "Werckmeister III\n12\n90.225\n192.180\n294.135\n390.225\n498.045\n588.270\n696.090\n792.180\n888.270\n"
     "996.090\n1092.180\n2/1\n"},
    {"19-EDO", nullptr},
    {"24-EDO", nullptr},
    {"31-EDO", nullptr},
    {"SLENDRO", "Slendro (approximation)\n5\n231.0\n474.0\n717.0\n955.0\n2/1\n"},
    {"PELOG", "Pelog (approximation)\n7\n120.0\n270.0\n540.0\n670.0\n785.0\n950.0\n2/1\n"},
    {"BOHLEN-PIERCE",
     "Bohlen-Pierce, just\n13\n27/25\n25/21\n9/7\n7/5\n75/49\n5/3\n9/5\n49/25\n15/7\n7/3\n63/25\n25/9\n3/1\n"},
};
constexpr int kCount = sizeof(kFactory) / sizeof(kFactory[0]);

// "19-EDO" -> 19, else 0.
int edoOf(const char* name) {
  int n = 0;
  for (const char* p = name; *p >= '0' && *p <= '9'; ++p) n = n * 10 + (*p - '0');
  return n;
}

}  // namespace

int factoryTuningCount() { return kCount; }

const char* factoryTuningName(int i) { return i >= 0 && i < kCount ? kFactory[i].name : ""; }

bool factoryTuning(int i, TuningTable& out) {
  if (i < 0 || i >= kCount) return false;
  const Factory& f = kFactory[i];
  SclTable s;
  if (f.scl) {
    if (parseScl(f.scl, static_cast<int>(strlen(f.scl)), s) != ParseErr::Ok) return false;
  } else {  // n equal steps of the octave
    const int n = edoOf(f.name);
    if (n < 1 || n > kTuneDegMax) return false;
    s.count = static_cast<uint8_t>(n);
    for (int k = 1; k <= n; ++k) s.mc[k - 1] = static_cast<int32_t>((1200000LL * k + n / 2) / n);
  }
  sclToTuning(s, f.name, out);
  return true;
}

int factoryTuningFind(const char* name) {
  for (int i = 0; i < kCount; ++i)
    if (name && strcmp(name, kFactory[i].name) == 0) return i;
  return -1;
}

}  // namespace mt
