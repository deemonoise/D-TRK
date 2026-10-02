#include "step_expand.h"

namespace mt {

namespace {
bool cndPasses(uint8_t v, uint32_t loop) {
  if (v == 0) return loop == 0;  // FST
  const uint32_t a = v >> 4, b = v & 15;
  if (b == 0) return true;
  return loop % b == a - 1;
}

int spread(Rng& rng, uint8_t range) { return static_cast<int>(rng.below(2u * range + 1)) - range; }
}  // namespace

bool expandStep(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, ExpandOut& out) {
  out.count = 0;
  out.tie = false;
  const uint32_t stepUs = c.stepUs;

  if (const FxSlot* f = s.find(Fx::CND)) {
    if (!cndPasses(f->val, c.loop)) return false;
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

  // Control events, in slot order, before any note.
  for (const FxSlot& f : s.fx) {
    switch (f.cmd) {
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
      default: break;
    }
  }

  if (!s.hasNote()) return out.count > 0;

  int root = s.note;
  if (const FxSlot* f = s.find(Fx::NRN)) {
    root = moveDegrees(root, spread(rng, f->val), c.scaleRoot, c.scale);
  }

  int vel = s.vel ? s.vel : t.defVel;
  if (const FxSlot* f = s.find(Fx::VRN)) vel += spread(rng, f->val);
  vel = vel < 1 ? 1 : (vel > 127 ? 127 : vel);

  uint8_t notes[4] = {static_cast<uint8_t>(root), 0, 0, 0};
  int nNotes = 1;
  if (const FxSlot* f = s.find(Fx::CHD)) {
    nNotes = chordNotes(static_cast<uint8_t>(root), f->val, c.scaleRoot, c.scale, notes);
    if (nNotes < 1) {
      notes[0] = static_cast<uint8_t>(root);
      nNotes = 1;
    }
  }

  uint32_t strum = 0;
  if (const FxSlot* f = s.find(Fx::STR)) strum = static_cast<uint32_t>(static_cast<uint64_t>(stepUs) * f->val / 100);

  int rat = 1;
  if (const FxSlot* r = s.find(Fx::RAT)) rat = r->val < 2 ? 2 : (r->val > 8 ? 8 : r->val);

  uint32_t gatePct = gatePercent(t.defGate);
  if (const FxSlot* g = s.find(Fx::GAT)) gatePct = gatePercent(g->val);

  out.tie = nNotes == 1 && s.find(Fx::TIE) != nullptr;  // a chord ignores TIE

  const uint32_t sub = stepUs / rat;
  uint32_t gateUs;
  if (rat > 1) {
    // Ratchet hits must not overlap: cap the gate below the sub-step.
    gateUs = static_cast<uint32_t>(static_cast<uint64_t>(sub) * (gatePct > 95 ? 95 : gatePct) / 100);
  } else {
    gateUs = static_cast<uint32_t>(static_cast<uint64_t>(stepUs) * gatePct / 100);
  }
  if (gateUs < kMinGateUs) gateUs = kMinGateUs;

  for (int i = 0; i < rat; ++i) {
    for (int k = 0; k < nNotes && out.count < kMaxStepEvents - 1; ++k) {
      const int32_t on = nudge + static_cast<int32_t>(sub * i + strum * k);
      out.ev[out.count++] = {on, EvKind::NoteOn, ch, notes[k], static_cast<uint8_t>(vel)};
      const bool last = i == rat - 1;
      if (!(last && out.tie)) {
        out.ev[out.count++] = {on + static_cast<int32_t>(gateUs), EvKind::NoteOff, ch, notes[k], 0};
      }
    }
  }
  return true;
}

}  // namespace mt
