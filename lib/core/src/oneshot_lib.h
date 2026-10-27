#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Save one-shot / KIT bake: where a rendered note goes on the card and which KIT lanes to bake.

constexpr int kOneshotDirMax = 32;
// Folder of the instrument's one-shots under /samples/oneshots: DRUM "909/BD" (model / group;
// PERC "CLAVE/PERC", "BELL/PERC"), FM "FM/KICK", SYNTH "SYNTH/SAW" (osc 1), CHIP "CHIP/WT3" (wave
// name, chars outside A-Z a-z 0-9 _ - as '_'), LOFI "LOFI/APC", SAMPLE, KIT, OTHER. Out of range
// machines as their type's fallback.
void oneshotDir(const Instrument& in, char out[kOneshotDirMax]);
// "/samples/oneshots/<dir>/<name>.wav", or <name>_1 .. _999 while exists(path, ctx); false if none
// is free or the path does not fit out[n].
bool oneshotFilePath(const char* dir, const char* name, bool (*exists)(const char* path, void* ctx),
                     void* ctx, char* out, int n);
// KIT lanes to bake, in lane order, into lanes; their count. A lane counts when it plays another
// instrument (instr < kInstruments) that is not a KIT or a SAMPLE, and no earlier lane has its note
// (the note-on takes the first lane with the note: a later one never sounds). 0 if instruments[kit]
// is not a KIT.
int kitBakeLanes(const Project& p, int kit, uint8_t lanes[kKitLanes]);
// A baked lane's vol: the sampler lane then plays its one-shot at the level it was rendered at. A
// sample voice plays at vol / 127 x kSampleGain (4) / 32768 per int16 step; the unnormalized bounce
// writes the preview tap x kPrevTapScale = kTapScale (32767 / 4) x 127 / kPreviewVol, so the preview
// fader is undone and 127 gives x 0.99997 of the lane's instrument as it played in the KIT (its own
// vol, the note's velocity applied again by the sampler).
constexpr uint8_t kLaneUnityVol = 127;
// The lane becomes a sampler on project sample `name` (cut to kSampleNameMax) at pitch 0, vol
// kLaneUnityVol, decay 0 (full: the one-shot's own tail); note stays. Out of range lane: nothing.
void kitLaneToSample(Instrument& kit, int lane, const char* name);

}  // namespace mt
