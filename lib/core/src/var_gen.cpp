#include "var_gen.h"
#include <string.h>
#include "evolve.h"
#include "shift_gen.h"

namespace mt {

namespace {
// varRand kinds.
enum : uint32_t { kSaltNoise = 1, kSaltTake, kSaltPick };
// CA2D rules: bit k = k live neighbours.
struct LifeRule {
  uint16_t born, survive;
};
constexpr LifeRule kLife[] = {
    {1u << 3, 1u << 2 | 1u << 3},                              // LIFE B3/S23
    {1u << 3 | 1u << 6, 1u << 2 | 1u << 3},                    // HIGHLIFE B36/S23
    {1u << 2, 0},                                              // SEEDS B2/S
    {1u << 3 | 1u << 6 | 1u << 7 | 1u << 8, 1u << 3 | 1u << 4 | 1u << 6 | 1u << 7 | 1u << 8},  // DAYNIGHT
    {1u << 3, 1u << 1 | 1u << 2 | 1u << 3 | 1u << 4 | 1u << 5},  // MAZE B3/S12345
};
static_assert(sizeof(kLife) / sizeof(kLife[0]) == static_cast<int>(VarRule2::Count), "a rule per VarRule2");

int clampLen(int len) { return len < 0 ? 0 : (len > kMaxSteps ? kMaxSteps : len); }
int popcount16(uint16_t v) {
  int k = 0;
  for (; v; v &= static_cast<uint16_t>(v - 1)) ++k;
  return k;
}
bool allDead(const VarCells& c) {
  for (int r = 0; r < kVarRows; ++r)
    for (int b = 0; b < kVarRowBytes; ++b)
      if (c.r[r][b]) return false;
  return true;
}
// The step without its note (fx kept), or the step itself when it has none.
const Step& dropNote(const Step& base, Step& tmp) {
  if (!base.hasNote()) return base;
  tmp = base;
  clearNote(tmp);
  return tmp;
}
}  // namespace

uint32_t varRand(uint32_t seed, int track, uint32_t n, int i, uint32_t salt) {
  return genHash(seed ^ (salt * 0x9E3779B1u) ^ 0x564152u,  // "VAR": not evolve's stream
                 (static_cast<uint32_t>(track) & 0xFFu) | (static_cast<uint32_t>(i) << 8), n);
}

VarMode varModeOf(const VarCfg& c, bool kit) {
  if (c.mode >= static_cast<uint8_t>(VarMode::Count)) return VarMode::Off;
  const VarMode m = static_cast<VarMode>(c.mode);
  if (m == VarMode::Evo) return VarMode::Off;  // MUT plays EVO: the VAR engine is off
  return m == VarMode::Ca2d && !kit ? VarMode::Ca1d : m;
}

void varReset(VarTrack& s) {
  s.n = 0;
  s.sinceReset = 0;
  s.mode = 0;
  memset(&s.cells, 0, sizeof(s.cells));
}

void varClamp(VarCfg& c) {
  if (c.mode >= static_cast<uint8_t>(VarMode::Count)) c.mode = 0;
  if (c.every < 1) c.every = 1;
  if (c.every > kVarEveryMax) c.every = kVarEveryMax;
  if (c.rule2 >= static_cast<uint8_t>(VarRule2::Count)) c.rule2 = 0;
  if (c.reset > kVarResetMax) c.reset = kVarResetMax;
  if (c.noise > 100) c.noise = 100;
  if (c.amount > 100) c.amount = 100;
  if (c.order < 1) c.order = 1;
  if (c.order > 2) c.order = 2;
  if (c.rhythm > 1) c.rhythm = 1;
}

void varCa1d(uint8_t* row, int len, uint8_t rule) {
  len = clampLen(len);
  uint8_t out[kVarRowBytes] = {};
  for (int i = 0; i < len; ++i) {
    const int idx = varBit(row, (i + len - 1) % len) << 2 | varBit(row, i) << 1 | varBit(row, (i + 1) % len);
    if ((rule >> idx) & 1) varSetBit(out, i, true);
  }
  memcpy(row, out, sizeof(out));
}

void varCa2d(VarCells& c, int len, uint8_t rule2) {
  len = clampLen(len);
  const LifeRule& lr = kLife[rule2 < static_cast<uint8_t>(VarRule2::Count) ? rule2 : 0];
  // In place, row by row (stack: 3 rows, not a VarCells): prev = row r - 1 as it was, first = row 0 as it
  // was (the torus' row below the last).
  uint8_t first[kVarRowBytes], prev[kVarRowBytes], out[kVarRowBytes];
  memcpy(first, c.r[0], sizeof(first));
  memcpy(prev, c.r[kVarRows - 1], sizeof(prev));
  for (int r = 0; r < kVarRows; ++r) {
    const uint8_t* const nb[3] = {prev, c.r[r], r + 1 < kVarRows ? c.r[r + 1] : first};
    memset(out, 0, sizeof(out));
    for (int i = 0; i < len; ++i) {
      int k = 0;
      for (int dr = 0; dr < 3; ++dr)
        for (int di = -1; di <= 1; ++di)
          if (dr != 1 || di) k += varBit(nb[dr], (i + di + len) % len);
      const uint16_t m = varBit(c.r[r], i) ? lr.survive : lr.born;
      if ((m >> k) & 1) varSetBit(out, i, true);
    }
    memcpy(prev, c.r[r], sizeof(prev));
    memcpy(c.r[r], out, sizeof(out));
  }
}

void varGen0(const Pattern& pt, int tr, bool kit, VarCells& out) {
  memset(&out, 0, sizeof(out));
  const int len = evoLen(pt, tr);
  for (int i = 0; i < len; ++i) {
    const Step& st = pt.steps[tr][i];
    if (!st.hasNote()) continue;
    if (!kit) varSetBit(out.r[0], i, true);
    else
      for (int k = 0; k < kVarRows; ++k)
        if (laneOn(st, k)) varSetBit(out.r[k], i, true);
  }
}

void varCaNext(VarTrack& s, const VarCfg& cfg, const VarCells& gen0, int len, bool kit, uint32_t seed, int track,
               uint16_t lanes) {
  VarCfg c = cfg;
  varClamp(c);
  len = clampLen(len);
  const VarMode mode = varModeOf(c, kit) == VarMode::Ca2d ? VarMode::Ca2d : VarMode::Ca1d;
  const uint32_t n = ++s.n;
  const VarCells& cur = s.sinceReset == 0 ? gen0 : s.cells;
  const int rows = kit ? kVarRows : 1;
  VarCells next = cur;
  if (!kit) memset(next.r[1], 0, sizeof(next) - sizeof(next.r[0]));  // melodic: row 0 only
  if (mode == VarMode::Ca2d) varCa2d(next, len, c.rule2);
  else
    for (int r = 0; r < rows; ++r) varCa1d(next.r[r], len, c.rule);
  if (len > 0) {
    const uint32_t h = varRand(seed, track, n, 0, kSaltNoise);
    if (h % 100u < c.noise) {
      uint16_t live = 1;  // rows the noise may flip
      if (kit) {
        live = lanes;
        for (int r = 0; r < kVarRows; ++r)
          for (int b = 0; b < kVarRowBytes; ++b)
            if (gen0.r[r][b]) live = static_cast<uint16_t>(live | (1u << r));
        if (!live) live = 0x00FF;  // nothing in use: lanes 1-8 (as with 8 lanes)
      }
      const int cell = static_cast<int>((h / 100u) % static_cast<uint32_t>(popcount16(live) * len));
      int r = 0;
      for (int k = cell / len;; ++r)
        if (((live >> r) & 1) && k-- == 0) break;
      varSetBit(next.r[r], cell % len, !varBit(next.r[r], cell % len));
    }
  }
  s.mode = static_cast<uint8_t>(mode);
  if (s.sinceReset < 255) ++s.sinceReset;
  if ((c.reset && s.sinceReset >= c.reset) || allDead(next) || !memcmp(&next, &cur, sizeof(next))) {
    s.cells = gen0;
    s.sinceReset = 0;
  } else {
    s.cells = next;
  }
}

const Step& varCellStep(const Pattern& pt, int tr, int idx, const VarCells& c, bool kit, uint8_t root,
                        Step& tmp) {
  const int len = evoLen(pt, tr);
  const Step& base = pt.steps[tr][idx < 0 ? 0 : idx];
  if (idx < 0 || idx >= len) return base;
  uint16_t mask = 0;
  if (kit) {
    for (int k = 0; k < kVarRows; ++k)
      if (varBit(c.r[k], idx)) mask = static_cast<uint16_t>(mask | (1u << k));
  } else {
    mask = varBit(c.r[0], idx);
  }
  if (!mask) return dropNote(base, tmp);
  if (base.hasNote()) {
    if (!kit || laneMask(base) == mask) return base;
    tmp = base;
    setLaneMask(tmp, mask);
    return tmp;
  }
  int j = -1;
  for (int d = 1; d < len && j < 0; ++d) {
    const int k = (idx - d + len) % len;
    if (pt.steps[tr][k].hasNote()) j = k;
  }
  if (j >= 0) {
    tmp = pt.steps[tr][j];
  } else {
    tmp = Step();
    tmp.note = kit ? 0 : static_cast<uint8_t>(root % 12 + kVarRootOct);  // KIT: 0 = the track's velocity
  }
  if (kit) setLaneMask(tmp, mask);
  return tmp;
}

void varSeq(const Pattern& pt, int tr, bool kit, bool rhythm, VarSeq& q) {
  q.m = 0;
  q.ntok = 0;
  const int len = evoLen(pt, tr);
  for (int i = 0; i < len; ++i) {  // the tokens, in order of appearance
    const Step& st = pt.steps[tr][i];
    if (!st.hasNote() || q.ntok >= kVarTokMax) continue;
    const uint16_t v = kit ? laneMask(st) : st.note;
    bool have = false;
    for (int t = 0; t < q.ntok && !have; ++t) have = q.val[t] == v;
    if (have) continue;
    q.val[q.ntok] = v;
    q.first[q.ntok] = static_cast<uint8_t>(i);
    ++q.ntok;
  }
  for (int i = 0; i < len; ++i) {
    const Step& st = pt.steps[tr][i];
    uint8_t t = q.ntok;  // REST
    if (st.hasNote()) {
      const uint16_t v = kit ? laneMask(st) : st.note;
      int best = 1 << 10;
      for (int k = 0; k < q.ntok; ++k) {
        const int d = kit ? popcount16(static_cast<uint16_t>(v ^ q.val[k])) : (v > q.val[k] ? v - q.val[k] : q.val[k] - v);
        if (d < best) {
          best = d;
          t = static_cast<uint8_t>(k);
        }
      }
    } else if (!rhythm) {
      continue;
    }
    q.pos[q.m] = static_cast<uint8_t>(i);
    q.tok[q.m] = t;
    ++q.m;
  }
}

void varMarkov(const Pattern& pt, int tr, const VarCfg& cfg, bool kit, uint32_t seed, uint32_t n, uint8_t* src) {
  VarCfg c = cfg;
  varClamp(c);
  for (int i = 0; i < kMaxSteps; ++i) src[i] = static_cast<uint8_t>(i);
  VarSeq q;
  varSeq(pt, tr, kit, c.rhythm != 0, q);
  const int m = q.m;
  if (!m || !c.amount) return;
  uint8_t out[kMaxSteps];
  out[0] = q.tok[0];  // the start: the pattern's first token
  for (int k = 1; k < m; ++k) {
    out[k] = q.tok[k];
    if (varRand(seed, tr, n, k, kSaltTake) % 100u >= c.amount) continue;
    const uint8_t h1 = out[k - 1];
    const uint8_t h2 = k >= 2 ? out[k - 2] : q.tok[(k - 2 + m) % m];
    auto match = [&](int j) { return q.tok[j] == h1 && (c.order < 2 || q.tok[(j - 1 + m) % m] == h2); };
    int cnt = 0;
    for (int j = 0; j < m; ++j) cnt += match(j);
    if (!cnt) continue;  // no successors: the pattern's
    int pick = static_cast<int>(varRand(seed, tr, n, k, kSaltPick) % static_cast<uint32_t>(cnt));
    for (int j = 0; j < m; ++j)
      if (match(j) && pick-- == 0) {
        out[k] = q.tok[(j + 1) % m];
        break;
      }
  }
  for (int k = 0; k < m; ++k)
    if (out[k] != q.tok[k]) src[q.pos[k]] = out[k] == q.ntok ? kVarRest : q.first[out[k]];
}

void varNext(VarTrack& s, const VarCfg& cfg, const Pattern& pt, int tr, bool kit, uint32_t seed, uint16_t lanes) {
  VarCfg c = cfg;
  varClamp(c);
  const VarMode mode = varModeOf(c, kit);
  if (mode == VarMode::Off) return;
  if (s.mode != static_cast<uint8_t>(mode)) s.sinceReset = 0;  // another mode: from generation 0
  if (mode == VarMode::Markov) {
    ++s.n;
    varMarkov(pt, tr, c, kit, seed, s.n, s.src);
    s.mode = static_cast<uint8_t>(mode);
    s.sinceReset = 1;
    return;
  }
  VarCells g0;
  varGen0(pt, tr, kit, g0);
  varCaNext(s, c, g0, evoLen(pt, tr), kit, seed, tr, lanes);
}

VarApply varApply(const VarCfg& c, bool kit, uint8_t root) {
  VarApply a;
  a.mode = static_cast<uint8_t>(varModeOf(c, kit));
  a.kit = kit;
  a.rhythm = c.rhythm != 0;
  a.root = root;
  return a;
}

VarApply varApplyOf(const Project& p, int tr) {
  if (tr < 0 || tr >= kTracks) return VarApply();
  return varApply(p.var[tr], p.trackIsDrum(tr), p.scaleRoot);
}

const Step& varStep(const Pattern& pt, int tr, int idx, const VarTrack& s, const VarApply& a, Step& tmp) {
  const Step& base = pt.steps[tr][idx < 0 ? 0 : idx];
  const int len = evoLen(pt, tr);
  if (a.mode == static_cast<uint8_t>(VarMode::Off) || s.sinceReset == 0 || s.mode != a.mode || idx < 0 ||
      idx >= len)
    return base;
  if (a.mode != static_cast<uint8_t>(VarMode::Markov)) return varCellStep(pt, tr, idx, s.cells, a.kit, a.root, tmp);
  const uint8_t j = s.src[idx];
  if (j == idx) return base;
  if (j == kVarRest) return dropNote(base, tmp);
  if (j >= len || !pt.steps[tr][j].hasNote()) return base;
  const Step& from = pt.steps[tr][j];
  if (a.rhythm) return from;
  if (!base.hasNote()) return base;
  tmp = base;
  if (a.kit) setLaneMask(tmp, laneMask(from));
  else tmp.note = from.note;
  return tmp;
}

}  // namespace mt
