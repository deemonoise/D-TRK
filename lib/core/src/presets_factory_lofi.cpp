// Factory LOFI presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

constexpr FactoryPreset kTable[] = {
    // BYTE: the formula's index in bytebeat_lib.cpp; songs play at their own speed on C-4.
    {InstrType::Lofi, "BYTE", "42 MELDY", [](I& m) { bb(m, 0); }},
    {InstrType::Lofi, "BYTE", "VIZNUT", [](I& m) { bb(m, 1); tl(m, 30, 0); }},
    {InstrType::Lofi, "BYTE", "SIERPNSK", [](I& m) { bb(m, 5); tl(m, 20, 0); fx(m, 30, 0); }},
    {InstrType::Lofi, "BYTE", "LOSTSPC", [](I& m) { bb(m, 8); tl(m, 40, 0); fx(m, 40, 40); }},
    {InstrType::Lofi, "BYTE", "NIBBLES", [](I& m) { bb(m, 16); tl(m, 25, 0); }},
    {InstrType::Lofi, "BYTE", "MASK 27", [](I& m) { bb(m, 21); mac(m, 27, 0, 0, 0, 0); }},  // 42 melody, mask 27
    {InstrType::Lofi, "BYTE", "SQ DUET", [](I& m) {  // two squares, rhythm gate (C 20: t>>9, 64 ms rests); LFO on B
       bb(m, 23); mac(m, 40, 60, 20, 15, 0); lp(m, 100, 20, 0, 40); lfo1(m, LfoWave::Tri, LfoDest::Col, 30, 20); }},
    {InstrType::Lofi, "BYTE", "FRACBELL", [](I& m) { bb(m, 24); mac(m, 50, 70, 90, 50, 0); fx(m, 30, 70); }},
    {InstrType::Lofi, "BYTE", "LOOP ARP", [](I& m) { bb(m, 19); tl(m, 35, 40); fx(m, 50, 20); }},
    {InstrType::Lofi, "BYTE", "BYTEBASS", [](I& m) {  // signed pulse an octave down, A = pitch, B = level
       bb(m, 25); mac(m, 16, 100, 0, 20, 0); m.transpose = -12; lp(m, 80, 30, 25, 60); m.mono = true; }},
    // SYNC: t locked to the tempo (1024 per step), the rhythm formulas play on the grid.
    {InstrType::Lofi, "BYTE", "SYNCBEAT", [](I& m) { bb(m, 37); m.lofiSync = 1; tl(m, 20, 0); }},  // BACKBEAT
    {InstrType::Lofi, "BYTE", "SYNCHATS", [](I& m) { bb(m, 36); m.lofiSync = 1; fx(m, 20, 20); }},  // HATS
    {InstrType::Lofi, "BYTE", "SYNCARP", [](I& m) {  // ARP
       bb(m, 38); m.lofiSync = 1; tl(m, 40, 0); fx(m, 50, 30); }},
    {InstrType::Lofi, "BYTE", "SYNCBASS", [](I& m) {  // BASSLN
       bb(m, 40); m.lofiSync = 1; tl(m, 30, 0); lp(m, 85, 40, 30, 50); m.mono = true; }},
    // APC: WIDTH under a period = the note; above it swallows edges (subharmonics).
    {InstrType::Lofi, "APC", "APC", [](I& m) { apc(m); }},
    {InstrType::Lofi, "APC", "APC SUB", [](I& m) { apc(m); mac(m, 90, 90, 0, 30, 0); m.transpose = 12; }},
    {InstrType::Lofi, "APC", "APC DRFT", [](I& m) { apc(m); mac(m, 50, 110, 90, 40, 0); fx(m, 40, 30); }},
    {InstrType::Lofi, "APC", "APC SOFT", [](I& m) { apc(m); mac(m, 30, 70, 20, 127, 0); lp(m, 95, 20, 20, 60); }},
    // TEXTURE: smoothed (TONE up), low-passed, slow envelopes and reverb: beds and drones.
    {InstrType::Lofi, "TEXTURE", "SIERPAD", [](I& m) {  // Sierpinski tones, smoothed into a pad
       bb(m, 5); tl(m, 70, 0); env(m, 60, 90, 127, 95); lp(m, 90, 10, 0, 40); m.vol = 110; fx(m, 30, 90); }},
    {InstrType::Lofi, "TEXTURE", "BELLHAZE", [](I& m) {  // fractal bells, masks drift slowly
       bb(m, 24); mac(m, 50, 70, 90, 80, 0); env(m, 40, 90, 127, 95); lp(m, 90, 5, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Col, 12, 10); m.vol = 80; fx(m, 40, 90); }},
    {InstrType::Lofi, "TEXTURE", "LOSTFOG", [](I& m) {  // Lost in Space, far away
       bb(m, 8); tl(m, 100, 0); env(m, 65, 90, 127, 95); lp(m, 75, 5, 0, 40); m.vol = 100; fx(m, 40, 100); }},
    {InstrType::Lofi, "TEXTURE", "RAINBYTE", [](I& m) {  // chaotic BLUEBRY noise, very dark: rain bed
       bb(m, 9); tl(m, 100, 0); env(m, 65, 90, 127, 95); lp(m, 78, 0, 0, 40); m.vol = 75; fx(m, 0, 90); }},
    {InstrType::Lofi, "TEXTURE", "BUZZDRON", [](I& m) {  // SKURK drone, filtered and smoothed
       bb(m, 7); tl(m, 100, 0); env(m, 65, 90, 127, 95); lp(m, 72, 10, 0, 40);
       lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 10, 10); m.vol = 70; fx(m, 20, 80); }},
    {InstrType::Lofi, "TEXTURE", "TRIHOP", [](I& m) {  // octave-hopping triangle, soft: braindance
       bb(m, 18); tl(m, 80, 0); lp(m, 95, 5, 0, 40); m.vol = 85; fx(m, 30, 50); }},
    {InstrType::Lofi, "TEXTURE", "GLITCHLP", [](I& m) {  // MODSEQ in a short loop, tamed: glitch
       bb(m, 33); tl(m, 40, 30); lp(m, 95, 10, 0, 40); m.vol = 70; fx(m, 30, 30); }},
    {InstrType::Lofi, "TEXTURE", "SOFTARP", [](I& m) {  // minor arp on the grid, smoothed
       bb(m, 39); m.lofiSync = 1; tl(m, 80, 0); lp(m, 88, 5, 0, 40); m.vol = 85; fx(m, 50, 50); }},
    {InstrType::Lofi, "TEXTURE", "SYNCCHRD", [](I& m) {  // chord stabs on the grid: dub techno / deep house
       bb(m, 42); m.lofiSync = 1; tl(m, 70, 0); lp(m, 90, 10, 0, 40); m.vol = 80; fx(m, 40, 50); }},
    {InstrType::Lofi, "TEXTURE", "SYNCMEL", [](I& m) {  // 16-step melody on the grid, rounded: IDM
       bb(m, 43); m.lofiSync = 1; tl(m, 70, 0); lp(m, 92, 5, 0, 40); m.vol = 85; fx(m, 40, 40); }},
    {InstrType::Lofi, "TEXTURE", "APCDRONE", [](I& m) {  // subharmonics shift as WIDTH breathes
       apc(m); mac(m, 110, 60, 60, 100, 0); m.transpose = -12; env(m, 68, 95, 127, 100); lp(m, 78, 5, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Dec, 15, 15); m.vol = 75; fx(m, 20, 80); }},
    {InstrType::Lofi, "TEXTURE", "APC HUM", [](I& m) {  // smooth, drifting, dark hum
       apc(m); mac(m, 30, 50, 30, 120, 0); env(m, 68, 95, 127, 100); lp(m, 70, 0, 0, 40); m.vol = 80; fx(m, 10, 70); }},
};

}  // namespace

const FactoryTable kFactoryLofi = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
