#include "step_expand.h"
#include "fx_info.h"

namespace mt {

namespace {
bool cndPasses(uint8_t v, uint32_t loop, bool fill) {
  if (v == 0) return loop == 0;  // FST
  if (v == kCndFill) return fill;
  if (v == kCndNoFill) return !fill;
  const uint32_t a = v >> 4, b = v & 15;
  if (b == 0) return true;
  return loop % b == a - 1;
}

int spread(Rng& rng, uint8_t range) { return static_cast<int>(rng.below(2u * range + 1)) - range; }

// Step velocity: `vel` (0 = the track default) with VRN, the track's humanize (+-20 at 100) and the
// groove accent, clamped to 1..127.
int stepVelocity(const Step& s, uint8_t vel, const TrackCfg& t, const ExpandCtx& c, Rng& rng) {
  int v = vel ? vel : t.defVel;
  if (const FxSlot* f = s.find(Fx::VRN)) v += spread(rng, f->val);
  if (t.humanize) v += spread(rng, static_cast<uint8_t>(t.humanize > 100 ? 20 : t.humanize / 5));
  if (c.velPct != 100) v = v * c.velPct / 100;
  return v < 1 ? 1 : (v > 127 ? 127 : v);
}

// Ratchet timing of a note step: RAT hits, sub-step and gate length (GAT or the track default).
struct Hits {
  int rat;
  int ramp;  // 0 even, kRatUp, kRatDown
  uint32_t sub;
  uint32_t gateUs;
};

// Velocity of ratchet hit i: the step velocity, or a ramp to (kRatUp) / from (kRatDown) it.
int hitVel(int vel, int i, const Hits& h) {
  if (h.ramp == 0 || h.rat < 2) return vel;
  const int k = h.ramp == kRatUp ? i + 1 : h.rat - i;
  const int v = vel * k / h.rat;
  return v < 1 ? 1 : v;
}

Hits noteHits(const Step& s, const TrackCfg& t, uint32_t stepUs) {
  Hits h;
  h.rat = 1;
  h.ramp = 0;
  if (const FxSlot* r = s.find(Fx::RAT)) {
    const int n = r->val & 15;
    h.rat = n < 2 ? 2 : (n > 8 ? 8 : n);
    h.ramp = (r->val >> 4) & 3;
    if (h.ramp > kRatDown) h.ramp = 0;
  }
  uint32_t gatePct = gatePercent(t.defGate);
  if (const FxSlot* g = s.find(Fx::GAT)) gatePct = gatePercent(g->val);
  h.sub = stepUs / h.rat;
  if (h.rat > 1) {
    // Ratchet hits must not overlap: cap the gate below the sub-step.
    h.gateUs = static_cast<uint32_t>(static_cast<uint64_t>(h.sub) * (gatePct > 95 ? 95 : gatePct) / 100);
  } else {
    h.gateUs = static_cast<uint32_t>(static_cast<uint64_t>(stepUs) * gatePct / 100);
  }
  if (h.gateUs < kMinGateUs) h.gateUs = kMinGateUs;
  return h;
}

// NoteOff time of a hit at `on`: the gate, cut by OFF, never shorter than kMinGateUs.
int32_t noteOffAt(int32_t on, uint32_t gateUs, const FxSlot* off, int32_t offUs) {
  int32_t offT = on + static_cast<int32_t>(gateUs);
  if (off && offT > offUs) offT = offUs;
  if (offT < on + static_cast<int32_t>(kMinGateUs)) offT = on + static_cast<int32_t>(kMinGateUs);
  return offT;
}

// Drum track: vel = lane mask, note = step velocity (0 = default). One NoteOn per set lane per
// ratchet, note = the lane's note, velocity x 0.6 for lanes outside ACC. TIE, CHD, NRN, STR are ignored.
void expandDrum(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, uint8_t ch, int32_t nudge,
                const FxSlot* off, ExpandOut& out) {
  const Instrument& k = *c.kit;
  const int vel = stepVelocity(s, s.note, t, c, rng);
  uint8_t accent = 0xFF;
  if (const FxSlot* f = s.find(Fx::ACC)) accent = f->val;
  const Hits h = noteHits(s, t, c.stepUs);
  for (int i = 0; i < h.rat; ++i) {
    const int32_t on = nudge + static_cast<int32_t>(h.sub * i);
    if (off && on >= out.offUs) continue;  // after OFF: silent
    for (int l = 0; l < kKitLanes && out.count < kMaxStepEvents - 1; ++l) {
      if (!(s.vel & (1u << l))) continue;
      int v = (accent & (1u << l)) ? vel : vel * 3 / 5;
      v = hitVel(v, i, h);
      if (v < 1) v = 1;
      const uint8_t n = k.kit[l].note;
      out.ev[out.count++] = {on, EvKind::NoteOn, ch, n, static_cast<uint8_t>(v)};
      out.ev[out.count++] = {noteOffAt(on, h.gateUs, off, out.offUs), EvKind::NoteOff, ch, n, 0};
    }
  }
}
}  // namespace

bool expandStep(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, ExpandOut& out) {
  out.count = 0;
  out.tie = false;
  out.offUs = -1;
  out.arp.n = 0;
  out.cond = -1;
  const uint32_t stepUs = c.stepUs;

  // Conditions; PRE / NEI read the last result of the others (out.cond, kept by the sequencer).
  if (const FxSlot* f = s.find(Fx::CND)) {
    bool pass;
    switch (f->val) {
      case kCndPre: pass = c.pre; break;
      case kCndNotPre: pass = !c.pre; break;
      case kCndNei: pass = c.nei; break;
      case kCndNotNei: pass = !c.nei; break;
      default:
        pass = cndPasses(f->val, c.loop, c.fill);
        out.cond = pass ? 1 : 0;
        break;
    }
    if (!pass) return false;
  }
  if (const FxSlot* p = s.find(Fx::PRB)) {
    const bool pass = rng.below(100) < p->val;
    out.cond = pass ? 1 : 0;
    if (!pass) return false;
  }

  uint8_t ch = t.channel & 0x0F;
  if (const FxSlot* f = s.find(Fx::CHN)) {
    if (f->val >= 1 && f->val <= 16) ch = f->val - 1;
  }

  int32_t nudge = 0;
  if (const FxSlot* n = s.find(Fx::NDG)) {
    int v = fxSigned(n->val);
    if (v < -50) v = -50;
    if (v > 50) v = 50;
    nudge = static_cast<int32_t>(static_cast<int64_t>(stepUs) * v / 100);
  }
  // Humanize: the whole step up to +-10 % of a step off the grid (at 100).
  if (t.humanize && s.hasNote()) {
    const uint32_t r = stepUs / 10 * (t.humanize > 100 ? 100 : t.humanize) / 100;
    nudge += static_cast<int32_t>(rng.below(2 * r + 1)) - static_cast<int32_t>(r);
  }

  const FxSlot* off = s.find(Fx::OFF);
  if (off) {
    const uint32_t tps = c.tps ? c.tps : 24;
    out.offUs = nudge + static_cast<int32_t>(static_cast<uint64_t>(stepUs) * off->val / tps);
  }

  // ARS: the sequencer arpeggiates, the synth's own ARP / ARM stay out.
  const FxSlot* ars = c.kit ? nullptr : s.find(Fx::ARS);

  // Control events, in slot order, before any note.
  for (const FxSlot& f : s.fx) {
    switch (f.cmd) {
      case Fx::ARP:
      case Fx::ARM:
        if (ars) break;
        if (t.out == TrackOut::Int) out.ev[out.count++] = {nudge, EvKind::SynthFx, ch, static_cast<uint8_t>(f.cmd), f.val};
        break;
      case Fx::CCA:
      case Fx::CCB:
        out.ev[out.count++] = {nudge, EvKind::Cc, ch, static_cast<uint8_t>((f.cmd == Fx::CCA ? t.ccA : t.ccB) & 127),
                               static_cast<uint8_t>(f.val > 127 ? 127 : f.val)};
        break;
      case Fx::PBN: {
        const uint16_t v = static_cast<uint16_t>(8192 + fxSigned(f.val) * 128);
        out.ev[out.count++] = {nudge, EvKind::PitchBend, ch, static_cast<uint8_t>(v & 0x7F),
                               static_cast<uint8_t>((v >> 7) & 0x7F)};
        break;
      }
      case Fx::PGM:
        out.ev[out.count++] = {nudge, EvKind::Program, ch, static_cast<uint8_t>(f.val & 127), 0};
        break;
      default:
        if (fxSynthOnly(f.cmd) && t.out == TrackOut::Int)
          out.ev[out.count++] = {nudge, EvKind::SynthFx, ch, static_cast<uint8_t>(f.cmd), f.val};
        break;
    }
  }

  if (!s.hasNote()) return out.count > 0 || off;

  // OFF on a step with a note: not before the minimum gate.
  if (off && out.offUs < nudge + static_cast<int32_t>(kMinGateUs)) out.offUs = nudge + static_cast<int32_t>(kMinGateUs);

  if (c.kit) {
    expandDrum(s, t, c, rng, ch, nudge, off, out);
    return true;
  }

  int root = s.note;
  if (const FxSlot* f = s.find(Fx::NRN)) {
    root = moveDegrees(root, spread(rng, f->val), c.scaleRoot, c.scale);
  }

  const int vel = stepVelocity(s, s.vel, t, c, rng);

  uint8_t notes[4] = {static_cast<uint8_t>(root), 0, 0, 0};
  int nNotes = 1;
  if (const FxSlot* f = s.find(Fx::CHD)) {
    nNotes = chordNotes(static_cast<uint8_t>(root), f->val, c.scaleRoot, c.scale, notes);
    if (nNotes < 1) {
      notes[0] = static_cast<uint8_t>(root);
      nNotes = 1;
    }
    // INT + ARP + CHD: the synth arpeggiates the chord from the root alone (kSynthArpChord, before the
    // note-on). MIDI tracks have no ARP and keep the whole chord; ARP 00 is off: a plain chord.
    const FxSlot* arp = s.find(Fx::ARP);
    if (!ars && t.out == TrackOut::Int && arp && arp->val && nNotes > 1 && out.count < kMaxStepEvents) {
      out.ev[out.count++] = {nudge, EvKind::SynthFx, ch, kSynthArpChord, f->val};
      nNotes = 1;
    }
  }

  if (ars) {
    StepArp& a = out.arp;
    a.n = 0;
    if (nNotes > 1) {
      for (int k = 0; k < nNotes; ++k) a.notes[a.n++] = notes[k];
    } else if (const FxSlot* arp = s.find(Fx::ARP); arp && arp->val) {
      const int offs[3] = {0, arp->val >> 4, arp->val & 15};
      for (int k = 0; k < 3; ++k) {
        const int n = root + offs[k];
        if (n <= 127) a.notes[a.n++] = static_cast<uint8_t>(n);
      }
    } else {
      a.notes[a.n++] = static_cast<uint8_t>(root);  // the root alone: its octaves
    }
    a.oct = static_cast<uint8_t>(((ars->val >> 6) & 3) + 1);
    if (a.n == 1 && a.oct < 2) a.oct = 2;
    a.mode = static_cast<uint8_t>((ars->val >> 4) & 3);
    a.div = static_cast<uint8_t>((ars->val & 15) < 1 ? 1 : ((ars->val & 15) > kArmRateMax ? kArmRateMax : (ars->val & 15)));
    a.vel = static_cast<uint8_t>(vel);
    a.ch = ch;
    a.gate = gatePercent(t.defGate);
    if (const FxSlot* g = s.find(Fx::GAT)) a.gate = gatePercent(g->val);
    a.k = 0;
    a.wait = 0;
    const int all = a.count();
    const int first = a.mode == 3 ? static_cast<int>(rng.below(all)) : arpIndex(a.mode, 0, all);
    notes[0] = a.note(first);
    nNotes = 1;
  }

  uint32_t strum = 0;
  if (const FxSlot* f = s.find(Fx::STR)) strum = static_cast<uint32_t>(static_cast<uint64_t>(stepUs) * f->val / 100);

  const Hits h = noteHits(s, t, stepUs);
  out.tie = !off && !ars && nNotes == 1 && s.find(Fx::TIE) != nullptr;  // a chord or an arp ignores TIE

  for (int i = 0; i < h.rat; ++i) {
    for (int k = 0; k < nNotes && out.count < kMaxStepEvents - 1; ++k) {
      const int32_t on = nudge + static_cast<int32_t>(h.sub * i + strum * k);
      if (off && on >= out.offUs) continue;  // after OFF: silent
      out.ev[out.count++] = {on, EvKind::NoteOn, ch, notes[k], static_cast<uint8_t>(hitVel(vel, i, h))};
      const bool last = i == h.rat - 1;
      if (!(last && out.tie))
        out.ev[out.count++] = {noteOffAt(on, h.gateUs, off, out.offUs), EvKind::NoteOff, ch, notes[k], 0};
    }
  }
  return true;
}

}  // namespace mt
