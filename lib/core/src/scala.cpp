#include "scala.h"
#include <math.h>
#include <string.h>

namespace mt {

namespace {

bool blank(char c) { return c == ' ' || c == '\t'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

// [b, e): the line's first word.
void firstWord(const char*& b, const char*& e) {
  while (b < e && blank(*b)) ++b;
  const char* w = b;
  while (w < e && !blank(*w)) ++w;
  e = w;
}

// Digits [b, e) as an integer below 2^31; false if empty, not digits or too large.
bool parseUint(const char* b, const char* e, int64_t& v) {
  if (b >= e) return false;
  v = 0;
  for (; b < e; ++b) {
    if (!digit(*b)) return false;
    v = v * 10 + (*b - '0');
    if (v >= (int64_t(1) << 31)) return false;
  }
  return true;
}

bool parsePitch(const char* b, const char* e, int32_t& mc) {
  firstWord(b, e);
  if (b >= e) return false;
  double cents;
  if (memchr(b, '.', static_cast<size_t>(e - b))) {
    const char* p = b;
    bool neg = false;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    double ip = 0, fp = 0, sc = 1;
    int nd = 0;
    for (; p < e && digit(*p); ++p, ++nd) ip = ip * 10 + (*p - '0');
    if (p >= e || *p != '.') return false;
    for (++p; p < e && digit(*p); ++p, ++nd) fp += (*p - '0') * (sc *= 0.1);
    if (p != e || nd == 0) return false;
    cents = neg ? -(ip + fp) : ip + fp;
  } else {
    const char* sl = static_cast<const char*>(memchr(b, '/', static_cast<size_t>(e - b)));
    int64_t n = 0, d = 1;
    if (!parseUint(b, sl ? sl : e, n)) return false;
    if (sl && !parseUint(sl + 1, e, d)) return false;
    if (n <= 0 || d <= 0) return false;
    cents = 1200.0 * log2(static_cast<double>(n) / static_cast<double>(d));
  }
  const double m = cents * 1000.0;
  if (!(m <= kTuneDegAbsMaxMc && m >= -kTuneDegAbsMaxMc)) return false;
  mc = static_cast<int32_t>(m < 0 ? m - 0.5 : m + 0.5);
  return true;
}

}  // namespace

const char* sclErrName(ParseErr e) {
  switch (e) {
    case ParseErr::Ok: return "OK";
    case ParseErr::NoCount: return "NO NOTE COUNT";
    case ParseErr::BadCount: return "BAD NOTE COUNT";
    case ParseErr::Empty: return "NO NOTES";
    case ParseErr::TooMany: return "TOO MANY NOTES (MAX 64)";
    case ParseErr::BadValue: return "BAD PITCH";
    case ParseErr::Truncated: return "TOO FEW NOTES";
    case ParseErr::BadPeriod: return "BAD PERIOD";
    case ParseErr::TooBig: return "FILE TOO BIG";
  }
  return "BAD SCL";
}

ParseErr parseScl(const char* text, int len, SclTable& out, int* errLine) {
  out = SclTable();
  if (errLine) *errLine = 0;
  enum { kDesc, kCount, kPitch } state = kDesc;
  int want = 0, line = 0;
  const char* p = text;
  const char* end = text + (len > 0 ? len : 0);
  auto fail = [&](ParseErr e) {
    if (errLine) *errLine = line;
    return e;
  };
  while (p < end) {
    const char* b = p;
    while (p < end && *p != '\n' && *p != '\r') ++p;
    const char* e = p;
    if (p < end) {  // CR, LF or CRLF ends the line
      if (*p == '\r' && p + 1 < end && p[1] == '\n') ++p;
      ++p;
    }
    ++line;
    if (b < e && *b == '!') continue;
    if (state == kDesc) {
      int n = static_cast<int>(e - b);
      if (n > kSclDescMax) n = kSclDescMax;
      memcpy(out.desc, b, static_cast<size_t>(n));
      out.desc[n] = 0;
      state = kCount;
    } else if (state == kCount) {
      firstWord(b, e);
      int64_t n = 0;
      if (!parseUint(b, e, n)) return fail(ParseErr::BadCount);
      if (n == 0) return fail(ParseErr::Empty);
      if (n > kTuneDegMax) return fail(ParseErr::TooMany);
      want = static_cast<int>(n);
      state = kPitch;
    } else {
      int32_t mc;
      if (!parsePitch(b, e, mc)) return fail(ParseErr::BadValue);
      out.mc[out.count++] = mc;
      if (out.count == want) {
        if (mc <= 0 || mc > kTunePeriodMaxMc) return fail(ParseErr::BadPeriod);
        return ParseErr::Ok;
      }
    }
  }
  if (state != kPitch) return fail(ParseErr::NoCount);
  return fail(ParseErr::Truncated);
}

void sclToTuning(const SclTable& s, const char* name, TuningTable& t) {
  const uint8_t root = t.root;
  tuningClear(t);
  t.root = root;
  strncpy(t.name, name ? name : "", sizeof(t.name) - 1);
  t.count = s.count;
  for (int i = 0; i < s.count && i < kTuneDegMax; ++i) t.mc[i] = s.mc[i];
}

}  // namespace mt
