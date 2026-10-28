#include "dice.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "gen_util.h"
#include "groove.h"
#include "preset_io.h"
#include "presets_factory.h"
#include "rng.h"

namespace mt {
namespace {

static_assert(kInstruments <= 32 && kDiceRoles <= 8, "dice masks");

constexpr const char* kRoleNames[kDiceRoles] = {"DRUMS", "BASS", "CHORDS", "LEAD", "ARP", "FX"};
constexpr uint32_t kCanonSeed = 0x0D1CE5u;  // generator seed at Variety 0
constexpr int kLeadBase = 72;              // C5: LEAD motifs' tonic octave
constexpr int kFxNote = 60;
constexpr int kLeadMain = 6;  // w.full track of MAIN / FILL's lead

// ---- Weighted picks ------------------------------------------------------------------------------
template <class T>
int listLen(const T* l) {
  int n = 0;
  if (l)
    while (l[n].w) ++n;
  return n;
}

// Variety 0: the heaviest (first of them), no draw. Above: weights flattened towards the heaviest's, so
// at 100 every option is as likely.
template <class T>
int pick(const T* l, int v, Rng& r) {
  const int n = listLen(l);
  if (n == 0) return -1;
  int best = 0;
  for (int i = 1; i < n; ++i)
    if (l[i].w > l[best].w) best = i;
  if (v <= 0) return best;
  const uint32_t mx = l[best].w;
  auto weight = [&](int i) { return static_cast<uint32_t>(l[i].w) * static_cast<uint32_t>(100 - v) + mx * v; };
  uint32_t total = 0;
  for (int i = 0; i < n; ++i) total += weight(i);
  uint32_t k = r.below(total);
  for (int i = 0; i < n; ++i) {
    if (k < weight(i)) return i;
    k -= weight(i);
  }
  return n - 1;
}

int byName(int n, const char* (*nameOf)(int), const char* s) {
  if (!s) return -1;
  for (int i = 0; i < n; ++i) {
    const char* x = nameOf(i);
    if (x && strcmp(x, s) == 0) return i;
  }
  return -1;
}

int presetIndex(InstrType t, const char* name) {
  if (!name) return -1;
  for (int i = 0; i < factoryCount(t); ++i)
    if (strcmp(factoryPreset(t, i).name, name) == 0) return i;
  return -1;
}

int grooveIndex(const char* name) {
  if (!name) return 0;
  for (int i = 1; i < grooveCount(); ++i)
    if (strcmp(grooveAt(i).name, name) == 0) return i;
  return 0;
}

// ---- Slots (which instrument each role writes) ---------------------------------------------------
bool isRoleTrack(int t) { return t >= 0 && t < kDiceRoles; }

void markPlayed(const TrackCfg* tracks, const Instrument* instruments, const TrackCfg& c, bool* used,
                bool lanesOnly = false) {
  (void)tracks;
  if (c.out != TrackOut::Int) return;
  const int i = c.instr % kInstruments;
  if (!lanesOnly) used[i] = true;
  if (instruments[i].type != InstrType::Kit) return;
  for (const KitLane& l : instruments[i].kit)
    if (l.instr < kInstruments) used[l.instr] = true;
}

// role[k]: track k's settings (k < kDiceRoles); all: every track's.
void slotsOf(const TrackCfg* role, const TrackCfg* all, const Instrument* instruments, uint8_t out[kDiceRoles]) {
  bool used[kInstruments] = {};  // played by a track the roll leaves alone
  bool own[kInstruments] = {};   // a role track's instrument: kept for that role
  for (int t = 0; t < kTracks; ++t)
    if (!isRoleTrack(t)) markPlayed(all, instruments, all[t], used);
  for (int k = 0; k < kDiceRoles; ++k) {
    own[role[k].instr % kInstruments] = true;
    if (k > 0) markPlayed(all, instruments, role[k], used, true);  // a KIT on a melodic role
  }
  const int kit = role[0].instr % kInstruments;
  const int block = factoryKitBlock(kit);
  bool free = !used[kit];
  for (int l = 0; l < kKitLanes; ++l) free &= !used[block + l];
  if (free) {
    out[0] = static_cast<uint8_t>(kit);
    used[kit] = true;
    for (int l = 0; l < kKitLanes; ++l) used[block + l] = true;
  } else {
    out[0] = kNoInstr;
    markPlayed(all, instruments, role[0], used);  // the kit stays, so do its lanes
  }
  for (int k = 1; k < kDiceRoles; ++k) {
    int s = role[k].instr % kInstruments;
    if (used[s]) {
      s = 0;
      while (s < kInstruments && (used[s] || own[s])) ++s;
    }
    if (s >= kInstruments) {
      out[k] = kNoInstr;
      continue;
    }
    out[k] = static_cast<uint8_t>(s);
    used[s] = true;
  }
}

// The role plays the notes as written: FM only as TONE (the other machines are one-shot drums or a chord
// of their own; FX takes them), CHORDS needs polyphony, no KIT on a melodic role.
bool roleFits(DiceRole role, const Instrument& m) {
  if (m.type == InstrType::Kit || m.type == InstrType::Sample) return false;
  if (role == DiceRole::Fx) return true;
  if (m.type == InstrType::Fm && m.machine != static_cast<uint8_t>(FmMachine::Tone)) return false;
  return !(role == DiceRole::Chords && m.mono);
}

// ---- Steps ---------------------------------------------------------------------------------------
int effVel(uint8_t v) { return v ? v : 100; }
bool isHit(char c) { return c == 'x' || c == 'X' || c == 'o' || (c >= '2' && c <= '4'); }
uint8_t hitVel(char c) { return c == 'X' ? 127 : c == 'o' ? 45 : 0; }

// Drum hit velocity of char c at step i by the style's feel ('X' accent, 'o' ghost, else by position).
uint8_t feelVel(const DiceFeel& f, char c, int i) { return c == 'X' ? f.accent : c == 'o' ? f.ghost : f.vel[i & 3]; }

// Lane hit at step i: the step's velocity is its loudest hit's.
void drumHit(Step& st, int lane, uint8_t v) {
  if (!st.hasNote() || effVel(v) > effVel(st.note)) st.note = v;
  st.vel = static_cast<uint8_t>(st.vel | (1u << lane));
}

void clearSteps(Step* tr) {
  for (int i = 0; i < kMaxSteps; ++i) tr[i] = Step();
}

void putNote(Step& st, int note, uint8_t vel, int gatePct) {
  st = Step();
  st.note = static_cast<uint8_t>(note < 0 ? 0 : note > 127 ? 127 : note);
  st.vel = vel;
  if (gatePct > 0) putFx(st, Fx::GAT, gateValue(gatePct));
}

bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

// Drum lane of each DiceLane in the kit, by its drums' names (-1 = none).
void kitLanes(const Instrument& kit, const Instrument* instruments, int8_t out[kDiceLanes]) {
  int8_t cls[kKitLanes];
  const char* names[kKitLanes];
  for (int l = 0; l < kKitLanes; ++l) {
    cls[l] = -1;
    const uint8_t i = kit.kit[l].instr;
    names[l] = i < kInstruments ? instruments[i].name : "";
  }
  for (int l = 0; l < kKitLanes; ++l) {
    const char* n = names[l];
    if (!n[0]) continue;
    if (startsWith(n, "BD") || strstr(n, "KICK")) cls[l] = kDiceKick;
    else if (startsWith(n, "OH") || strstr(n, " OH") || strstr(n, "HAT O") || strstr(n, "HHR O") ||
             (startsWith(n, "HAT") && n[strlen(n) - 1] == 'O'))  // FM HAT 909O
      cls[l] = kDiceOpen;
    else if (startsWith(n, "CH") || startsWith(n, "HH") || startsWith(n, "HAT") || strstr(n, "HHR C") ||
             strstr(n, " HH"))  // HAT C, HAT TGHT, HAT SOFT...
      cls[l] = kDiceHat;
    else if (startsWith(n, "SD") || strstr(n, "SN")) cls[l] = kDiceSnare;
    else if (startsWith(n, "CP") || strstr(n, "CLAP")) cls[l] = kDiceClap;
  }
  for (int k = 0; k < kDiceLanes; ++k) out[k] = -1;
  for (int l = kKitLanes - 1; l >= 0; --l)
    if (cls[l] >= 0) out[cls[l]] = static_cast<int8_t>(l);  // the first lane of each wins
  static const char* const kPerc[] = {"RS", "RIM", "CL", "CB", "COW", "CONGA", "WOOD", "METAL", "BLIP", "TOM"};
  for (const char* pre : kPerc) {
    for (int l = 0; l < kKitLanes && out[kDicePerc] < 0; ++l)
      if (cls[l] < 0 && names[l][0] && startsWith(names[l], pre)) out[kDicePerc] = static_cast<int8_t>(l);
    if (out[kDicePerc] >= 0) break;
  }
  for (int l = 0; l < kKitLanes && out[kDicePerc] < 0; ++l)
    if (cls[l] < 0 && names[l][0]) out[kDicePerc] = static_cast<int8_t>(l);
  if (out[kDiceKick] < 0 && out[kDiceSnare] < 0 && out[kDiceHat] < 0) {  // no names: the usual layout
    static constexpr int8_t kDefault[kDiceLanes] = {0, 1, 4, 2, 3, 5};
    memcpy(out, kDefault, sizeof(kDefault));
    return;
  }
  if (out[kDiceClap] < 0) out[kDiceClap] = out[kDiceSnare];
  if (out[kDiceOpen] < 0) out[kDiceOpen] = out[kDiceHat];
  if (out[kDiceHat] < 0) {  // no hats (FM PERC): a short perc lane other than PERC's ticks them
    static const char* const kTick[] = {"WOOD", "METAL", "RIDE", "CY", "BLIP", "CL", "RS", "RIM", "CONGA"};
    for (const char* pre : kTick) {
      for (int l = 0; l < kKitLanes && out[kDiceHat] < 0; ++l)
        if (cls[l] < 0 && l != out[kDicePerc] && names[l][0] && startsWith(names[l], pre))
          out[kDiceHat] = static_cast<int8_t>(l);
      if (out[kDiceHat] >= 0) break;
    }
    if (out[kDiceHat] < 0) out[kDiceHat] = out[kDicePerc];
    if (out[kDiceOpen] < 0 || out[kDiceOpen] == out[kDicePerc]) out[kDiceOpen] = out[kDiceHat];
  }
}

// ---- The roll ------------------------------------------------------------------------------------
struct Picks {
  int8_t drum[kDiceLanes];        // item of the recipe's kit lane list, -1 = none
  int8_t jd[kDiceLanes] = {}, jt[kDiceLanes] = {};  // Variety: DECAY / TONE macro offsets
  int sound[kDiceRoles];  // factory index of the role's preset (type: rec.sounds), -1 = none
  int8_t soundItem[kDiceRoles];
  const char* drums[kDiceLanes];
  int fill = 0;
  int prog = -1, rhythm = 0, bass = -1, arp = -1, leadArp = -1, motif = -1;
  uint8_t arpMode = 0, leadArpMode = 0, arpOct = 1;
  DiceLead lead = DiceLead::None;
  bool riser = false;
  const char* fxHits = nullptr;
  int percRot = 0;
  uint32_t seed = kCanonSeed;
};

struct Ctx {
  const DiceRecipe& rec;
  int v;
  Picks pk;
  uint8_t root;
  ScaleType sc;
  int len;
  Rng mr;  // mutations (Variety > 0)
  Rng pr;  // phrase variations: a fixed seed at Variety 0
  uint8_t drumVar;              // drum variation strength 1..3 (kTraits)
  uint8_t addNotes;             // chord notes added: 0 none, 1 7th / 9th, 2 sus (kTraits)
  uint8_t bar[kMaxSteps / 16];  // Bar of each phrase unit (phraseBars)
};

// Style traits (dice_recipes.cpp order): drum variation strength 1..3 (low nibble, 0 = 2), chord notes
// added (high nibble, Ctx::addNotes).
constexpr uint8_t kTraits[9][6] = {
    {0x12, 0x02, 0x02, 0x02, 0x02},        // HOUSE: DEEP 7/9
    {0x01, 0x01, 0x01, 0x01, 0x01, 0x01},  // TECHNO
    {0x22, 0x22, 0x22, 0x22, 0x22},        // TRANCE: sus
    {0x13, 0x03, 0x03, 0x03, 0x03},        // DNB: LIQUID 7/9
    {0x03, 0x03, 0x03},                    // BREAKS
    {0x03, 0x13, 0x03},                    // HIPHOP: LOFI 7/9
    {0x03, 0x03, 0x13},                    // IDM: ELECTRONICA 7/9
    {0x11, 0x11, 0x11},                    // AMBIENT 7/9
    {0x02, 0x02, 0x12},                    // SYNTHWAVE: CHILLWAVE 7/9
};

// Phrase: units of a bar (16 steps; 8 in a 16-step pattern), four to a phrase A A' A B (two units: A B;
// 128 steps: two phrases, the 8th bar a fill).
enum Bar : uint8_t { kBarA, kBarA2, kBarB, kBarBig };
int unitLen(int len) { return len <= 16 ? 8 : 16; }

void phraseBars(Ctx& c) {
  static constexpr uint8_t kPhrase[4] = {kBarA, kBarA2, kBarA, kBarB};
  const int n = c.len / unitLen(c.len);
  for (int j = 0; j < n; ++j) {
    uint8_t b = n == 2 ? (j ? kBarB : kBarA) : kPhrase[j % 4];
    if (n == 8 && j == 7) b = kBarBig;
    if (b == kBarA && j > 0 && c.v > 0 && c.pr.below(200) < static_cast<uint32_t>(c.v)) b = kBarA2;
    c.bar[j] = b;
  }
}

void rollPicks(Ctx& c, Rng& r) {
  const DiceRecipe& R = c.rec;
  Picks& k = c.pk;
  const int v = c.v;
  int i;
  for (int l = 0; l < kDiceLanes; ++l) {
    i = R.kit ? pick(R.kit->lane[l], v, r) : -1;
    k.drum[l] = static_cast<int8_t>(i);
    if (v > 0 && i >= 0) {  // a small spread around the drum: +-v/8
      const int a = v / 8;
      k.jd[l] = static_cast<int8_t>(static_cast<int>(r.below(static_cast<uint32_t>(2 * a + 1))) - a);
      k.jt[l] = static_cast<int8_t>(static_cast<int>(r.below(static_cast<uint32_t>(2 * a + 1))) - a);
    }
  }
  for (int role = 0; role < kDiceRoles; ++role) {
    k.sound[role] = -1;
    k.soundItem[role] = -1;
    if (role == 0 || !R.sounds[role]) continue;
    i = pick(R.sounds[role], v, r);
    if (i < 0) continue;
    k.soundItem[role] = static_cast<int8_t>(i);
    k.sound[role] = presetIndex(R.sounds[role][i].type, R.sounds[role][i].name);
  }
  for (int l = 0; l < kDiceLanes; ++l) {
    i = pick(R.drums[l], v, r);
    k.drums[l] = i >= 0 ? R.drums[l][i].name : nullptr;
  }
  k.fill = pick(diceFills(), v, r);
  i = pick(R.progs, v, r);
  k.prog = i >= 0 ? byName(progFactoryCount(), progFactoryName, R.progs[i].name) : -1;
  i = pick(R.rhythms, v, r);
  k.rhythm = i >= 0 ? byName(chordRhythmCount(), chordRhythmName, R.rhythms[i].name) : -1;
  if (k.rhythm < 0) k.rhythm = 0;
  i = pick(R.basses, v, r);
  k.bass = i >= 0 ? byName(bassFactoryCount(), bassFactoryName, R.basses[i].name) : -1;
  i = pick(R.arps, v, r);
  k.arp = i >= 0 ? byName(arpFactoryCount(), arpFactoryName, R.arps[i].name) : -1;
  i = pick(R.arpModes, v, r);
  k.arpMode = i >= 0 ? R.arpModes[i].v : 0;
  i = pick(R.leads, v, r);
  k.lead = i >= 0 ? static_cast<DiceLead>(R.leads[i].v) : DiceLead::None;
  i = pick(R.arps, v, r);
  k.leadArp = i >= 0 ? byName(arpFactoryCount(), arpFactoryName, R.arps[i].name) : -1;
  i = pick(R.arpModes, v, r);
  k.leadArpMode = i >= 0 ? R.arpModes[i].v : 0;
  i = pick(R.motifs, v, r);
  k.motif = i >= 0 ? byName(diceMotifCount(), diceMotifName, R.motifs[i].name) : -1;
  static constexpr DiceName kFxDefault[] = {{"x...............", 1}, {nullptr, 0}};
  const DiceName* fh = R.fxHits ? R.fxHits : kFxDefault;
  i = pick(fh, v, r);
  k.fxHits = i >= 0 ? fh[i].name : nullptr;
  if (k.sound[5] >= 0) k.riser = strcmp(factoryPreset(R.sounds[5][k.soundItem[5]].type, k.sound[5]).category, "FX") == 0;
  if (v > 0) {
    k.arpOct = static_cast<uint8_t>(r.below(100) < static_cast<uint32_t>(v) / 2 ? 2 : 1);
    k.percRot = r.below(100) < static_cast<uint32_t>(v) / 2 ? 1 + static_cast<int>(r.below(3)) : 0;
    k.seed = r.next();
  }
}

// Drum fill fill (diceFills) over steps [from, from + n), n 4 or 8.
void writeFill(Ctx& c, Step* tr, const int8_t lane[kDiceLanes], int fill, int from, int n) {
  const DiceFill& f = diceFills()[fill >= 0 ? fill : 0];
  uint8_t rat[8] = {};
  for (int i = from; i < from + n; ++i) tr[i] = Step();
  const int perc = lane[kDicePerc] >= 0 ? lane[kDicePerc] : lane[kDiceSnare];
  const struct {
    const char* s;
    int lane;
  } parts[] = {{f.kick, lane[kDiceKick]}, {f.snare, lane[kDiceSnare]}, {f.perc, perc}};
  for (const auto& pa : parts) {
    if (!pa.s || pa.lane < 0) continue;
    for (int i = 0; i < n; ++i) {
      const char ch = pa.s[8 - n + i];
      if (!isHit(ch)) continue;
      drumHit(tr[from + i], pa.lane, feelVel(*c.rec.feel, ch, from + i));
      if (ch >= '2' && ch <= '4') rat[i] = static_cast<uint8_t>(ch - '0');
    }
  }
  for (int i = 0; i < n; ++i) {
    const uint8_t m = tr[from + i].vel;
    if (rat[i] && tr[from + i].hasNote() && m && !(m & (m - 1))) putFx(tr[from + i], Fx::RAT, rat[i]);
  }
}

// ---- Phrase variations ---------------------------------------------------------------------------
bool hasLane(const Step& st, int l) { return l >= 0 && st.hasNote() && (st.vel & (1u << l)); }
void dropLane(Step& st, int l) {
  if (!hasLane(st, l)) return;
  st.vel = static_cast<uint8_t>(st.vel & ~(1u << l));
  if (!st.vel) st = Step();
}
// Adds lane l at s[o] (b + o in the pattern); false: already there.
bool addLane(const DiceFeel& f, Step* s, int b, int o, int l, uint8_t vel = 0) {
  if (l < 0 || hasLane(s[o], l)) return false;
  drumHit(s[o], l, vel ? vel : f.vel[(b + o) & 3]);
  return true;
}
void solo(Step& st, int l, uint8_t rat) {
  if (l >= 0 && st.hasNote() && st.vel == (1u << l)) putFx(st, Fx::RAT, rat);
}

// A' edit kind on unit s (u steps from b): 0 an extra kick, 1 a snare ghost, 2 the open hat on another
// beat, 3 a 16th hat. False: nothing changed.
bool drumEdit(const DiceFeel& f, Step* s, int u, int b, const int8_t* lane, int kind) {
  const int kick = lane[kDiceKick], sn = lane[kDiceSnare], oh = lane[kDiceOpen], hat = lane[kDiceHat];
  switch (kind) {
    case 0:
      for (int o : {u - 3, u - 6, u - 2})
        if (addLane(f, s, b, o, kick)) return true;
      return false;
    case 1:
      for (int o : {u - 1, u - 5, u / 2 - 1, 3})
        if (!s[o].hasNote() && addLane(f, s, b, o, sn, f.ghost)) return true;
      return false;
    case 2:
      if (oh < 0) return false;
      for (int o = 0; o < u; ++o) {
        const int t = (o + 2) % u;
        if (!hasLane(s[o], oh) || hasLane(s[t], oh)) continue;
        dropLane(s[o], oh);
        addLane(f, s, b, t, oh);
        return true;
      }
      return addLane(f, s, b, u - 2, oh);
    default:
      for (int o : {u - 1, u - 3, u / 2 + 1})
        if (addLane(f, s, b, o, hat, f.ghost)) return true;
      return false;
  }
}

// B mini-fill kind in the unit's last beat: 0 a snare roll, 1 a flam, 2 a kick pause and an open hat,
// 3 perc hits, 4 a triplet hat lift.
bool drumFill(const DiceFeel& f, Step* s, int u, int b, const int8_t* lane, int kind) {
  const int kick = lane[kDiceKick], sn = lane[kDiceSnare], hat = lane[kDiceHat];
  const int perc = lane[kDicePerc] >= 0 ? lane[kDicePerc] : lane[kDiceClap];
  bool ch = false;
  switch (kind) {
    case 0:
      for (int i = 0; i < 4; ++i) ch |= addLane(f, s, b, u - 4 + i, sn, feelVel(f, "oxxX"[i], b + u - 4 + i));
      break;
    case 1:
      ch = addLane(f, s, b, u - 2, sn, f.ghost);
      ch |= addLane(f, s, b, u - 1, sn, f.accent);
      if (ch) solo(s[u - 1], sn, 2);
      break;
    case 2:
      for (int o = u - 4; o < u; ++o)
        if (hasLane(s[o], kick)) dropLane(s[o], kick), ch = true;
      if (ch) addLane(f, s, b, u - 2, lane[kDiceOpen]);
      break;
    case 3:
      ch = addLane(f, s, b, u - 3, perc);
      ch |= addLane(f, s, b, u - 1, perc, f.accent);
      break;
    default:
      ch = addLane(f, s, b, u - 4, hat);
      ch |= addLane(f, s, b, u - 2, hat);
      if (ch) solo(s[u - 2], hat, 3);
      break;
  }
  return ch;
}

// Lane l of unit s turned by `by` steps.
void turnLane(const DiceFeel& f, Step* s, int u, int b, int l, int by) {
  if (l < 0) return;
  uint16_t hit = 0;
  for (int o = 0; o < u; ++o)
    if (hasLane(s[o], l)) hit = static_cast<uint16_t>(hit | (1u << o)), dropLane(s[o], l);
  for (int o = 0; o < u; ++o)
    if (hit & (1u << o)) addLane(f, s, b, (o + by) % u, l);
}

// The drums over the phrase: A' small edits, B a mini-fill (perc turned), the 8th bar of 128 a fill.
// Variety 0: the style's first kinds; above: drawn, more of them.
void phraseDrums(Ctx& c, Step* tr, const int8_t lane[kDiceLanes]) {
  static constexpr uint8_t kEdits[3][4] = {{2, 0, 3, 1}, {0, 2, 1, 3}, {1, 0, 3, 2}};
  static constexpr uint8_t kFills[3][5] = {{2, 3, 1, 0, 4}, {1, 2, 3, 0, 4}, {0, 4, 1, 3, 2}};
  const DiceFeel& f = *c.rec.feel;
  const int u = unitLen(c.len), str = c.drumVar, v = c.v;
  for (int j = 0; j < c.len / u; ++j) {
    const int b = j * u, role = c.bar[j];
    Step* s = tr + b;
    if (role == kBarA) continue;
    if (role == kBarBig) {
      writeFill(c, tr, lane, (c.pk.fill + 1) % listLen(diceFills()), b + u - 8, 8);
      continue;
    }
    const bool b2 = role == kBarB;
    const uint8_t* kinds = b2 ? kFills[str - 1] : kEdits[str - 1];
    const int nk = b2 ? 5 : 4;
    int count = b2 || str < 3 ? 1 : 2;
    if (v > 0 && c.pr.below(100) < static_cast<uint32_t>(v / 2)) ++count;
    if (str >= 2 && (b2 || (v > 0 && c.pr.below(100) < static_cast<uint32_t>(v / 3))))
      turnLane(f, s, u, b, lane[kDicePerc], 2);
    const int at = v > 0 ? static_cast<int>(c.pr.below(static_cast<uint32_t>(nk))) : 0;
    for (int t = 0; t < nk && count > 0; ++t) {
      const int kind = kinds[(at + t) % nk];
      if (b2 ? drumFill(f, s, u, b, lane, kind) : drumEdit(f, s, u, b, lane, kind)) --count;
    }
  }
}

// Writes the lanes' hits on drum track tr; mutations by Variety.
void writeDrums(Ctx& c, Step* tr, const int8_t lane[kDiceLanes]) {
  const int len = c.len;
  const DiceFeel& f = *c.rec.feel;
  uint8_t rat[kMaxSteps] = {};
  for (int l = 0; l < kDiceLanes; ++l) {
    const char* s = c.pk.drums[l];
    if (!s || lane[l] < 0) continue;
    const int n = static_cast<int>(strlen(s));
    const int rot = l == kDicePerc ? c.pk.percRot : 0;
    for (int i = 0; i < len; ++i) {
      const char ch = s[i % n];
      if (!isHit(ch)) continue;
      const int at = (i + rot) % len;
      drumHit(tr[at], lane[l], feelVel(f, ch, at));
      if (ch >= '2' && ch <= '4') rat[at] = static_cast<uint8_t>(ch - '0');
    }
  }
  if (c.v > 0) {
    const int v = c.v;
    const int hat = lane[kDiceHat], sn = lane[kDiceSnare], kick = lane[kDiceKick];
    for (int i = 0; i < len; ++i) {
      Step& st = tr[i];
      const uint8_t m = st.hasNote() ? st.vel : 0;
      if (hat >= 0 && !(m & (1u << hat)) && (i & 1) && c.mr.below(100) < static_cast<uint32_t>(v / 5))
        drumHit(st, hat, f.ghost);
      if (sn >= 0 && !m && c.mr.below(100) < static_cast<uint32_t>(v / 10)) drumHit(st, sn, f.ghost);
      if (hat >= 0 && st.hasNote() && st.vel == (1u << hat) && c.mr.below(100) < static_cast<uint32_t>(v / 4))
        putFx(st, Fx::PRB, static_cast<uint8_t>(50 + c.mr.below(41)));
      (void)kick;
      // Velocity spread grows with Variety: up to +-v/7 (+-14 at 100).
      if (st.hasNote() && st.note) {
        const int a = v / 7;
        const int x = st.note + static_cast<int>(c.mr.below(static_cast<uint32_t>(2 * a + 1))) - a;
        st.note = static_cast<uint8_t>(x < 1 ? 1 : x > 127 ? 127 : x);
      }
    }
  }
  for (int i = 0; i < len; ++i) {
    const uint8_t m = tr[i].vel;
    if (rat[i] && tr[i].hasNote() && m && !(m & (m - 1))) putFx(tr[i], Fx::RAT, rat[i]);
  }
  phraseDrums(c, tr, lane);
}

// ---- Melodic phrase --------------------------------------------------------------------------------
const ChordSeg* segAt(const ChordSeg* segs, int n, int i) {
  for (int k = 0; k < n; ++k)
    if (i >= segs[k].start && i < segs[k].start + segs[k].steps) return &segs[k];
  return nullptr;
}

// The note nearest x (up first on ties) of seg's chord.
int chordTone(int x, const ChordSeg& sg, uint8_t root, ScaleType sc) {
  uint8_t ns[4];
  const int n = chordNotes(sg.root, sg.chd, root, sc, ns);
  for (int a = 0; a <= 6; ++a)
    for (int y : {x + a, x - a})
      for (int i = 0; i < n; ++i)
        if ((ns[i] - y) % 12 == 0) return y;
  return x;
}

struct Tok {
  char kind;  // '.', '-', 'n'
  int8_t deg, oct;
};
bool sameTok(const Tok& a, const Tok& b) { return a.kind == b.kind && a.deg == b.deg && a.oct == b.oct; }

int parseMotif(const char* text, Tok* tok, int cap) {
  if (!text || diceMotifSteps(text) <= 0) return 0;
  int k = 0;
  for (const char* p = text; *p && k < cap;) {
    if (*p == ' ') {
      ++p;
      continue;
    }
    Tok& t = tok[k++];
    t.kind = *p == '.' || *p == '-' ? *p : 'n';
    t.deg = t.kind == 'n' ? static_cast<int8_t>(*p - '1') : 0;
    t.oct = 0;
    ++p;
    if (*p == '^') t.oct = 1, ++p;
    else if (*p == 'v') t.oct = -1, ++p;
  }
  return k;
}

// One unit of the lead phrase from the motif's tokens ut (in place): A' its tail turned (inverted round
// its first note, shifted or delayed), B an answer (head a third up, tail moved on, the last note the
// tonic / fifth: *cad its offset). Variety: notes added / dropped, leaps, steps.
void leadUnit(Ctx& c, Tok* ut, int u, int role, Rng& r, int* cad) {
  const int v = c.v, h = u / 2;
  Tok src[16];
  memcpy(src, ut, sizeof(Tok) * u);
  *cad = -1;
  if (role == kBarA2) {
    const int t0 = u - u / 4, mode = v > 0 ? static_cast<int>(r.below(3)) : 0;
    int pivot = -99;
    if (mode == 2) {
      for (int i = u - 1; i > t0; --i) ut[i] = ut[i - 1];
      ut[t0].kind = '.';
    } else {
      for (int i = t0; i < u; ++i) {
        if (ut[i].kind != 'n') continue;
        if (mode == 1) ut[i].deg = static_cast<int8_t>(ut[i].deg + 1 + static_cast<int>(r.below(2)));
        else if (pivot == -99) pivot = ut[i].deg;
        else ut[i].deg = static_cast<int8_t>(2 * pivot - ut[i].deg);
      }
    }
  } else if (role >= kBarB) {
    const int lift = v > 0 ? 1 + static_cast<int>(r.below(3)) : 2, by = v > 0 ? 1 + static_cast<int>(r.below(3)) : 2;
    for (int i = 0; i < h; ++i)
      if (ut[i].kind == 'n') ut[i].deg = static_cast<int8_t>(ut[i].deg + lift);
    for (int i = h; i < u; ++i) ut[i] = src[h + (i - h + h - by) % h];
    int last = -1, prev = 0;
    for (int i = 0; i < u; ++i)
      if (ut[i].kind == 'n') (i >= h ? last : prev) = i;
    if (last < 0) {
      last = h;
      ut[h].kind = 'n';
    }
    const int pd = ut[prev].kind == 'n' ? ut[prev].deg : 0;
    ut[last].deg = static_cast<int8_t>(v > 0 && r.below(3) == 0 ? 4 : pd >= 4 ? 7 : 0);
    ut[last].oct = 0;
    *cad = last;
  }
  if (v > 0) {
    int pd = 0;
    for (int i = 0; i < u; ++i) {
      Tok& t = ut[i];
      if (i == *cad) {
        pd = t.deg;
        continue;
      }
      if (t.kind == '.' && i && !(i & 1) && r.below(100) < static_cast<uint32_t>(v / 8)) {
        t.kind = 'n';
        t.deg = static_cast<int8_t>(pd + (r.below(2) ? 1 : -1));
        t.oct = 0;
      } else if (t.kind == 'n' && i && r.below(100) < static_cast<uint32_t>(v / 12)) {
        t.kind = '.';
      } else if (t.kind == 'n') {
        if (r.below(100) < static_cast<uint32_t>(v / 3)) t.deg = static_cast<int8_t>(t.deg + (r.below(2) ? 1 : -1));
        if (t.oct < 1 && r.below(100) < static_cast<uint32_t>(v / 12)) ++t.oct;
      }
      if (t.kind == 'n') pd = t.deg;
    }
  }
  bool same = true;
  for (int i = 0; i < u; ++i) same &= sameTok(ut[i], src[i]);
  if (same && role != kBarA)  // nothing moved (a tail of holds): the unit's last note a step up
    for (int i = u - 1; i >= 0; --i)
      if (ut[i].kind == 'n') {
        ut[i].deg = static_cast<int8_t>(ut[i].deg + 2);
        break;
      }
}

// LEAD motif over the phrase (c.bar, leadUnit); strong beats (but the cadence) take the chord's nearest
// note. simple (MAIN / FILL): A and A' only, velocity -20%. r: its own draws, so both takes share A.
void writeLead(Ctx& c, Step* tr, const char* text, const ChordSeg* segs, int segN, bool simple, Rng r) {
  Tok tok[32];
  const int k = parseMotif(text, tok, 32);
  if (k <= 0) return;
  const int u = unitLen(c.len);
  int last = -1, held = 0;
  for (int j = 0; j < c.len / u; ++j) {
    const int b = j * u;
    const int role = simple && c.bar[j] >= kBarB ? kBarA : c.bar[j];
    Tok ut[16];
    for (int i = 0; i < u; ++i) ut[i] = tok[(b + i) % k];
    int cad;
    leadUnit(c, ut, u, role, r, &cad);
    for (int i = 0; i < u; ++i) {
      const Tok& t = ut[i];
      if (t.kind == '.') {
        last = -1;
        continue;
      }
      if (t.kind == '-') {
        if (last >= 0) putFx(tr[last], Fx::GAT, gateValue(100 * (++held + 1) - 10));
        continue;
      }
      int note = moveDegrees(kLeadBase + c.root, t.deg, c.root, c.sc) + 12 * t.oct;
      const ChordSeg* sg = segAt(segs, segN, b + i);
      if (sg && !(i & 3) && i != cad) note = chordTone(note, *sg, c.root, c.sc);
      putNote(tr[b + i], note, simple ? 80 : 0, 0);
      last = b + i;
      held = 0;
    }
  }
}

// Note steps of unit [b, b + u) (offsets), returns their count.
int onsets(const Step* tr, int b, int u, uint8_t* at) {
  int n = 0;
  for (int o = 0; o < u; ++o)
    if (tr[b + o].hasNote()) at[n++] = static_cast<uint8_t>(o);
  return n;
}

// BASS over the phrase: A' a beat note pushed early or skipped; A' and B an approach in the last beat to
// the next bar's root (a scale step in A', chromatic in B); B now and then an octave leap.
void phraseBass(Ctx& c, Step* tr, const ChordSeg* segs, int segN) {
  const int len = c.len, u = unitLen(len), h = u / 2;
  for (int j = 0; j < len / u; ++j) {
    const int role = c.bar[j], b = j * u;
    if (role == kBarA) continue;
    Step* s = tr + b;
    if (role == kBarA2) {
      if (!(c.v > 0 && c.pr.below(2)) && s[h].hasNote() && !s[h - 1].hasNote()) {
        s[h - 1] = s[h];
        s[h] = Step();
      } else {
        for (int o : {u / 4, 3 * u / 4})
          if (s[o].hasNote()) {
            s[o] = Step();
            break;
          }
      }
    }
    uint8_t at[16];
    const int n = onsets(tr, b, u, at);
    int pos = n && at[n - 1] >= u - 4 ? at[n - 1] : u - 2;
    const int ref = s[pos].hasNote() ? s[pos].note : n ? s[at[n - 1]].note : 36 + c.root;
    const int next = (b + u) % len;
    int t;
    if (tr[next].hasNote()) {
      t = tr[next].note;
    } else {
      const ChordSeg* sg = segAt(segs, segN, next);
      if (!sg) continue;
      const int d = ((sg->root - ref) % 12 + 12) % 12;
      t = d <= 6 ? ref + d : ref + d - 12;
    }
    const bool chroma = role != kBarA2 || (c.v > 0 && c.pr.below(3) == 0);
    const int dir = ref <= t ? -1 : 1;
    const int a = chroma ? t + dir : moveDegrees(t, dir, c.root, c.sc);
    if (s[pos].hasNote()) s[pos].note = static_cast<uint8_t>(a < 0 ? 0 : a > 127 ? 127 : a);
    else putNote(s[pos], a, 0, 50);
    if (role != kBarA2 && c.v > 0 && c.pr.below(100) < static_cast<uint32_t>(c.v / 4))
      for (int o = u / 4; o <= h; ++o)
        if (s[o].hasNote() && s[o].note <= 60) {
          s[o].note = static_cast<uint8_t>(s[o].note + 12);
          break;
        }
  }
}

// ARP (and an arp LEAD) over the phrase: B an octave over (under, when high), or (Variety) reversed.
void phraseArp(Ctx& c, Step* tr) {
  const int u = unitLen(c.len);
  for (int j = 0; j < c.len / u; ++j) {
    if (c.bar[j] < kBarB) continue;
    const int b = j * u;
    uint8_t at[16];
    const int n = onsets(tr, b, u, at);
    bool moved = false;
    if (c.v > 0 && c.pr.below(2))
      for (int i = 0; i < n / 2; ++i) {
        Step &x = tr[b + at[i]], &y = tr[b + at[n - 1 - i]];
        moved |= x.note != y.note;
        const uint8_t t = x.note;
        x.note = y.note;
        y.note = t;
      }
    if (moved) continue;
    bool high = false;
    for (int i = 0; i < n; ++i) high |= tr[b + at[i]].note > 96;
    for (int i = 0; i < n; ++i) tr[b + at[i]].note = static_cast<uint8_t>(tr[b + at[i]].note + (high ? -12 : 12));
  }
}

// The progression over the pattern: repeated while a chord would last over 2 bars (a turnaround on the
// last pass by Variety); the style's added notes on its diatonic chords (Variety 0: on the last one).
void phraseProg(Ctx& c, Progression& p) {
  if (p.steps || !p.n) return;
  int sum = 0;
  for (int i = 0; i < p.n; ++i) sum += p.c[i].len;
  int reps = 1;
  while (sum > 0 && p.n * reps * 2 <= kProgMax && c.len / (sum * reps) > 32) reps *= 2;
  const int n0 = p.n;
  for (int r = 1; r < reps; ++r) memcpy(&p.c[r * n0], &p.c[0], sizeof(ProgChord) * n0);
  p.n = static_cast<uint8_t>(n0 * reps);
  if (reps > 1 && c.v > 0 && c.pr.below(100) < static_cast<uint32_t>(c.v)) {
    ProgChord& e = p.c[p.n - 1];
    const int d = e.roman ? -1 : e.degree;
    const uint8_t l = e.len;
    e = ProgChord();
    e.degree = static_cast<int8_t>(d == 4 ? 3 : 4);
    e.len = l;
  }
  if (!c.addNotes) return;
  const uint8_t type = c.addNotes == 2 ? kChordSus4 : c.v > 0 && c.pr.below(2) ? kChordNinth : kChordSeventh;
  for (int i = 0; i < p.n; ++i) {
    ProgChord& e = p.c[i];
    if (e.type >= 0 || e.roman) continue;  // named / Roman chords keep theirs
    if (c.v > 0 ? c.pr.below(100) < static_cast<uint32_t>(c.v / 2) : i == p.n - 1) e.type = static_cast<int8_t>(type);
  }
}

// CHORDS: B's last '&' takes the next bar's chord (a push).
void phraseChords(Ctx& c, Step* tr) {
  const int u = unitLen(c.len);
  for (int j = 0; j < c.len / u; ++j) {
    if (c.bar[j] < kBarB) continue;
    const int p = j * u + u - 2, next = (j * u + u) % c.len;
    if (tr[next].hasNote()) tr[p] = tr[next];
  }
}

// FX percussion hits on track tr.
void writeFxHits(Ctx& c, Step* tr) {
  const char* s = c.pk.fxHits;
  if (!s) return;
  const int n = static_cast<int>(strlen(s));
  for (int i = 0; i < c.len; ++i) {
    const char ch = s[i % n];
    if (!isHit(ch)) continue;
    putNote(tr[i], kFxNote, hitVel(ch), 0);
    if (ch >= '2' && ch <= '4') putFx(tr[i], Fx::RAT, static_cast<uint8_t>(ch - '0'));
  }
}

Sel trackSel(int t, int len) {
  return Sel{static_cast<uint8_t>(t), static_cast<uint8_t>(t), 0, static_cast<uint8_t>(len - 1)};
}

// MAIN with every role into w.full (DROP's lead included; MAIN's simpler lead on track 7). on: per role.
void generate(Ctx& c, DiceWork& w, const Instrument* instruments, const TrackCfg* tracks, const bool* on) {
  Pattern& pt = w.full;
  const int len = c.len;
  for (auto& tr : pt.steps) clearSteps(tr);
  const Picks& k = c.pk;
  // DRUMS
  if (on[0]) {
    const Instrument& kit = instruments[tracks[0].instr % kInstruments];
    int8_t lane[kDiceLanes];
    kitLanes(kit, instruments, lane);
    writeDrums(c, pt.steps[0], lane);
  }
  // CHORDS (also the source of BASS / ARP / LEAD)
  ChordSpec cs;
  cs.random = false;
  cs.octave = c.rec.chordOct;
  cs.flavor = c.rec.flavor;
  cs.voicing = ChordVoicing::Close;
  cs.length = 0;
  cs.dest = 2;
  cs.rhythm = static_cast<uint8_t>(k.rhythm);
  cs.seed = k.seed;
  w.prog = Progression();
  bool chords = false;
  if (k.prog >= 0 && parseProgression(progFactoryText(k.prog), w.prog)) {
    phraseProg(c, w.prog);
    parseArpPattern(chordRhythmText(cs.rhythm), w.rhythm);
    chords = applyChords(pt, trackSel(2, len), cs, w.prog, w.rhythm, c.root, c.sc, nullptr);
  }
  const int segN = layoutChords(w.prog, cs, len, c.root, c.sc, w.segs);
  // BASS
  if (on[1] && k.bass >= 0) {
    BassSpec bs;
    bs.source = BassSource::Progression;
    bs.octave = 2;
    bs.dest = 1;
    bs.pattern = static_cast<uint8_t>(k.bass);
    bs.seed = k.seed;
    bs.mutate = static_cast<uint8_t>(c.v / 4);
    parseBassPattern(bassFactoryText(k.bass), w.bass);
    applyBass(pt, pt, trackSel(1, len), bs, w.bass, c.root, c.sc, w.segs, segN, nullptr);
    phraseBass(c, pt.steps[1], w.segs, segN);
  }
  auto arp = [&](int dest, int pattern, uint8_t mode, uint8_t octaves, uint32_t seed) {
    if (pattern < 0) return;
    ArpSpec as;
    as.source = chords ? ArpSource::Selection : ArpSource::Chord;
    as.root = static_cast<uint8_t>(60 + c.root);
    as.chord = kChordTriad;
    as.dest = static_cast<uint8_t>(dest);
    as.mode = static_cast<ArpMode>(mode < static_cast<uint8_t>(ArpMode::Count) ? mode : 0);
    as.octaves = octaves;
    as.pattern = static_cast<uint8_t>(pattern);
    as.seed = seed;
    as.mutate = static_cast<uint8_t>(c.v / 3);
    parseArpPattern(arpFactoryText(pattern), w.arp);
    // SELECTION reads the CHORDS track (sel.t0..t1) and writes only Dest.
    applyArp(pt, pt, trackSel(chords ? 2 : dest, len), as, w.arp, c.root, c.sc, nullptr);
  };
  if (on[4]) {
    arp(4, k.arp, k.arpMode, k.arpOct, k.seed + 1);
    phraseArp(c, pt.steps[4]);
  }
  if (on[3]) {
    Step* simple = pt.steps[kLeadMain];
    if (k.lead == DiceLead::Arp) {
      arp(3, k.leadArp, k.leadArpMode, 1, k.seed + 2);
      for (int i = 0; i < len; ++i) {
        Step& st = pt.steps[3][i];
        if (st.hasNote() && st.note <= 115) st.note = static_cast<uint8_t>(st.note + 12);
        simple[i] = st;
        if (st.hasNote()) simple[i].vel = static_cast<uint8_t>(effVel(st.vel) * 4 / 5);
      }
      phraseArp(c, pt.steps[3]);
    } else if (k.lead == DiceLead::Motif) {
      const Rng r(c.pr.next());
      writeLead(c, pt.steps[3], diceMotifText(k.motif), w.segs, segN, false, r);
      writeLead(c, simple, diceMotifText(k.motif), w.segs, segN, true, r);
    }
  }
  if (chords) phraseChords(c, pt.steps[2]);
  if (on[5] && !k.riser) writeFxHits(c, pt.steps[5]);
}

void removeLane(Step* tr, int len, int lane) {
  if (lane < 0) return;
  for (int i = 0; i < len; ++i) {
    Step& st = tr[i];
    if (!st.hasNote()) continue;
    st.vel = static_cast<uint8_t>(st.vel & ~(1u << lane));
    if (!st.vel) st = Step();
  }
}

// Everything a roll may write, bit for bit (memcpy: padding too, so an undo compares equal).
void load(const Project& p, int first, DiceParts& d) {
  d.bpm = p.bpm;
  d.scaleRoot = p.scaleRoot;
  d.scaleType = p.scaleType;
  memcpy(d.tracks, p.tracks, sizeof(d.tracks));
  memcpy(d.instruments, p.instruments, sizeof(d.instruments));
  memcpy(d.gen, p.gen, sizeof(d.gen));
  memcpy(d.var, p.var, sizeof(d.var));
  for (int i = 0; i < kDicePats; ++i) {
    const Pattern& pt = p.patterns[first + i];
    DicePat& dp = d.pats[i];
    dp.length = pt.length;
    dp.res = pt.res;
    dp.swing = pt.swing;
    dp.groove = pt.groove;
    memcpy(dp.trackLen, pt.trackLen, sizeof(dp.trackLen));
    memcpy(dp.trackSpeed, pt.trackSpeed, sizeof(dp.trackSpeed));
    memcpy(dp.steps, pt.steps, sizeof(dp.steps));
  }
  DiceSong& s = d.song;
  memcpy(s.song, p.song, sizeof(s.song));
  memcpy(s.songMute, p.songMute, sizeof(s.songMute));
  s.songLen = p.songLen;
  memcpy(s.songFol, p.songFol, sizeof(s.songFol));
  memcpy(s.songTrn, p.songTrn, sizeof(s.songTrn));
  memcpy(s.songBpm, p.songBpm, sizeof(s.songBpm));
}

uint8_t capU7(uint8_t v, uint8_t cap) { return cap != kDiceKeep && v > cap ? cap : v; }
uint8_t addU7(uint8_t v, int d) {
  const int x = v + d;
  return static_cast<uint8_t>(x < 0 ? 0 : x > 127 ? 127 : x);
}

// The style's kit into instruments[slot], its drums into the block from factoryKitBlock (DiceLane l on kit
// lane kDiceKitLane[l], transposed down by the lane as factoryBuildKit does); lanes 7, 8 empty. Writes the
// slots it fills into mask; false: no kit.
bool buildKit(const Ctx& c, Instrument* ins, int slot, uint32_t& mask) {
  const DiceKit* dk = c.rec.kit;
  if (!dk) return false;
  Instrument& kit = ins[slot];
  kit = Instrument();
  kit.type = InstrType::Kit;
  snprintf(kit.name, sizeof(kit.name), "%.8s", c.rec.style);
  kitSetDefaults(kit);
  const DiceTone& t = c.rec.mix->role[0];
  const int first = factoryKitBlock(slot);
  for (int l = 0; l < kDiceLanes; ++l) {
    const int item = c.pk.drum[l];
    if (item < 0) continue;
    const DiceDrum& dr = dk->lane[l][item];
    const int x = presetIndex(dr.type, dr.name);
    if (x < 0) continue;
    const int kl = kDiceKitLane[l];
    Instrument& m = ins[first + kl];
    factoryBuild(dr.type, x, m);
    clearInsert(m);  // behind a lane: no track insert takes the drum's recommendation
    m.transpose = static_cast<int8_t>(m.transpose - kl);
    m.vol = dr.vol;
    if (m.type == InstrType::Drum || m.type == InstrType::Fm) {
      m.macro[kMacDec] = addU7(m.macro[kMacDec], dr.dec + c.pk.jd[l]);
      m.macro[kMacCol] = addU7(m.macro[kMacCol], dr.tone + c.pk.jt[l]);
    }
    const bool kick = l == kDiceKick;
    m.rsend = kick ? 0 : t.rvb != kDiceKeep ? t.rvb : m.rsend;
    m.send = l == kDicePerc && t.dly != kDiceKeep ? t.dly : kick ? 0 : m.send;
    kit.kit[kl].instr = static_cast<uint8_t>(first + kl);
    kit.kit[kl].vol = dr.vol;
    mask |= 1u << (first + kl);
  }
  return true;
}

// A melodic role's tone on top of its preset (see DiceTone).
void applyTone(Instrument& m, TrackCfg& cfg, const DiceTone& t) {
  if (t.cut != kDiceKeep) {
    const FltMode f = static_cast<FltMode>(m.fltMode);
    if (f == FltMode::Off) {
      m.fltMode = static_cast<uint8_t>(FltMode::Lp);
      m.cutoff = t.cut;
      m.reso = 0;
    } else if (f == FltMode::Lp || f == FltMode::Ladder || f == FltMode::Ms20 || f == FltMode::Sid) {
      m.cutoff = capU7(m.cutoff, t.cut);
    }
  }
  m.reso = capU7(m.reso, t.res);
  if (t.fenv != kDiceKeep && m.fenv > static_cast<int>(t.fenv)) m.fenv = static_cast<int8_t>(t.fenv > 63 ? 63 : t.fenv);
  if (m.attack < t.atk) m.attack = t.atk;
  if (m.release < t.rel) m.release = t.rel;
  cfg.drive = capU7(cfg.drive, t.drv);
  m.synFold = capU7(m.synFold, t.drv);
  m.synRing = capU7(m.synRing, t.drv);
  m.fmSat = capU7(m.fmSat, t.drv);
  cfg.crushBits = capU7(cfg.crushBits, t.crush);
  cfg.crushRate = capU7(cfg.crushRate, t.crush);
  if (t.dly != kDiceKeep) m.send = t.dly;
  if (t.rvb != kDiceKeep) m.rsend = t.rvb;
}

void rollSounds(Ctx& c, DiceParts& d, const Project& p, DiceSnap& s, const bool* present) {
  uint8_t slot[kDiceRoles];
  slotsOf(d.tracks, p.tracks, d.instruments, slot);
  for (int k = 0; k < kDiceRoles; ++k) {
    if (!present[k] || slot[k] == kNoInstr) continue;
    const DiceRole role = static_cast<DiceRole>(k);
    if (role == DiceRole::Drums) {
      if (!buildKit(c, d.instruments, slot[k], s.instrMask)) continue;
    } else {
      if (c.pk.sound[k] < 0) continue;
      const InstrType t = c.rec.sounds[k][c.pk.soundItem[k]].type;
      factoryBuild(t, c.pk.sound[k], s.work.tmp);
      if (!roleFits(role, s.work.tmp)) continue;
      memcpy(&d.instruments[slot[k]], &s.work.tmp, sizeof(Instrument));
    }
    s.instrMask |= 1u << slot[k];
    TrackCfg& cfg = d.tracks[k];
    cfg.out = TrackOut::Int;
    cfg.instr = slot[k];
    snprintf(cfg.name, sizeof(cfg.name), "%s", kRoleNames[k]);
    takePresetInsert(d.instruments[slot[k]], cfg);  // as the preset browser: the recommended insert
    const DiceTone& t = c.rec.mix->role[k];
    if (role == DiceRole::Drums) {
      cfg.drive = capU7(cfg.drive, t.drv);
      cfg.crushBits = capU7(cfg.crushBits, t.crush);
      cfg.crushRate = capU7(cfg.crushRate, t.crush);
      cfg.humanize = static_cast<uint8_t>(c.rec.feel->human * c.v / 100);
    } else {
      applyTone(d.instruments[slot[k]], cfg, t);
    }
    cfg.vol = t.vol;
    cfg.pan = t.pan;
    s.cfgMask = static_cast<uint8_t>(s.cfgMask | (1u << k));
  }
}

void rollPatterns(Ctx& c, DiceParts& d, DiceSnap& s, const bool* present) {
  DiceWork& w = s.work;
  bool writable[kDiceRoles], on[kDiceRoles];
  for (int k = 0; k < kDiceRoles; ++k) {
    const bool kit = d.instruments[d.tracks[k].instr % kInstruments].type == InstrType::Kit;
    writable[k] = k == 0 ? kit : !kit;
    on[k] = present[k] && writable[k];
  }
  w.full.length = static_cast<uint8_t>(c.len);
  phraseBars(c);
  generate(c, w, d.instruments, d.tracks, on);
  int8_t lane[kDiceLanes];
  kitLanes(d.instruments[d.tracks[0].instr % kInstruments], d.instruments, lane);
  const bool leadOn = present[3] && c.pk.lead != DiceLead::None;
  const int fxNote = kFxNote + c.root;
  for (int i = 0; i < kDicePats; ++i) {
    DicePat& dp = d.pats[i];
    dp.length = static_cast<uint8_t>(c.len);
    dp.res = c.rec.res;
    dp.swing = c.rec.swing;
    dp.groove = static_cast<uint8_t>(grooveIndex(c.rec.groove));
    for (int k = 0; k < kDiceRoles; ++k) {
      dp.trackLen[k] = 0;
      dp.trackSpeed[k] = 0;
      clearSteps(dp.steps[k]);
      if (on[k]) memcpy(dp.steps[k], w.full.steps[k], sizeof(Step) * c.len);
    }
    const int len = c.len;
    switch (i) {
      case kDiceMain:
      case kDiceFill:
        clearSteps(dp.steps[3]);
        if (on[3]) memcpy(dp.steps[3], w.full.steps[kLeadMain], sizeof(Step) * len);
        if (i == kDiceMain) break;
        if (on[0]) writeFill(c, dp.steps[0], lane, c.pk.fill, len - (len <= 16 ? 4 : 8), len <= 16 ? 4 : 8);
        if (on[5] && c.pk.riser) putNote(dp.steps[5][len - 8], fxNote, 0, 790);
        break;
      case kDiceBreak:
        if (on[0]) removeLane(dp.steps[0], len, lane[kDiceKick]);
        clearSteps(dp.steps[1]);
        clearSteps(dp.steps[3]);
        clearSteps(dp.steps[4]);
        if (on[5] && c.pk.riser) {
          putNote(dp.steps[5][0], fxNote, 0, 790);
          putNote(dp.steps[5][len - 8], fxNote, 0, 790);
        }
        break;
      case kDiceDrop:
        if (on[5] && c.pk.riser) putNote(dp.steps[5][0], fxNote, 127, 100);
        break;
    }
  }
  for (int k = 0; k < kDiceRoles; ++k) {
    const bool absent = !present[k] || (k == 3 && !leadOn);
    if (!absent && !writable[k]) continue;  // a role this track cannot play: left alone
    s.patMask = static_cast<uint8_t>(s.patMask | (1u << k));
    s.muteMask = static_cast<uint8_t>(s.muteMask | (1u << k));
    d.tracks[k].mute = absent;
    d.gen[k].mode = static_cast<uint8_t>(GenMode::Off);
    d.var[k].mode = static_cast<uint8_t>(VarMode::Off);
  }
}

void rollSong(DiceParts& d, int first) {
  static constexpr uint8_t kOrder[kDiceSongLen] = {kDiceMain, kDiceMain, kDiceFill, kDiceBreak,
                                                   kDiceDrop, kDiceDrop, kDiceFill};
  DiceSong& s = d.song;
  for (int i = 0; i < kDiceSongLen; ++i) {
    s.song[i] = static_cast<uint8_t>(first + kOrder[i]);
    s.songMute[i] = 0;
    s.songFol[i] = SongFollow();
    s.songTrn[i] = 0;
    s.songBpm[i] = 0;
  }
  s.songLen = kDiceSongLen;
}

// Byte swap without a temporary (a track's steps are kilobytes; the UI task's stack is small).
void swapBytes(void* a, void* b, size_t n) {
  uint8_t* x = static_cast<uint8_t*>(a);
  uint8_t* y = static_cast<uint8_t*>(b);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t t = x[i];
    x[i] = y[i];
    y[i] = t;
  }
}
#define SWAP(a, b) swapBytes(&(a), &(b), sizeof(a))

// Swaps the parts the roll wrote (s.layers, the masks) between p and s.parts. bpm: the project BPM (p.bpm
// may be a song slot's live tempo); it goes into s.parts, not p.bpm.
void swapWritten(Project& p, DiceSnap& s, uint16_t bpm) {
  DiceParts& d = s.parts;
  if (s.layers & kDiceTempo) {
    p.bpm = d.bpm;
    d.bpm = bpm;
    SWAP(p.scaleRoot, d.scaleRoot);
    SWAP(p.scaleType, d.scaleType);
  }
  for (int k = 0; k < kDiceRoles; ++k) {
    const int t = kDiceTrack[k];
    if (s.cfgMask & (1u << k)) {
      SWAP(p.tracks[t], d.tracks[k]);
      // Mute / solo stay live (the sequencer writes them too: song mutes, rewind): swapped back.
      SWAP(p.tracks[t].mute, d.tracks[k].mute);
      SWAP(p.tracks[t].solo, d.tracks[k].solo);
    }
    if (s.muteMask & (1u << k)) SWAP(p.tracks[t].mute, d.tracks[k].mute);
    if (s.patMask & (1u << k)) {
      for (int i = 0; i < kDicePats; ++i) {
        Pattern& pt = p.patterns[s.first + i];
        DicePat& dp = d.pats[i];
        SWAP(pt.steps[t], dp.steps[k]);
        SWAP(pt.trackLen[t], dp.trackLen[k]);
        SWAP(pt.trackSpeed[t], dp.trackSpeed[k]);
      }
      SWAP(p.gen[t], d.gen[k]);
      SWAP(p.var[t], d.var[k]);
    }
  }
  if (s.layers & kDicePattern)
    for (int i = 0; i < kDicePats; ++i) {
      Pattern& pt = p.patterns[s.first + i];
      DicePat& dp = d.pats[i];
      SWAP(pt.length, dp.length);
      SWAP(pt.res, dp.res);
      SWAP(pt.swing, dp.swing);
      SWAP(pt.groove, dp.groove);
    }
  for (int i = 0; i < kInstruments; ++i)
    if (s.instrMask & (1u << i)) SWAP(p.instruments[i], d.instruments[i]);
  if (s.layers & kDiceSong) {
    DiceSong& g = d.song;
    SWAP(p.song, g.song);
    SWAP(p.songMute, g.songMute);
    SWAP(p.songLen, g.songLen);
    SWAP(p.songFol, g.songFol);
    SWAP(p.songTrn, g.songTrn);
    SWAP(p.songBpm, g.songBpm);
  }
}

}  // namespace

const char* diceRoleName(DiceRole r) {
  const int i = static_cast<int>(r);
  return kRoleNames[i >= 0 && i < kDiceRoles ? i : 0];
}

int diceFirstPattern(int editPattern) {
  if (editPattern < 0) editPattern = 0;
  return editPattern > kPatterns - kDicePats ? kPatterns - kDicePats : editPattern;
}

uint16_t diceOverwrites(const Project& p, int editPattern) {
  const int first = diceFirstPattern(editPattern);
  uint16_t m = 0;
  for (int i = 0; i < kDicePats; ++i)
    if (!p.patterns[first + i].isEmpty()) m = static_cast<uint16_t>(m | (1u << i));
  return m;
}

void diceSlots(const Project& p, uint8_t out[kDiceRoles]) { slotsOf(p.tracks, p.tracks, p.instruments, out); }

DiceResult diceDraft(const Project& p, const DiceOpts& o, int editPattern, uint32_t seed, DiceSnap& s, uint16_t bpm) {
  DiceResult res;
  int g = o.genre, st = o.style;
  if (!diceRecipe(g, st)) g = 0, st = 0;
  const DiceRecipe& rec = *diceRecipe(g, st);
  uint8_t layers = o.layers & (kDiceAll | kDiceSong);
  if (!(layers & kDicePattern)) layers &= static_cast<uint8_t>(~kDiceSong);
  const int first = diceFirstPattern(editPattern);
  s.layers = 0;  // parts are about to be overwritten: the previous undo is gone
  s.drafted = layers;
  s.first = static_cast<uint8_t>(first);
  s.cfgMask = s.muteMask = s.patMask = 0;
  s.instrMask = 0;
  DiceParts& d = s.parts;
  load(p, first, d);
  if (bpm) d.bpm = bpm;
  res.genre = static_cast<int8_t>(g);
  res.style = static_cast<int8_t>(st);
  res.first = static_cast<uint8_t>(first);

  const int v = o.variety > 100 ? 100 : o.variety;
  Rng r(seed);
  // TEMPO
  uint16_t tempo = rec.bpm;
  if (v > 0) {
    const int spread = (rec.bpmHi - rec.bpmLo) * v / 100;
    int b = rec.bpm + static_cast<int>(r.below(static_cast<uint32_t>(2 * spread + 1))) - spread;
    b = b < rec.bpmLo ? rec.bpmLo : b > rec.bpmHi ? rec.bpmHi : b;
    tempo = static_cast<uint16_t>(b);
  }
  const int si = pick(rec.scales, v, r);
  const uint8_t scale = si >= 0 ? rec.scales[si].v : static_cast<uint8_t>(ScaleType::Minor);
  const uint8_t root = o.root >= 0 && o.root < 12 ? static_cast<uint8_t>(o.root) : static_cast<uint8_t>(r.below(12));
  if (layers & kDiceTempo) {
    d.bpm = tempo;
    d.scaleRoot = root;
    d.scaleType = scale;
  }
  // The generators work in the project's key (the rolled one with TEMPO); a scale without 7 notes takes
  // the style's.
  ScaleType sc = d.scaleType < static_cast<uint8_t>(ScaleType::Count) ? static_cast<ScaleType>(d.scaleType)
                                                                       : ScaleType::Minor;
  if (scaleDegrees(sc) != 7) sc = static_cast<ScaleType>(scale);
  const int len = o.len == 16 || o.len == 32 || o.len == 64 || o.len == 128 ? o.len : rec.len;
  const uint8_t tr = g < 9 && st < 6 ? kTraits[g][st] : 0;
  Ctx c{rec,
        v,
        Picks(),
        static_cast<uint8_t>(d.scaleRoot % 12),
        sc,
        len,
        Rng(seed ^ 0x9E3779B9u),
        Rng(v > 0 ? seed * 2654435761u + 7u : kCanonSeed),
        static_cast<uint8_t>(tr & 15 ? tr & 15 : 2),
        static_cast<uint8_t>(tr >> 4),
        {}};
  rollPicks(c, r);
  bool present[kDiceRoles];
  for (int k = 0; k < kDiceRoles; ++k) present[k] = (rec.roles >> k) & 1;
  if (layers & kDiceSounds) rollSounds(c, d, p, s, present);
  if (layers & kDicePattern) rollPatterns(c, d, s, present);
  if (layers & kDiceSong) rollSong(d, first);
  res.written = static_cast<uint16_t>(s.cfgMask | s.patMask | s.muteMask);
  return res;
}

uint8_t diceApply(Project& p, DiceSnap& s, uint16_t bpm) {
  const uint8_t l = s.drafted;
  if (!l) return 0;  // nothing drafted: the applied roll's undo stays
  s.drafted = 0;
  s.layers = l;
  swapWritten(p, s, bpm ? bpm : p.bpm);
  return l;
}

DiceResult diceRoll(Project& p, const DiceOpts& o, int editPattern, uint32_t seed, DiceSnap& s, uint16_t bpm) {
  const DiceResult res = diceDraft(p, o, editPattern, seed, s, bpm);
  diceApply(p, s, bpm);
  return res;
}

uint8_t diceUndo(Project& p, DiceSnap& s, uint16_t bpm) {
  const uint8_t l = s.layers;
  if (!l) return 0;
  swapWritten(p, s, bpm ? bpm : p.bpm);
  s.layers = 0;
  s.cfgMask = s.muteMask = s.patMask = 0;
  s.instrMask = 0;
  return l;
}

}  // namespace mt
