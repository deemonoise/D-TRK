#pragma once
#include <stdint.h>
#include "arp_gen.h"
#include "model.h"
#include "rng.h"

namespace mt {

// Helpers shared by the FILL generators (arp, chords, bass): the step token grammar and the writes.

int genClamp(int v, int lo, int hi);
bool isBlankChar(char c);  // token separator: space, tab, newline, ','
bool genFx(Fx f);          // the generators' fx: GAT TIE SLD NDG RAT PRB CHD ARS ARP
// The slot already holding f, else the first free one. False: no slot (fx skipped).
bool putFx(Step& s, Fx f, uint8_t v);
// Steps s0..s1 of a track: note empty, velocity default, genFx slots cleared (other fx stay).
void clearGenRange(Step* track, int s0, int s1);

// One step token at p (p moves past it; trailing blanks are not skipped). Reads the kind char
// (x X o . -), every following modifier char that is not ':' / blank into mods (cap incl. '\0';
// each distinct char once, at its last position, so applying mods in order = last one wins; chars
// past cap dropped), then ':vel:gate:nudge'. Applies s / l / ~ itself (not put into mods).
// False: end of text or unknown kind char (p is moved past the token, the caller skips it).
bool parseGenToken(const char*& p, ArpStep& a, char* mods, int cap);
// Appends one step at out[n]: kind char, s / l / ~, then mods, then ':vel:gate[:nudge]' when set
// (a rest / tie is its kind char alone). False when out of space (keeps room for '\0'). n updated.
bool formatGenToken(const ArpStep& a, const char* mods, char* out, int cap, int& n);

// Velocity / gate % as applyArp computes them (ghost = lo, accent = hi, norm = mid, velPct exact;
// gatePct x gate / 50 x rate, else short / norm / long x rate).
uint8_t genVelocity(const ArpStep& a, int velLo, int velHi);
int genGatePct(const ArpStep& a, int gate, int rate);

// Seeded variation of one step: hits toggled, accents and pitches redrawn. Always draws the same
// count of numbers so a step's change does not shift the next ones. pitchDraw (optional): the drawn
// pitch value 0..3 (Next, Repeat, Root, Next + 12) when the pitch was redrawn, else -1.
void mutateStep(ArpStep& a, int amount, Rng& r, int* pitchDraw = nullptr);

}  // namespace mt
