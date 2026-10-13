// FillDialog's CHORDS and BASS types: rows, user file lists, resolving and capture.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "fill_dialog.h"
#include "fx_info.h"
#include "note_name.h"
#include "storage/bass_store.h"
#include "storage/prog_store.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint8_t clampu8(int v, int lo, int hi) { return static_cast<uint8_t>(clampi(v, lo, hi)); }

template <typename E>
E stepEnum(E e, int d) {
  return static_cast<E>(clampi(static_cast<int>(e) + d, 0, static_cast<int>(E::Count) - 1));
}

void formatSlide(uint8_t v, char* o, int n) {
  char s[5];
  mt::fxFormat(mt::Fx::SLD, v, s);
  const char* p = s;
  while (*p == ' ') ++p;
  snprintf(o, n, "%s", p);
}

}  // namespace

void FillDialog::initGenParams() {
  params_[kCProg] = {"Progression", [this](char* o, int n) { snprintf(o, n, "%s", progName()); },
                     // resolved in onEdit, outside the project lock (a user progression reads the card)
                     [this](int d) {
                       const int count = progCount();
                       c_->prog = clampu8(c_->prog + d, 0, count - 1);
                       c_->random = c_->prog == count - 1;
                     }};
  params_[kCChords] = {"Chords", [this](char* o, int n) { snprintf(o, n, "%u", c_->chords); },
                       [this](int d) { c_->chords = clampu8(c_->chords + d, 2, 8); }};
  params_[kCOctave] = {"Octave", [this](char* o, int n) { snprintf(o, n, "C%u", c_->octave); },
                       [this](int d) { c_->octave = clampu8(c_->octave + d, 2, 5); }};
  params_[kCFlavor] = {"Chord Type", [this](char* o, int n) { snprintf(o, n, "%s", mt::chordFlavorName(c_->flavor)); },
                       [this](int d) { c_->flavor = stepEnum(c_->flavor, d); }};
  params_[kCVoicing] = {"Voicing",
                        [this](char* o, int n) { snprintf(o, n, "%s", mt::chordVoicingName(c_->voicing)); },
                        [this](int d) { c_->voicing = stepEnum(c_->voicing, d); }};
  params_[kCLength] = {"Length",
                       [this](char* o, int n) {
                         if (c_->length == 0) snprintf(o, n, "AUTO");
                         else snprintf(o, n, "%u", c_->length);
                       },
                       [this](int d) { c_->length = clampu8(c_->length + d, 0, 64); }};
  params_[kCDest] = {"Dest",
                     [this](char* o, int n) {
                       snprintf(o, n, "T%d %s", c_->dest + 1, app_.project().tracks[c_->dest].name);
                     },
                     [this](int d) { c_->dest = clampu8(c_->dest + d, 0, mt::kTracks - 1); }, nullptr,
                     [this] { return app_.project().trackIsDrum(c_->dest); }};  // a drum Dest is not written
  params_[kCRhythm] = {"Rhythm", [this](char* o, int n) { snprintf(o, n, "%s", mt::chordRhythmName(c_->rhythm)); },
                       [this](int d) { c_->rhythm = clampu8(c_->rhythm + d, 0, mt::chordRhythmCount() - 1); }};
  params_[kCGate] = {"Gate", [this](char* o, int n) { snprintf(o, n, "%u %%", c_->gate); },
                     [this](int d) { c_->gate = clampu8(c_->gate + d, 5, 100); }};
  params_[kCVelLo] = {"Vel Lo", [this](char* o, int n) { snprintf(o, n, "%u", c_->velLo); },
                      [this](int d) { c_->velLo = clampu8(c_->velLo + d, 1, 127); }};
  params_[kCVelHi] = {"Vel Hi", [this](char* o, int n) { snprintf(o, n, "%u", c_->velHi); },
                      [this](int d) { c_->velHi = clampu8(c_->velHi + d, 1, 127); }};
  params_[kCMutate] = {"Mutate", [this](char* o, int n) { snprintf(o, n, "%u %%", c_->mutate); },
                       [this](int d) { c_->mutate = clampu8(c_->mutate + d, 0, 100); }};
  params_[kCCapture] = {"Capture", [](char* o, int n) { snprintf(o, n, "SAVE DEST AS PROG"); }, nullptr};

  params_[kBSource] = {"Source", [this](char* o, int n) { snprintf(o, n, "%s", mt::bassSourceName(b_->source)); },
                       [this](int d) { b_->source = stepEnum(b_->source, d); }};
  params_[kBRoot] = {"Root",
                     [this](char* o, int n) {
                       char nn[4];
                       mt::noteName(b_->root, nn);
                       snprintf(o, n, "%s", nn);
                     },
                     [this](int d) { b_->root = clampu8(b_->root + d, 0, 127); }};
  params_[kBOctave] = {"Octave", [this](char* o, int n) { snprintf(o, n, "C%u", b_->octave); },
                       [this](int d) { b_->octave = clampu8(b_->octave + d, 1, 3); }};
  params_[kBDest] = {"Dest",
                     [this](char* o, int n) {
                       snprintf(o, n, "T%d %s", b_->dest + 1, app_.project().tracks[b_->dest].name);
                     },
                     [this](int d) { b_->dest = clampu8(b_->dest + d, 0, mt::kTracks - 1); }, nullptr,
                     [this] { return app_.project().trackIsDrum(b_->dest); }};  // a drum Dest is not written
  params_[kBPattern] = {"Pattern", [this](char* o, int n) { snprintf(o, n, "%s", bassName()); },
                        // resolved in onEdit, outside the project lock (a user pattern reads the card)
                        [this](int d) { b_->pattern = clampu8(b_->pattern + d, 0, bassCount() - 1); }};
  params_[kBRate] = {"Rate", [this](char* o, int n) { snprintf(o, n, "x%u", b_->rate); },
                     [this](int d) { b_->rate = clampu8(b_->rate + d, 1, 4); }};
  params_[kBRotate] = {"Rotate", [this](char* o, int n) { snprintf(o, n, "%u", b_->rotate); },
                       [this](int d) {
                         b_->rotate = clampu8(b_->rotate + d, 0, bassPat_.len ? bassPat_.len - 1 : 0);
                       }};
  params_[kBGate] = {"Gate", [this](char* o, int n) { snprintf(o, n, "%u %%", b_->gate); },
                     [this](int d) { b_->gate = clampu8(b_->gate + d, 5, 100); }};
  params_[kBSwing] = {"Swing", [this](char* o, int n) { snprintf(o, n, "%u %%", b_->swing); },
                      [this](int d) { b_->swing = clampu8(b_->swing + d, 0, 100); }};
  params_[kBVelLo] = {"Vel Lo", [this](char* o, int n) { snprintf(o, n, "%u", b_->velLo); },
                      [this](int d) { b_->velLo = clampu8(b_->velLo + d, 1, 127); }};
  params_[kBVelHi] = {"Vel Hi", [this](char* o, int n) { snprintf(o, n, "%u", b_->velHi); },
                      [this](int d) { b_->velHi = clampu8(b_->velHi + d, 1, 127); }};
  params_[kBSlide] = {"Slide", [this](char* o, int n) { formatSlide(b_->slide, o, n); },
                      [this](int d) { b_->slide = mt::fxStep(mt::Fx::SLD, b_->slide, d); }};
  params_[kBMutate] = {"Mutate", [this](char* o, int n) { snprintf(o, n, "%u %%", b_->mutate); },
                       [this](int d) { b_->mutate = clampu8(b_->mutate + d, 0, 100); }};
  params_[kBCapture] = {"Capture", [](char* o, int n) { snprintf(o, n, "SAVE DEST AS PATTERN"); }, nullptr};
}

void FillDialog::buildChordRows() {
  static const int kRowsC[] = {kType,   kCProg,  kCChords, kCOctave, kCFlavor, kCVoicing, kCLength,
                               kCDest,  kCRhythm, kCGate,  kCVelLo,  kCVelHi,  kCMutate,  kSeed,
                               kReseed, kCCapture, kOk,    kCancel};
  const bool seeded = c_->random || c_->mutate;
  for (const int id : kRowsC) {
    if (id == kCChords && !c_->random) continue;
    if ((id == kSeed || id == kReseed) && !seeded) continue;
    addRow(id);
  }
}

void FillDialog::buildBassRows() {
  static const int kRowsB[] = {kType,  kBSource, kBRoot,  kBOctave, kBDest, kBPattern, kBRate,
                               kBRotate, kBGate, kBSwing, kBVelLo,  kBVelHi, kBSlide,  kBMutate,
                               kSeed,  kReseed,  kBCapture, kOk,    kCancel};
  // With Source PROGRESSION the seed is the chord spec's (a RANDOM / mutated progression), see seed().
  const bool progSeeded = b_->source == mt::BassSource::Progression && (c_->random || c_->mutate);
  const bool seeded = b_->mutate || progSeeded;
  for (const int id : kRowsB) {
    if (id == kBRoot && b_->source != mt::BassSource::Root) continue;
    if ((id == kSeed || id == kReseed) && !seeded) continue;
    addRow(id);
  }
}

int FillDialog::progCount() const { return mt::progFactoryCount() + progUserCount_ + 1; }

const char* FillDialog::progName() const {
  const int i = c_->prog;
  if (i < mt::progFactoryCount()) return mt::progFactoryName(i);
  if (i < mt::progFactoryCount() + progUserCount_) return progUser_[i - mt::progFactoryCount()];
  return "RANDOM";
}

int FillDialog::bassCount() const { return mt::bassFactoryCount() + bassUserCount_; }

const char* FillDialog::bassName() const {
  const int i = b_->pattern;
  if (i < mt::bassFactoryCount()) return mt::bassFactoryName(i);
  if (i < bassCount()) return bassUser_[i - mt::bassFactoryCount()];
  return "?";
}

// prog_ from c_->prog: factory text, else the user file (a bad file plays the first factory one).
// RANDOM (c_->random) is always the last index: the user list may have changed since it was picked.
void FillDialog::resolveProg() {
  const int count = progCount();
  if (c_->random) c_->prog = static_cast<uint8_t>(count - 1);
  else if (c_->prog >= count - 1) c_->prog = 0;
  const int i = c_->prog;
  progResolved_ = i;
  if (c_->random) return;  // layoutChords draws it
  bool ok;
  if (i < mt::progFactoryCount()) {
    ok = mt::parseProgression(mt::progFactoryText(i), prog_);
  } else {
    const storage::Result r = storage::loadProg(progUser_[i - mt::progFactoryCount()], prog_);
    ok = r == storage::Result::Ok;
    if (!ok) app_.toast(storage::resultText(r));
  }
  if (!ok) mt::parseProgression(mt::progFactoryText(0), prog_);
}

// bassPat_ from b_->pattern: factory text, else the user file (a bad file plays the first factory one).
void FillDialog::resolveBass() {
  if (b_->pattern >= bassCount()) b_->pattern = 0;
  const int i = b_->pattern;
  bool ok;
  if (i < mt::bassFactoryCount()) {
    ok = mt::parseBassPattern(mt::bassFactoryText(i), bassPat_);
  } else {
    const storage::Result r = storage::loadBass(bassUser_[i - mt::bassFactoryCount()], bassPat_);
    ok = r == storage::Result::Ok;
    if (!ok) app_.toast(storage::resultText(r));
  }
  if (!ok) mt::parseBassPattern(mt::bassFactoryText(0), bassPat_);
  bassResolved_ = b_->pattern;
  if (b_->rotate >= (bassPat_.len ? bassPat_.len : 1)) b_->rotate = 0;
}

// The user lists (first time the type needs them) and the caches whose index changed. Outside the
// project lock: reads the card. BASS from PROGRESSION uses CHORDS' progression.
void FillDialog::syncGen() {
  const bool chords = f_->type == mt::FillType::Chords;
  const bool bass = f_->type == mt::FillType::Bass;
  if (chords || (bass && b_->source == mt::BassSource::Progression)) {
    if (!progListed_) {
      progListed_ = true;
      if (!progUser_) progUser_ = allocNames();
      progUserCount_ = progUser_ ? storage::listProgs(progUser_, kUserMax) : 0;
      progResolved_ = -1;
    }
    if (c_->prog != progResolved_ || c_->random != (c_->prog == progCount() - 1)) resolveProg();
  }
  if (chords && c_->rhythm != rhythmResolved_) {
    if (c_->rhythm >= mt::chordRhythmCount()) c_->rhythm = 0;
    if (!mt::parseArpPattern(mt::chordRhythmText(c_->rhythm), rhythmPat_)) rhythmPat_.len = 0;
    rhythmResolved_ = c_->rhythm;
  }
  if (bass) {
    if (!bassListed_) {
      bassListed_ = true;
      if (!bassUser_) bassUser_ = allocNames();
      bassUserCount_ = bassUser_ ? storage::listBass(bassUser_, kUserMax) : 0;
      bassResolved_ = -1;
    }
    if (b_->pattern != bassResolved_) resolveBass();
  }
}

int FillDialog::bassSegs(mt::ChordSeg* out) {
  if (b_->source != mt::BassSource::Progression) return 0;
  const mt::Project& p = app_.project();
  return mt::layoutChords(prog_, *c_, rangeLen(), p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType), out);
}

// Dest's range as it was at open() (the hand-made chords, not the preview) into /presets/PRG.
void FillDialog::captureProg(const char* name) {
  if (!open_ || !name || !*name) return;
  char clean[17];
  if (!storage::sanitize(name, clean)) {
    app_.toast("BAD NAME");
    return;
  }
  if (!progUser_) {
    app_.toast("NO MEMORY");
    return;
  }
  const mt::Project& p = app_.project();
  if (p.trackIsDrum(c_->dest)) {
    app_.toast("DRUM DEST");
    return;
  }
  mt::Progression cp;
  if (!mt::captureChords(*saved_, c_->dest, sel_.s0, sel_.s1, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType),
                         cp)) {
    app_.toast("NO NOTES");
    return;
  }
  app_.showBusy("SAVING...");
  const storage::Result r = storage::saveProg(clean, cp);
  if (r != storage::Result::Ok) {
    app_.toast(storage::resultText(r));
    return;
  }
  progUserCount_ = storage::listProgs(progUser_, kUserMax);
  progListed_ = true;
  bool found = false;
  for (int i = 0; i < progUserCount_ && !found; ++i)
    if (strcmp(progUser_[i], clean) == 0) {
      c_->prog = static_cast<uint8_t>(mt::progFactoryCount() + i);
      c_->random = false;
      found = true;
    }
  resolveProg();
  buildRows();
  preview();
  // Not listed (past the first kUserMax names): the file is saved all the same, just not selectable.
  app_.toast(found ? "SAVED" : "LIST FULL");
}

// Dest's range as it was at open() (the hand-made line, not the preview) into /presets/BAS. The roles
// are read against the chords of the current Source.
void FillDialog::captureBass(const char* name) {
  if (!open_ || !name || !*name) return;
  char clean[17];
  if (!storage::sanitize(name, clean)) {
    app_.toast("BAD NAME");
    return;
  }
  if (!bassUser_) {
    app_.toast("NO MEMORY");
    return;
  }
  const mt::Project& p = app_.project();
  bool drumTr[mt::kTracks];
  for (int t = 0; t < mt::kTracks; ++t) drumTr[t] = p.trackIsDrum(t);
  mt::ChordSeg segs[mt::kProgMax];
  const int segN = bassSegs(segs);
  // Captured into bassPat_ (no 400-byte copy on the stack); resolved again afterwards.
  const bool got = mt::captureBass(*saved_, b_->dest, sel_.s0, sel_.s1, *b_, p.scaleRoot,
                                   static_cast<mt::ScaleType>(p.scaleType), segs, segN, saved_, &sel_, bassPat_,
                                   drumTr);
  bassResolved_ = -1;
  if (!got) {
    resolveBass();
    app_.toast("NO NOTES");
    return;
  }
  app_.showBusy("SAVING...");
  const storage::Result r = storage::saveBass(clean, bassPat_);
  if (r != storage::Result::Ok) {
    resolveBass();
    app_.toast(storage::resultText(r));
    return;
  }
  bassUserCount_ = storage::listBass(bassUser_, kUserMax);
  bassListed_ = true;
  bool found = false;
  for (int i = 0; i < bassUserCount_ && !found; ++i)
    if (strcmp(bassUser_[i], clean) == 0) {
      b_->pattern = static_cast<uint8_t>(mt::bassFactoryCount() + i);
      found = true;
    }
  resolveBass();
  buildRows();
  preview();
  app_.toast(found ? "SAVED" : "LIST FULL");
}

}  // namespace ui
