#pragma once
#include "dice_recipes.h"

namespace mt { namespace dice_detail {

// Offsets relative to each recipe. The last four controls describe the FM
// kick's body tune, pitch sweep, transient modulator and saturation.
struct SoundDirection {
  int8_t tone, release, kickTune, sweep, attackTone, over;
  const char* accent[2];
};
constexpr SoundDirection directions[] = {
    { 0, 0,-2, 7,68, 9,{"PLUCK","PULSE"}}, // Default
    {-5,-5,-4,14,73,14,{"MALLET","PLUCK"}}, // Minimal
    { 0,-7,-4,18,78,18,{"PULSE","FM BELL"}}, // PsyRoll
    {-2, 3,-3, 8,70, 9,{"E PIANO","MALLET"}}, // Liquid
    {-5, 3,-2, 6,67, 8,{"ACOU GTR","E PIANO"}}, // Warm
    { 3,-6,-2,13,76,14,{"FM BELL","PULSE"}}, // Glitch
    { 1,-2,-3,16,77,22,{"VOX FORM","PULSE"}}, // Noise
    { 2,-4,-4,14,73,16,{"303","PLUCK"}}, // Acid
    {-8, 8,-3, 4,65, 6,{"DRONE","FM BELL"}}, // Drone
    { 3, 0,-4,16,75,16,{"SAW LEAD","PLUCK"}}, // Anthem
    {-3, 6,-2, 5,65, 6,{"MALLET","FM BELL"}}, // Air
    { 0,-4,-3,14,76,14,{"PLUCK","FM KEYS"}}, // Breaks
    {-2, 0,-2, 8,69,10,{"MALLET","E PIANO"}}, // Swing
    {-4,-3,-1,11,72, 9,{"PULSE","PLUCK"}}, // Sub
    { 1,-4,-3,15,76,15,{"PULSE","FM KEYS"}}, // Reese
    {-7, 4,-3, 7,68, 9,{"ORGAN","PLUCK"}}, // Dub
    { 0,-2,-4,13,72,15,{"STAB","PULSE"}}, // Club
    {-4, 2,-2, 7,68, 8,{"ACOU GTR","ORGAN"}}, // Roots
    { 3,-5,-5,19,80,36,{"SAW LEAD","PULSE"}}, // Hard
};
static_assert(sizeof(directions)/sizeof(*directions)==unsigned(DiceVoicing::Count),"one sound direction per voicing");

// A roll shares a small ensemble, a four-attack motif in scale degrees and a
// comping rhythm (sixteenth-note positions). Native guide onsets remain the groove.
struct DiceEnsemble {
  const char *bass, *keys, *lead, *arp;
  int8_t cell[4];
  uint8_t stabs[3];
};

inline const DiceEnsemble& diceEnsemble(DiceFeel feel, uint32_t seed) {
  static const DiceEnsemble ensembles[][2] = {
    {{"SUB","ORGAN","PLUCK","FM KEYS",       {0,2,0,4},{2,6,14}},
     {"808 SUB","E PIANO","PULSE","PLUCK",  {2,0,3,2},{2,9,14}}}, // Pocket
    {{"REESE","E PIANO","MALLET","PULSE",    {0,0,4,-1},{0,7,10}},
     {"SUB","STAB","FM KEYS","PLUCK",        {0,3,-2,0},{3,8,14}}}, // Broken
    {{"SUB","WARM PAD","FM BELL","MALLET",   {0,1,4,2},{0,8,14}},
     {"ANALOGBS","DRONE","PLUCK","FM KEYS",  {2,1,0,-2},{0,6,12}}}, // Air
    {{"SUB","E PIANO","ACOU GTR","MALLET",   {2,3,4,1},{0,6,11}},
     {"ANALOGBS","ORGAN","FM KEYS","PLUCK",  {4,2,1,0},{3,7,12}}}, // Keys
    {{"SUB","ORGAN","ACOU GTR","PLUCK",      {0,0,2,-1},{2,6,10}},
     {"808 SUB","STAB","MALLET","E PIANO",   {0,3,2,0},{2,10,14}}}, // Echo
    {{"ANALOGBS","STAB","PULSE","303",       {0,0,1,4},{2,8,14}},
     {"SUB","PLUCK","303","PULSE",           {0,4,0,-1},{0,6,12}}}, // Motor
    {{"ANALOGBS","WARM PAD","SAW LEAD","PLUCK",{0,2,4,7},{0,6,12}},
     {"SUB","E PIANO","PLUCK","PULSE",       {4,5,2,0},{0,8,14}}}, // Anthem
  };
  return ensembles[static_cast<unsigned>(feel)][(seed ^ (seed >> 16)) & 1];
}
}}
