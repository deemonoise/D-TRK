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

// Step velocity: `vel` (0 = the track default) with VRN, clamped to 1..127.
int stepVelocity(const Step& s, uint8_t vel, const TrackCfg& t, Rng& rng) {
  int v = vel ? vel : t.defVel;
  if (const FxSlot* f = s.find(Fx::VRN)) v += spread(rng, f->val);
  return v < 1 ? 1 : (v > 127 ? 127 : v);
}

// Ratchet timing of a note step: RAT hits, sub-step and gate length (GAT or the track default).
struct Hits {
  int rat;
  uint32_t sub;
  uint32_t gateUs;
};

Hits noteHits(const Step& s, const TrackCfg& t, uint32_t stepUs) {
  Hits h;
  h.rat = 1;
  if (const FxSlot* r = s.find(Fx::RAT)) h.rat = r->val < 2 ? 2 : (r->val > 8 ? 8 : r->val);
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
  const int vel = stepVelocity(s, s.note, t, rng);
  uint8_t accent = 0xFF;
  if (const FxSlot* f = s.find(Fx::ACC)) accent = f->val;
  const Hits h = noteHits(s, t, c.stepUs);
  for (int i = 0; i < h.rat; ++i) {
    const int32_t on = nudge + static_cast<int32_t>(h.sub * i);
    if (off && on >= out.offUs) continue;  // after OFF: silent
    for (int l = 0; l < kKitLanes && out.count < kMaxStepEvents - 1; ++l) {
      if (!(s.vel & (1u << l))) continue;
      int v = (accent & (1u << l)) ? vel : vel * 3 / 5;
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
  const uint32_t stepUs = c.stepUs;

  if (const FxSlot* f = s.find(Fx::CND)) {
    if (!cndPasses(f->val, c.loop, c.fill)) return false;
  }
  if (const FxSlot* p = s.find(Fx::PRB)) {
    if (rng.below(100) >= p->val) return false;
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

  const int vel = stepVelocity(s, s.vel, t, rng);

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
    } else {
      const FxSlot* arp = s.find(Fx::ARP);
      const int offs[3] = {0, arp && arp->val ? arp->val >> 4 : 12, arp && arp->val ? arp->val & 15 : -1};
      for (int k = 0; k < 3; ++k) {
        const int n = root + offs[k];
        if (offs[k] >= 0 && n <= 127) a.notes[a.n++] = static_cast<uint8_t>(n);
      }
    }
    a.mode = static_cast<uint8_t>((ars->val >> 4) & 3);
    a.div = static_cast<uint8_t>((ars->val & 15) < 1 ? 1 : ((ars->val & 15) > kArmRateMax ? kArmRateMax : (ars->val & 15)));
    a.vel = static_cast<uint8_t>(vel);
    a.ch = ch;
    a.gate = gatePercent(t.defGate);
    if (const FxSlot* g = s.find(Fx::GAT)) a.gate = gatePercent(g->val);
    a.k = 0;
    a.wait = 0;
    const int first = a.mode == 3 ? static_cast<int>(rng.below(a.n)) : arpIndex(a.mode, 0, a.n);
    notes[0] = a.notes[first];
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
      out.ev[out.count++] = {on, EvKind::NoteOn, ch, notes[k], static_cast<uint8_t>(vel)};
      const bool last = i == h.rat - 1;
      if (!(last && out.tie))
        out.ev[out.count++] = {noteOffAt(on, h.gateUs, off, out.offUs), EvKind::NoteOff, ch, notes[k], 0};
    }
  }
  return true;
}

}  // namespace mt
