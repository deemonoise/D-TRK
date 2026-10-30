#include "dice_library.h"
#include <algorithm>
#include <limits.h>
#include <string.h>
#include "midi_import.h"
#include "rng.h"

namespace mt {
namespace {
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return u16(p) | (static_cast<uint32_t>(u16(p + 2)) << 16); }
constexpr uint32_t kHead = 16, kEntry = 64, kEvent = 8;
bool entryValid(const uint8_t* e, const uint8_t* data, size_t size, uint32_t tableEnd) {
  const uint32_t at = u32(e + 8), bytes = u32(e + 12);
  if (!u16(e) || (u16(e) & ~0x1FF) || e[2] >= kDiceRoles || e[3] > 11 ||
      e[4] >= static_cast<int>(ScaleType::Count) || e[5] < 1 || e[5] > 8 ||
      !memchr(e + 16, 0, 48) || at < tableEnd || at > size ||
      !bytes || bytes % kEvent || bytes > kDiceMidiEvents * kEvent || bytes > size - at) return false;
  uint16_t prev = 0;
  for (uint32_t i = 0; i < bytes; i += kEvent) {
    const uint8_t* n = data + at + i;
    if (u16(n) < prev || u16(n) >= e[5] * 384 || !u16(n + 2) || n[4] > 127 ||
        !n[5] || n[5] > 127 || (n[6] != 255 && n[6] >= kKitLanes)) return false;
    prev = u16(n);
  }
  return true;
}
int floorDiv(int x, int d) { return x >= 0 ? x / d : (x - d + 1) / d; }
int scaleNote(int note, const DicePhrase& ph, int root, ScaleType sc) {
  const ScaleType source = static_cast<ScaleType>(ph.scale);
  if (source == ScaleType::Chromatic || sc == ScaleType::Chromatic ||
      scaleDegrees(source) != scaleDegrees(sc)) return std::max(0, std::min(127, note + root - ph.root));
  const int rel = note - ph.root, oct = floorDiv(rel, 12), pc = rel - oct * 12;
  const uint16_t from = scaleMask(source), to = scaleMask(sc);
  int degree = 0, fromPc = 0;
  for (int i = 0; i <= pc; ++i) if ((from >> i) & 1) { fromPc = i; ++degree; }
  int targetPc = 0, found = 0;
  for (int i = 0; i < 12; ++i) if ((to >> i) & 1) { targetPc = i; if (++found == degree) break; }
  return std::max(0, std::min(127, oct * 12 + root + targetPc + pc - fromPc));
}
void put(Step& s, Fx fx, uint8_t value) {
  for (auto& slot : s.fx) if (slot.cmd == Fx::None || slot.cmd == fx) { slot = {fx, value}; return; }
}
bool fitChord(const uint8_t* notes, int n, int root, ScaleType scale, Step& s) {
  int best = INT_MAX, bestRoot = notes[0], bestChord = kChordMaj;
  for (int base = std::max(0, static_cast<int>(notes[0]) - 24); base <= notes[0]; ++base)
    for (int chord = 0; chord < kChordValues; ++chord) {
      uint8_t ns[4]; const int count = chordNotes(base, chord, root, scale, ns);
      if (!count) continue;
      int matches = 0;
      for (int a = 0; a < n; ++a) for (int b = 0; b < count; ++b) if (notes[a] == ns[b]) ++matches;
      const int score = (n - matches) * 24 + (count - matches) * 16 + abs(static_cast<int>(ns[0]) - notes[0]);
      if (score < best) { best = score; bestRoot = base; bestChord = chord; }
    }
  s.note = static_cast<uint8_t>(bestRoot);
  put(s, Fx::CHD, static_cast<uint8_t>(bestChord));
  return best != 0;
}
}  // namespace

int diceCandidateSlot(uint32_t seen, uint32_t random) {
  if (seen <= kDicePhraseCandidates) return static_cast<int>(seen) - 1;
  return random < kDicePhraseCandidates ? static_cast<int>(random) : -1;
}
void DicePhraseFeatures::add(const DicePhraseEvent& event) {
  if (event.tick != previous) {
    ++attacks;
    // Near-beat attacks count as accents too, preserving humanized source MIDI.
    const int beat = event.tick % 96;
    if (beat <= 6 || beat >= 90) ++strong;
    previous = event.tick;
  }
  low = std::min(low, event.note); high = std::max(high, event.note);
}
int DicePhraseFeatures::score(DiceRole role, uint8_t bars, uint32_t roleSeed) const {
  const int k = static_cast<int>(role);
  if (!bars || !attacks || k < 0 || k >= kDiceRoles) return INT_MAX;
  // Recover the roll seed so every role shares an activity and phrase-length target.
  Rng profile(roleSeed ^ (0x9E3779B9u * (k + 1)));
  const int activity = 70 + profile.below(61);
  const int wantedBars = 1 << profile.below(3);
  static constexpr int density[kDiceRoles] = {12, 6, 2, 5, 10, 1};
  // Busy rhythm sections leave more space for chords and the foreground melody.
  const int wanted = density[k] * ((k == 2 || k == 3) ? 200 - activity : activity);
  int score = abs(static_cast<int>(attacks) * 100 / bars - wanted) / 5;
  score += abs(static_cast<int>(bars) - wantedBars) * 5;
  const int wantedStrong = (k == 1 || k == 2) ? 65 : 35;
  score += abs(static_cast<int>(strong) * 100 / attacks - wantedStrong) / 3;
  if (k != 0) {
    static constexpr int bottom[kDiceRoles] = {0, 24, 42, 48, 48, 24};
    static constexpr int top[kDiceRoles] = {127, 60, 84, 96, 96, 108};
    score += std::max(0, bottom[k] - low) * 20 + std::max(0, high - top[k]) * 20;
    score += std::max(0, static_cast<int>(high) - low - (k == 1 ? 24 : 36)) * 3;
  }
  return score;
}

DiceMidiLibrary::DiceMidiLibrary(const uint8_t* data, size_t size) : data_(data), size_(size) {
  if (!data || size < kHead || size > kDiceLibraryMaxBytes || memcmp(data, "DML1", 4) ||
      u16(data + 4) != 1 || u16(data + 6) != kEntry) return;
  const uint32_t count = u32(data + 8);
  if (!count || count > 16000 || count > (size - kHead) / kEntry) return;
  const uint32_t end = kHead + count * kEntry;
  for (uint32_t i = 0; i < count; ++i) if (!entryValid(data + kHead + i * kEntry, data, size, end)) return;
  count_ = count;
}

bool DiceMidiLibrary::pick(int genre, DiceRole role, uint32_t seed, DicePhrase& out) const {
  if (!valid() || genre < 0 || genre >= 9) return false;
  if (static_cast<int>(role) < 0 || static_cast<int>(role) >= kDiceRoles) return false;
  Rng rng(seed); const uint8_t* candidates[kDicePhraseCandidates] = {}; uint32_t found = 0;
  for (uint32_t i = 0; i < count_; ++i) {
    const uint8_t* e = data_ + kHead + i * kEntry;
    if (e[2] != static_cast<uint8_t>(role) || !(u16(e) & (1u << genre))) continue;
    ++found;
    const int slot = diceCandidateSlot(found, rng.below(found));
    if (slot >= 0) candidates[slot] = e;
  }
  const uint8_t* chosen = nullptr; int best = INT_MAX;
  for (const uint8_t* e : candidates) if (e) {
    DicePhraseFeatures features;
    const uint8_t* p = data_ + u32(e + 8);
    for (uint32_t i = 0; i < u32(e + 12) / kEvent; ++i, p += kEvent)
      features.add({u16(p), u16(p + 2), p[4], p[5], p[6]});
    const int score = features.score(role, e[5], seed);
    if (score < best) { best = score; chosen = e; }
  }
  if (!chosen) return false;
  out.bars = chosen[5]; out.root = chosen[3]; out.scale = chosen[4];
  memcpy(out.name, chosen + 16, sizeof(out.name));
  out.count = static_cast<uint16_t>(u32(chosen + 12) / kEvent);
  const uint8_t* p = data_ + u32(chosen + 8);
  for (int i = 0; i < out.count; ++i, p += kEvent)
    out.events[i] = {u16(p), u16(p + 2), p[4], p[5], p[6]};
  return true;
}

DicePhraseResult diceWritePhrase(const DicePhrase& ph, DiceRole role, Step* out, int len,
                                Resolution res, uint8_t root, ScaleType scale, const Instrument* kit) {
  DicePhraseResult result;
  if (!out || len < 1 || len > kMaxSteps || !ph.count || ph.count > kDiceMidiEvents || !ph.bars ||
      ph.bars > 8 || ph.root > 11 || ph.scale >= static_cast<int>(ScaleType::Count)) return result;
  for (int i = 0; i < kMaxSteps; ++i) out[i] = Step();
  const int q = ticksPerStep(res), span = ph.bars * 384;
  uint16_t durationAt[kMaxSteps] = {};
  for (int repeat = 0; repeat * span < len * q; ++repeat) {
    for (int i = 0; i < ph.count;) {
      const int tick = ph.events[i].tick + repeat * span, step = (tick + q / 2) / q;
      int end = i + 1;
      while (end < ph.count && (ph.events[end].tick + repeat * span + q / 2) / q == step) ++end;
      if (step >= len) { i = end; continue; }
      Step& st = out[step]; uint8_t pitches[32]; int n = 0, duration = ph.events[i].duration;
      int velocity = 0;
      for (int j = i; j < end; ++j) {
        const auto& ev = ph.events[j];
        velocity = std::max(velocity, static_cast<int>(ev.vel));
        if (ev.duration != ph.events[i].duration) result.approximate = true;
        duration = std::max(duration, static_cast<int>(ev.duration));
        if (role == DiceRole::Drums) {
          int lane = ev.lane;
          auto filled = [&](int at) {
            return kit && at >= 0 && at < kKitLanes &&
                   (kit->kit[at].instr != kNoInstr || kit->kit[at].sample[0]);
          };
          if (!filled(lane)) {
            lane = 5;  // an unprovided tom/cymbal keeps its rhythm on the kit's percussion voice
            if (!filled(lane)) {
              lane = 0;
              while (lane < kKitLanes && !filled(lane)) ++lane;
            }
            result.approximate = true;
          }
          if (!filled(lane)) { ++result.dropped; continue; }
          setLane(st, lane, true);
        } else {
          const int note = scaleNote(ev.note, ph, root, scale);
          bool exists = false; for (int k = 0; k < n; ++k) exists |= pitches[k] == note;
          if (!exists && n < 32) pitches[n++] = static_cast<uint8_t>(note);
        }
      }
      if (role == DiceRole::Drums) {
        if (laneMask(st)) { st.note = static_cast<uint8_t>(velocity); result.changed = true; }
      } else if (n) {
        std::sort(pitches, pitches + n);
        st.note = pitches[role == DiceRole::Bass ? 0 : n - 1];
        st.vel = static_cast<uint8_t>(velocity);
        if (role == DiceRole::Chords && n > 1) result.approximate |= fitChord(pitches, n, root, scale, st);
        else if (n > 1) result.approximate = true;
        put(st, Fx::GAT, nearestGate(duration, static_cast<int64_t>(q) * kPpqn));
        durationAt[step] = static_cast<uint16_t>(duration);
        result.changed = true;
      }
      if (st.hasNote()) {
        int offset = (tick - step * q) * 100 / q;
        if (offset) put(st, Fx::NDG, static_cast<uint8_t>(static_cast<int8_t>(offset)));
      }
      i = end;
    }
  }
  // CHD cannot tie voices in the current sequencer. Sustain long phrases with bounded
  // continuations instead of silently shortening them to eight steps; articulation is approximate.
  if (role != DiceRole::Drums) for (int at = 0; at < len; ++at) {
    if (!durationAt[at] || durationAt[at] <= 8 * q) continue;
    int end = len;
    for (int next = at + 1; next < len; ++next) if (durationAt[next]) { end = next; break; }
    int remaining = std::min(static_cast<int>(durationAt[at]), (end - at) * q);
    const Step base = out[at];
    for (int cursor = at; remaining > 0 && cursor < end; cursor += 8) {
      if (cursor != at) out[cursor] = base;
      put(out[cursor], Fx::GAT, nearestGate(std::min(remaining, 8 * q),
                                           static_cast<int64_t>(q) * kPpqn));
      remaining -= 8 * q;
    }
    result.approximate = true;
  }
  result.approximate |= result.dropped != 0;
  return result;
}
}  // namespace mt
