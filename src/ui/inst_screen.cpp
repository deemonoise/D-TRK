#include "inst_screen.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "note_name.h"
#include "name_edit.h"
#include "synth_fm_machines.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void envTime(uint8_t v, char* o, int n) {
  const unsigned ms = mt::envTimeMs(v);
  if (ms < 1000) snprintf(o, n, "%u ms", ms);
  else snprintf(o, n, "%u.%u s", ms / 1000, ms % 1000 / 100);
}

void waveName(uint8_t w, char* o, int n) {
  static const char* const kNames[] = {"PULSE", "TRI", "SAW", "NOISE", "METAL"};
  constexpr int kWt = static_cast<int>(mt::Wave::Wt1);
  if (w < kWt) snprintf(o, n, "%s", kNames[w]);
  else snprintf(o, n, "WT%d", w - kWt + 1);
}

// Region fractions (/0xFFFF) are edited in 0.1 % steps.
int toPermille(uint16_t v) { return (v * 1000 + 0x7FFF) / 0xFFFF; }
uint16_t fromPermille(int p) { return static_cast<uint16_t>((p * 0xFFFF + 500) / 1000); }
void permille(uint16_t v, char* o, int n) {
  const int p = toPermille(v);
  snprintf(o, n, "%d.%d%%", p / 10, p % 10);
}

}  // namespace

InstScreen::InstScreen(App& app) : app_(app) {
  Param* const sets[] = {chip_, sample_, fm_};
  for (Param* p : sets) {
    p[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", inst().name); },
                [this](int d) { editNameChar(inst().name, kNameLen, namePos_, d); }};
    p[kType] = {"Type",
                [this](char* o, int n) {
                  static const char* const kNames[] = {"CHIP", "SAMPLE", "FM"};
                  const int t = static_cast<int>(inst().type);
                  snprintf(o, n, "%s", kNames[t < 3 ? t : 0]);
                },
                [this](int d) {
                  const int v = clampi(static_cast<int>(inst().type) + d, 0, static_cast<int>(mt::InstrType::Count) - 1);
                  inst().type = static_cast<mt::InstrType>(v);
                }};
    p[kVol] = {"Volume", [this](char* o, int n) { snprintf(o, n, "%u", inst().vol); },
               [this](int d) { inst().vol = static_cast<uint8_t>(clampi(inst().vol + d, 0, 127)); }};
    p[kTranspose] = {"Transpose", [this](char* o, int n) { snprintf(o, n, "%+d", inst().transpose); },
                     [this](int d) { inst().transpose = static_cast<int8_t>(clampi(inst().transpose + d, -24, 24)); }};
    p[kFine] = {"Fine", [this](char* o, int n) { snprintf(o, n, "%+d ct", inst().fine); },
                [this](int d) { inst().fine = static_cast<int8_t>(clampi(inst().fine + d, -50, 50)); }};
    p[kAttack] = {"Attack", [this](char* o, int n) { envTime(inst().attack, o, n); },
                  [this](int d) { inst().attack = static_cast<uint8_t>(clampi(inst().attack + d, 0, 127)); }};
    p[kDecay] = {"Decay", [this](char* o, int n) { envTime(inst().decay, o, n); },
                 [this](int d) { inst().decay = static_cast<uint8_t>(clampi(inst().decay + d, 0, 127)); }};
    p[kSustain] = {"Sustain", [this](char* o, int n) { snprintf(o, n, "%d%%", (inst().sustain * 100 + 63) / 127); },
                   [this](int d) { inst().sustain = static_cast<uint8_t>(clampi(inst().sustain + d, 0, 127)); }};
    p[kRelease] = {"Release", [this](char* o, int n) { envTime(inst().release, o, n); },
                   [this](int d) { inst().release = static_cast<uint8_t>(clampi(inst().release + d, 0, 127)); }};
    p[kMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", inst().mono ? "MONO" : "POLY"); },
                [this](int d) { inst().mono = d > 0; }};
    p[kGlide] = {"Glide",
                 [this](char* o, int n) {
                   if (inst().glide) snprintf(o, n, "%u ms", inst().glide * 4u);
                   else snprintf(o, n, "OFF");
                 },
                 [this](int d) { inst().glide = static_cast<uint8_t>(clampi(inst().glide + d, 0, 255)); },
                 [this] { return !inst().mono; }};
  }
  chip_[kWave] = {"Wave", [this](char* o, int n) { waveName(inst().wave, o, n); },
                  [this](int d) { inst().wave = static_cast<uint8_t>(clampi(inst().wave + d, 0, mt::kWaveCount - 1)); }};
  auto notPulse = [this] { return inst().wave != static_cast<uint8_t>(mt::Wave::Pulse); };
  chip_[kDuty] = {"Duty", [this](char* o, int n) { snprintf(o, n, "%u%%", inst().duty); },
                  [this](int d) { inst().duty = static_cast<uint8_t>(clampi(inst().duty + d, 1, 99)); }, notPulse};
  chip_[kPwmRate] = {"PWM rate", [this](char* o, int n) { snprintf(o, n, "%u", inst().pwmRate); },
                     [this](int d) { inst().pwmRate = static_cast<uint8_t>(clampi(inst().pwmRate + d, 0, 127)); },
                     notPulse};
  chip_[kPwmDepth] = {"PWM depth", [this](char* o, int n) { snprintf(o, n, "%u%%", inst().pwmDepth); },
                      [this](int d) { inst().pwmDepth = static_cast<uint8_t>(clampi(inst().pwmDepth + d, 0, 49)); },
                      notPulse};
  sample_[kSample] = {"Sample",
                      [this](char* o, int n) { snprintf(o, n, "%s", inst().sample[0] ? inst().sample : "---"); },
                      [this](int d) {
                        // Bank order, "---" (none) before the first; picking one takes its root and loop.
                        mt::SampleBank& b = audio::bank();
                        const int cnt = audio::bankMounted() ? b.count() : 0;
                        const int i = clampi(bankIndex() + d, -1, cnt - 1);
                        if (i < 0) {
                          inst().sample[0] = 0;
                          return;
                        }
                        const mt::BankEntry* e = b.entry(i);
                        snprintf(inst().sample, sizeof(inst().sample), "%s", e->name);
                        inst().root = e->root > 127 ? 127 : e->root;
                        inst().loop = e->loop < static_cast<uint8_t>(mt::LoopMode::Count) ? e->loop : 0;
                      },
                      {}, [this] { return sampleMissing(); }};
  sample_[kRoot] = {"Root", [this](char* o, int n) {
                      char nn[4];
                      mt::noteName(inst().root, nn);
                      snprintf(o, n, "%s", nn);
                    },
                    [this](int d) { inst().root = static_cast<uint8_t>(clampi(inst().root + d, 0, 127)); }};
  sample_[kStart] = {"Start", [this](char* o, int n) { permille(inst().start, o, n); },
                     [this](int d) {
                       inst().start = fromPermille(clampi(toPermille(inst().start) + d, 0, toPermille(inst().end)));
                     }};
  sample_[kEnd] = {"End", [this](char* o, int n) { permille(inst().end, o, n); },
                   [this](int d) {
                     inst().end = fromPermille(clampi(toPermille(inst().end) + d, toPermille(inst().start), 1000));
                   }};
  sample_[kLoop] = {"Loop", [this](char* o, int n) {
                      static const char* const kNames[] = {"OFF", "FWD", "PING"};
                      snprintf(o, n, "%s", kNames[inst().loop < 3 ? inst().loop : 0]);
                    },
                    [this](int d) {
                      inst().loop = static_cast<uint8_t>(
                          clampi(inst().loop + d, 0, static_cast<int>(mt::LoopMode::Count) - 1));
                    }};
  auto noLoop = [this] { return inst().loop == static_cast<uint8_t>(mt::LoopMode::Off); };
  sample_[kLoopStart] = {"Loop start", [this](char* o, int n) { permille(inst().loopStart, o, n); },
                         [this](int d) {
                           inst().loopStart = fromPermille(clampi(toPermille(inst().loopStart) + d, 0, 1000));
                         },
                         noLoop};
  sample_[kReverse] = {"Reverse", [this](char* o, int n) { snprintf(o, n, "%s", inst().reverse ? "ON" : "OFF"); },
                       [this](int d) { inst().reverse = d > 0; }};
  auto isTone = [this] { return inst().machine == static_cast<uint8_t>(mt::FmMachine::Tone); };
  auto drum = [this] { return !mt::fmGated(inst().machine); };
  fm_[kAttack].dim = drum;
  fm_[kDecay].dim = [] { return true; };  // the DECAY macro replaces it
  fm_[kSustain].dim = drum;
  fm_[kRelease].dim = drum;
  fm_[kMode].dim = [isTone] { return !isTone(); };
  fm_[kGlide].dim = [this, isTone] { return !inst().mono || !isTone(); };
  fm_[kMachine] = {"Machine",
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"KICK", "SNARE", "METAL", "PERC",
                                                          "TONE", "CHORD", "CLAP", "HAT"};
                     snprintf(o, n, "%s", kNames[inst().machine % 8]);
                   },
                   [this](int d) {
                     const int v = clampi(inst().machine + d, 0, static_cast<int>(mt::FmMachine::Count) - 1);
                     if (v != inst().machine) mt::fmSetMachine(inst(), static_cast<uint8_t>(v));
                   }};
  auto macroEdit = [this](int k) {
    return [this, k](int d) { inst().macro[k] = static_cast<uint8_t>(clampi(inst().macro[k] + d, 0, 127)); };
  };
  auto macroNum = [this](int k) {
    return [this, k](char* o, int n) { snprintf(o, n, "%u", inst().macro[k]); };
  };
  fm_[kMacDecay] = {"DECAY",
                    [this](char* o, int n) {
                      const unsigned ms = mt::fmDecayMs(inst().macro[mt::kMacDec]);
                      if (ms < 1000) snprintf(o, n, "%u ms", ms);
                      else snprintf(o, n, "%u.%u s", ms / 1000, ms % 1000 / 100);
                    },
                    macroEdit(mt::kMacDec)};
  fm_[kMacColor] = {"COLOR", macroNum(mt::kMacCol), macroEdit(mt::kMacCol)};
  fm_[kMacShape] = {"SHAPE",
                    [this](char* o, int n) {
                      const uint8_t v = inst().macro[mt::kMacShp];
                      if (inst().machine == static_cast<uint8_t>(mt::FmMachine::Chord))
                        snprintf(o, n, "%u %s", v, mt::fmChordName(v));
                      else snprintf(o, n, "%u", v);
                    },
                    macroEdit(mt::kMacShp)};
  fm_[kMacSweep] = {"SWEEP", macroNum(mt::kMacSwp), macroEdit(mt::kMacSwp)};
  fm_[kMacContour] = {"CONTOUR", macroNum(mt::kMacCon), macroEdit(mt::kMacCon)};
  auto noLfo = [this] { return inst().lfoDepth == 0; };
  fm_[kLfoWave] = {"LFO wave",
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"SINE", "TRI", "SAW", "SQR", "RND"};
                     snprintf(o, n, "%s", kNames[inst().lfoWave % 5]);
                   },
                   [this](int d) {
                     inst().lfoWave = static_cast<uint8_t>(
                         clampi(inst().lfoWave + d, 0, static_cast<int>(mt::LfoWave::Count) - 1));
                   },
                   noLfo};
  fm_[kLfoRate] = {"LFO rate", [this](char* o, int n) { snprintf(o, n, "%.2f Hz", mt::lfoHz(inst().lfoRate)); },
                   [this](int d) { inst().lfoRate = static_cast<uint8_t>(clampi(inst().lfoRate + d, 0, 127)); },
                   noLfo};
  fm_[kLfoDepth] = {"LFO depth", [this](char* o, int n) { snprintf(o, n, "%+d", inst().lfoDepth); },
                    [this](int d) { inst().lfoDepth = static_cast<int8_t>(clampi(inst().lfoDepth + d, -64, 63)); }};
  fm_[kLfoDest] = {"LFO dest",
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"PITCH", "DECAY", "COLOR", "SHAPE",
                                                          "SWEEP", "CONTOUR", "VOL"};
                     snprintf(o, n, "%s", kNames[inst().lfoDest % 7]);
                   },
                   [this](int d) {
                     inst().lfoDest = static_cast<uint8_t>(
                         clampi(inst().lfoDest + d, 0, static_cast<int>(mt::LfoDest::Count) - 1));
                   },
                   noLfo};
  list_.setParams(chip_, kChipRows);
  list_.setVisibleRows(kVisibleRows);
  list_.setOnEdit([this] {
    app_.markDirty();
    syncParams();
  });
}

mt::Instrument& InstScreen::inst() { return app_.project().instruments[instr_]; }

void InstScreen::syncParams() {
  const mt::InstrType t = inst().type;
  if (t == shown_) return;
  shown_ = t;
  switch (t) {
    case mt::InstrType::Sample:
      list_.setParams(sample_, kSampleRows);
      list_.setVisibleRows(kSampleVisibleRows);
      break;
    case mt::InstrType::Fm:
      list_.setParams(fm_, kFmRows);
      list_.setVisibleRows(kVisibleRows);
      break;
    default:
      list_.setParams(chip_, kChipRows);
      list_.setVisibleRows(kVisibleRows);
      break;
  }
}

int InstScreen::bankIndex() {
  if (!audio::bankMounted() || !inst().sample[0]) return -1;
  return audio::bank().find(inst().sample);
}

bool InstScreen::sampleMissing() { return inst().sample[0] && bankIndex() < 0; }

void InstScreen::updateWave() {
  const int i = bankIndex();
  const mt::BankEntry* e = i >= 0 ? audio::bank().entry(i) : nullptr;
  const int16_t* d = e ? audio::bank().data(i) : nullptr;
  const uint32_t frames = d ? e->frames : 0;
  const uint32_t gen = audio::bank().generation();
  if (d == waveData_ && frames == waveFrames_ && gen == waveGen_) return;
  waveData_ = d;
  waveFrames_ = frames;
  waveGen_ = gen;
  if (!d || !frames) return;
  // Long samples: at most kProbe evenly spaced frames per column (flash reads are not free).
  constexpr uint32_t kProbe = 256;
  for (int x = 0; x < kScreenW; ++x) {
    const uint32_t a = static_cast<uint32_t>(static_cast<uint64_t>(frames) * x / kScreenW);
    uint32_t b = static_cast<uint32_t>(static_cast<uint64_t>(frames) * (x + 1) / kScreenW);
    if (b <= a) b = a + 1;
    const uint32_t stride = (b - a) > kProbe ? (b - a) / kProbe : 1;
    int lo = 32767, hi = -32768;
    for (uint32_t k = a; k < b && k < frames; k += stride) {
      const int v = d[k];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    waveMin_[x] = static_cast<int8_t>(lo >> 8);
    waveMax_[x] = static_cast<int8_t>(hi >> 8);
  }
}

void InstScreen::drawWave(LGFX_Sprite& s, int y) {
  s.fillRect(0, y, kScreenW, kWaveH, kBeatBg);
  updateWave();
  if (!waveData_) {
    const char* msg = inst().sample[0] ? "NOT IN BANK" : "NO SAMPLE";
    s.setTextColor(inst().sample[0] ? kRed : kDim);
    s.drawString(msg, (kScreenW - static_cast<int>(strlen(msg)) * kCharW) / 2, y + (kWaveH - kCharH) / 2);
    return;
  }
  const mt::Instrument& m = inst();
  const int xs = static_cast<int>(static_cast<uint32_t>(m.start) * kScreenW / 0xFFFF);
  int xe = static_cast<int>(static_cast<uint32_t>(m.end) * kScreenW / 0xFFFF);
  if (xe > kScreenW - 1) xe = kScreenW - 1;
  const int mid = y + kWaveH / 2;
  constexpr int kHalf = kWaveH / 2 - 1;
  for (int x = 0; x < kScreenW; ++x) {
    const int y1 = mid - waveMax_[x] * kHalf / 128;
    const int y2 = mid - waveMin_[x] * kHalf / 128;
    s.drawFastVLine(x, y1, y2 - y1 + 1, x >= xs && x <= xe ? kText : kDim);
  }
  if (m.loop != static_cast<uint8_t>(mt::LoopMode::Off)) {
    // Loop start is a fraction of start..end; reverse mirrors it (see Synth::startSample).
    const int span = xe - xs;
    int xl = xs + static_cast<int>(static_cast<uint32_t>(m.loopStart) * span / 0xFFFF);
    if (m.reverse) xl = xe - (xl - xs);
    s.drawFastVLine(xl, y, kWaveH, kYellow);
  }
  s.drawFastVLine(xs, y, kWaveH, kGreen);
  s.drawFastVLine(xe, y, kWaveH, kRed);
}

void InstScreen::fixNames() {
  mt::Project& p = app_.project();
  for (int i = 0; i < mt::kInstruments; ++i) {
    if (p.instruments[i].name[0]) continue;
    engine::lockProject();
    snprintf(p.instruments[i].name, sizeof(p.instruments[i].name), "INS%d", i + 1);
    engine::unlockProject();
  }
}

void InstScreen::leaveEdit() {
  list_.setEdit(false);
  namePos_ = 0;
  fixNames();
}

void InstScreen::onEnter() {
  leaveEdit();
  instr_ = app_.project().tracks[app_.curTrack()].instr % mt::kInstruments;
  syncParams();
}

void InstScreen::changeInstr(int d) {
  leaveEdit();
  instr_ = ((instr_ + d) % mt::kInstruments + mt::kInstruments) % mt::kInstruments;
  syncParams();
}

void InstScreen::preview() { audio::preview(static_cast<uint8_t>(instr_), kPreviewNote); }

void InstScreen::onInput(const hw::InputEvent& ev) {
  const bool wasName = nameEdit();
  if (ev.type == hw::InputType::EncLong) {
    preview();
    return;
  }
  if (ev.type == hw::InputType::EncTurn && ev.shift) {
    if (wasName) {
      namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
      return;
    }
    if (!list_.editing()) {
      changeInstr(ev.delta);
      return;
    }
  }
  if (wasName && ev.type == hw::InputType::EncTurn) {
    list_.edit(ev.delta);  // no x10 for characters
    return;
  }
  list_.onInput(ev);
  if (wasName && !nameEdit()) leaveEdit();
}

void InstScreen::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    if (ev.x < kLeftX1) changeInstr(-1);
    else if (ev.x >= kRightX0 && ev.x < kRightX1) changeInstr(1);
    else if (ev.x >= kPrevX0) preview();
    return;
  }
  // Tap on a character of the name being edited moves the name cursor.
  if (ev.type == TouchType::Tap && nameEdit() && list_.rowAt(ev.y) == kName && ev.x >= ParamList::kValueX &&
      ev.x < ParamList::kValueX + kNameLen * kCharW) {
    namePos_ = (ev.x - ParamList::kValueX) / kCharW;
    return;
  }
  const bool wasName = nameEdit();
  list_.onTouch(ev);
  if (wasName && !nameEdit()) leaveEdit();
}

void InstScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  char buf[24];
  const int cy = y0 + kHeaderH / 2;
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.fillTriangle(24, cy - 2, 36, cy - 9, 36, cy + 5, kCursor);
  s.fillTriangle(kRightX0 + 36, cy - 2, kRightX0 + 24, cy - 9, kRightX0 + 24, cy + 5, kCursor);
  snprintf(buf, sizeof(buf), "INS%d %s", instr_ + 1, inst().name);
  s.setTextColor(kText);
  const int mid = (kLeftX1 + kRightX0) / 2;
  s.drawString(buf, mid - static_cast<int>(strlen(buf)) * kCharW / 2, ty);
  s.fillRect(kPrevX0, y0 + 2, kScreenW - kPrevX0 - 8, kHeaderH - 8, kPlayBg);
  s.setTextColor(kCursor);
  s.drawString("PREVIEW", kPrevX0 + (kScreenW - kPrevX0 - 8 - 7 * kCharW) / 2, ty);

  int ly = y0 + kHeaderH;
  if (shown_ == mt::InstrType::Sample) {
    drawWave(s, ly);
    ly += kWaveH + kWaveGap;
  }
  list_.draw(s, ly);
  if (nameEdit()) {
    const int uy = list_.rowY(kName) + (ParamList::kRowH + kCharH) / 2;
    s.fillRect(ParamList::kValueX + namePos_ * kCharW, uy, kCharW, 2, kEditCursor);
  }
}

}  // namespace ui
