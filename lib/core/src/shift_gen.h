#pragma once
#include <stdint.h>
#include "model.h"
#include "scale.h"

namespace mt {

// Shift-register generator of a track (GenCfg, Project::gen). Each clock shifts the register left: the
// bit falling out of bit len-1 comes back as bit 0, inverted with probability flip %. Flip 0 locks the
// loop (period len), 100 locks it too (period 2·len, the second half inverted); in between it mutates.
// The chance comes from a hash of (seed, track, clocks since reset), not from a shared random generator:
// the same seed plays the same melody whatever else happens (PRB elsewhere, replanning, Render).
struct GenState {
  uint16_t reg = 1;
  uint8_t flipped = 0;  // the last shift inverted its bit (UI blink)
  uint8_t pad = 0;
  uint32_t n = 0;  // shifts since reset
};

inline uint16_t genMask(uint8_t len) {
  const int l = len < kGenLenMin ? kGenLenMin : (len > kGenLenMax ? kGenLenMax : len);
  return static_cast<uint16_t>((1u << l) - 1);
}
uint32_t genHash(uint32_t a, uint32_t b, uint32_t c);
// The start register (never 0), n = 0.
void genReset(GenState& s, const GenCfg& c, int track);
// One clock; flip 0..100 (an FLP lock or c.flip).
void genShift(GenState& s, const GenCfg& c, int track, uint8_t flip);
// The note of the register now: the low min(len, 8) bits pick a degree of the scale over c.oct octaves
// from c.low raised to the scale (the top tonic included); the range drops octaves to stay <= 127.
// BYTE modes: a hash of the clock's output bytes (genByteBytes) picks the degree, mod the range's degrees.
uint8_t genNote(const GenState& s, const GenCfg& c, uint8_t root, ScaleType t);
// GATE: the window's bits reversed (older bits weigh more) against Density; monotonic in it. BYTE GATE:
// 255 - genByteLevel against Density (the loud clocks play first).
bool genGate(const GenState& s, const GenCfg& c);
// BYTE modes: clock n is the formula's window of W = 1 << zoom ticks from t = n * W (zoom 10: a step is
// 1024 ticks, as LOFI SYNC). Its output bytes (x & 255) at 8 points t + k * D + (37 k mod D), D = W / 8
// (off the power-of-two grid, where t * K & 255 hardly moves); W < 8: D = 1, the points run on past the
// window. Returns how many (8). The register shifts on, unused, and never flips.
int genByteBytes(const GenState& s, const GenCfg& c, uint8_t v[8]);
// The window's loudness 0..255: twice the mean distance of the samples (signed formulas: as int8) from
// their mean, so silence at any level (0, 128) is 0 and a full square is 255.
uint8_t genByteLevel(const GenState& s, const GenCfg& c);
// Clocked by every step (GATE, BYTE GATE), else by the steps with a note.
inline bool genGateMode(uint8_t m) {
  return m == static_cast<uint8_t>(GenMode::Gate) || m == static_cast<uint8_t>(GenMode::ByteGate);
}
inline bool genByteMode(uint8_t m) {
  return m == static_cast<uint8_t>(GenMode::BytePitch) || m == static_cast<uint8_t>(GenMode::ByteGate);
}
// Flip as the UI shows it: "LOCK" (0), "1%".."99%", "x2 INV" (100: the loop doubled, second half inverted).
void genFlipText(uint8_t flip, char out[8]);
// Values into range (files): mode >= Count -> OFF, len, flip, density, oct, low, BYTE formula (past the
// library -> 0), a, b, c, zoom.
void genClamp(GenCfg& c);

// What a held track button + turn changes (App): INT tracks VOL <-> PAN, with a generator VOL -> PAN ->
// FLIP; a MIDI track with a generator FLIP only, without one nothing (VOL, shown as MIDI).
enum class HoldMode : uint8_t { Vol, Pan, Flip };
inline HoldMode holdFirst(bool internal, bool gen) { return !internal && gen ? HoldMode::Flip : HoldMode::Vol; }
inline HoldMode holdNext(HoldMode m, bool internal, bool gen) {
  if (!internal) return holdFirst(internal, gen);
  if (m == HoldMode::Vol) return HoldMode::Pan;
  if (m == HoldMode::Pan && gen) return HoldMode::Flip;
  return HoldMode::Vol;
}

}  // namespace mt
