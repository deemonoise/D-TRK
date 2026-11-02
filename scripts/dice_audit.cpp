// Host-only audit and audition tool. Build after a native PlatformIO test run:
// clang++ -O2 -std=c++17 -Ilib/core/src scripts/dice_audit.cpp \
//   .pio/build/native/lib104/libcore.a -o /tmp/dice-audit
// /tmp/dice-audit --output /tmp/dice-audition --matrix --render
// Optional --library PATH uses an existing DML1 for MIDI renders/audit.
// Repeat --only GENRE/STYLE to restrict audition files; omit it for the full matrix.
// --arrange audits six sections and renders a continuous SONG with a single Synth.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "dice.h"
#include "project_io.h"
#include "render.h"
#include "templates.h"
#include "voice_cost.h"

using namespace mt;
namespace {
// Deliberately busy, independent MIDI tracks exercise adaptation, not musical
// quality of a real library. Their identity is explicit in the audit CSV.
struct Fixture : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& ph) const override {
    ph = DicePhrase();
    std::snprintf(ph.name, sizeof(ph.name), "synthetic audit fixture");
    ph.scale = static_cast<uint8_t>(ScaleType::Minor);
    auto add = [&](int tick, int duration, int note, int vel, int lane = 255) {
      ph.events[ph.count++] = {static_cast<uint16_t>(tick), static_cast<uint16_t>(duration),
                             static_cast<uint8_t>(note), static_cast<uint8_t>(vel),
                             static_cast<uint8_t>(lane)};
    };
    for (int i = 0; i < 16; ++i) {
      const int tick = i * 24;
      if (role == DiceRole::Drums) {
        add(tick, 6, 42, 65, kDiceKitLane[kDiceHat]);
        if (i % 4 == 0) add(tick, 6, 36, 120, kDiceKitLane[kDiceKick]);
        if (i % 8 == 4) add(tick, 6, 38, 108, kDiceKitLane[kDiceSnare]);
      } else if (role == DiceRole::Chords) {
        if (i % 8 == 0) for (int n : {60, 63, 67}) add(tick, 180, n, 85);
      } else if (role == DiceRole::Bass) {
        if (i % 2) add(tick + 2, 18, i % 4 == 1 ? 36 : 43, 100);
      } else if (role == DiceRole::Fx) {
        if (i == 0) add(tick, 48, 60, 70);
      } else {
        static const int notes[] = {60, 63, 67, 70, 72, 75, 79, 82};
        add(tick + (i % 2 ? 2 : 0), 18, notes[i % 8], 80 + i % 4 * 8);
      }
    }
    return true;
  }
};

struct FileSink : ByteSink {
  explicit FileSink(const std::filesystem::path& path) : file(path, std::ios::binary) {}
  bool write(const void* bytes, size_t n) override {
    file.write(static_cast<const char*>(bytes), n);
    return static_cast<bool>(file);
  }
  std::ofstream file;
};

void put16(std::ostream& out, uint16_t v) {
  const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8)};
  out.write(b, sizeof(b));
}
void put32(std::ostream& out, uint32_t v) { put16(out, v); put16(out, v >> 16); }
void wavHeader(std::ostream& out, uint32_t frames) {
  out.write("RIFF", 4); put32(out, 36 + frames * 4); out.write("WAVEfmt ", 8);
  put32(out, 16); put16(out, 1); put16(out, 2); put32(out, kSynthRate);
  put32(out, kSynthRate * 4); put16(out, 4); put16(out, 16);
  out.write("data", 4); put32(out, frames * 4);
}
std::string slug(const char* text) {
  std::string out;
  for (; *text; ++text) out += (*text >= 'A' && *text <= 'Z') || (*text >= '0' && *text <= '9') ? *text : '_';
  return out;
}
int count(const Pattern& pat, int role) {
  int n = 0;
  for (int i = 0; i < pat.length; ++i) n += pat.steps[role][i].hasNote();
  return n;
}
int kicks(const Project& p, int pat) {
  int n = 0;
  for (int i = 0; i < p.patterns[pat].length; ++i) {
    const Step& st = p.patterns[pat].steps[0][i];
    n += st.hasNote() && laneOn(st, kDiceKitLane[kDiceKick]);
  }
  return n;
}

void row(std::ostream& out, const Project& p, const DiceOpts& o, uint32_t seed,
         const DiceResult& result, const char* source) {
  out << diceGenreName(o.genre) << ',' << diceStyleName(o.genre, o.style) << ',' << source << ','
      << seed << ',' << int(o.variety) << ',' << int(o.form) << ',' << int(o.len) << ',' << p.bpm
      << ',' << int(o.density) << ',' << int(result.midiMask) << ',' << int(result.missingMask);
  const int active = dicePatternCount(o);
  for (int pat = 0; pat < kDiceMaxPats; ++pat) out << ',' << (pat < active ? kicks(p, pat) : 0);
  for (int role = 0; role < kDiceRoles; ++role) {
    const Pattern& pat = p.patterns[1];
    const auto& sound = p.instruments[p.tracks[role].instr % kInstruments];
    int lo = 128, hi = -1;
    for (int i = 0; i < pat.length; ++i) if (role && pat.steps[role][i].hasNote()) {
      const int pitch = pat.steps[role][i].note + sound.transpose;
      lo = std::min(lo, pitch); hi = std::max(hi, pitch);
    }
    out << ',' << count(pat, role) << ',' << (hi < 0 ? -1 : lo) << ',' << hi << ',' << sound.name;
  }
  for (int pat = 0; pat < kDiceMaxPats; ++pat)
    for (int role = 0; role < kDiceRoles; ++role) out << ',' << (pat < active ? count(p.patterns[pat], role) : 0);
  out << '\n';
}
bool validTails(const Project& p, int active) {
  for (int pat = 0; pat < active; ++pat)
    for (int r = 0; r < kDiceRoles; ++r)
      for (int i = p.patterns[pat].length; i < kMaxSteps; ++i)
        if (!p.patterns[pat].steps[r][i].isEmpty()) return false;
  return true;
}
bool validMusic(const Project& p, const DiceOpts& opts) {
  const auto& recipe = *diceRecipe(opts.genre, opts.style);
  const auto& profile = diceMusicProfile(opts.genre, opts.style);
  const bool arranged = opts.form == DiceForm::Arrange;
  for (int pat = 0; pat < dicePatternCount(opts); ++pat) {
    const Pattern& pt = p.patterns[pat];
    const int barSteps = std::max(1, 4 * kPpqn / ticksPerStep(pt.res));
    int first = kDiceRoles, last = -1;
    for (int role = 0; role < kDiceRoles; ++role) if (count(pt, role)) {
      first = std::min(first, role); last = role;
    }
    if (!arranged) for (int role = first; role <= last; ++role)
      if (!count(pt, role) || p.tracks[role].mute) return false;
    for (int role = 1; role < 5; ++role) {
      const bool support = !(recipe.roles & (1u << role));
      const int cap = support ? 1 : role == 3 ? profile.leadPerBar : role == 4 ? profile.arpPerBar : 0;
      for (int start = 0; start < pt.length; start += barSteps) {
        int attacks = 0;
        for (int i = start; i < std::min<int>(pt.length, start + barSteps); ++i) {
          const Step& step = pt.steps[role][i];
          if (!step.hasNote()) continue;
          ++attacks;
          if (support && (step.find(Fx::TIE) || step.find(Fx::SLD))) return false;
        }
        if (cap && attacks > cap && opts.density != DiceDensity::Busy &&
            !(arranged && pat == kArrangeBuild)) return false;
      }
    }
  }
  if (arranged) {
    if (count(p.patterns[kArrangeBreak], 1) || kicks(p, kArrangeBreak)) return false;
    if ((recipe.roles & 2) && !count(p.patterns[kArrangeDrop], 1)) return false;
    if ((recipe.roles & 1) && recipe.drums[kDiceKick] &&
        (!kicks(p, kArrangeMain) || !kicks(p, kArrangeDrop))) return false;
    const Pattern& build = p.patterns[kArrangeBuild];
    if (!opts.len && build.length * ticksPerStep(build.res) < 4 * 4 * kPpqn) return false;
    return true;
  }
  if ((recipe.roles & 1) && recipe.drums[kDiceKick]) {
    for (int pat : {0, 1, 2, 3}) {
      if ((opts.form == DiceForm::Classic && pat == kDiceBreak) ||
          (opts.form == DiceForm::Develop && pat == 3)) continue;
      if (!kicks(p, pat)) return false;
    }
  }
  return true;
}
struct AudioStats { int peak = 0; uint64_t clips = 0, frames = 0; long double energy = 0; };
bool render(Project& p, const std::filesystem::path& path, AudioStats& stats, bool sections,
            bool song, int active) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  wavHeader(out, 0);
  // Optional complete form makes transitions and the outro available for listening.
  std::vector<int> patterns = {1, 2};
  if (sections) { patterns.clear(); for (int i = 0; i < active; ++i) patterns.push_back(i); }
  if (song) patterns = {0};  // One render, one Synth: reverb, delays and locks cross real boundaries.
  for (int pat : patterns) {
    auto synth = std::make_unique<Synth>(p);
    synth->reset();
    synth->setVoiceBudget(kVoiceBudgetTeensy);
    RenderSpec spec; spec.pattern = pat; spec.tailBlocks = kSynthRate / Synth::kBlock / 4;
    if (song) {
      spec.mode = RenderSpec::Mode::Song;
      spec.maxSlots = p.songLen;
      spec.tailBlocks = 2 * kSynthRate / Synth::kBlock;
    }
    OfflineRender::Guard guard(p);
    OfflineRender audio(p, *synth, spec);
    int16_t l[Synth::kBlock], r[Synth::kBlock];
    while (audio.renderBlock(l, r)) {
      for (int i = 0; i < Synth::kBlock; ++i) {
        put16(out, static_cast<uint16_t>(l[i])); put16(out, static_cast<uint16_t>(r[i]));
        stats.energy += static_cast<long double>(l[i]) * l[i] + static_cast<long double>(r[i]) * r[i];
      }
      stats.frames += Synth::kBlock;
    }
    stats.peak = std::max(stats.peak, static_cast<int>(audio.peak()));
    stats.clips += audio.clips();
  }
  out.seekp(0); wavHeader(out, static_cast<uint32_t>(stats.frames));
  return static_cast<bool>(out);
}
void timeline(const Project& p, const std::filesystem::path& path, bool arranged) {
  std::ofstream out(path);
  out << "slot,pattern,section,start_seconds,end_seconds,mute_mask\n";
  static const char* const names[] = {"INTRO", "MAIN", "BREAK", "BUILD", "DROP", "OUTRO"};
  double seconds = 0;
  int bpm = p.bpm;
  for (int slot = 0; slot < p.songLen; ++slot) {
    if (p.songBpm[slot]) bpm = p.songBpm[slot];
    const int pattern = p.song[slot];
    const double end = seconds + static_cast<double>(passTicks(p, pattern)) * 60 / (bpm * kPpqn);
    out << slot + 1 << ',' << pattern + 1 << ',' << (arranged && pattern < 6 ? names[pattern] : "PATTERN")
        << ',' << seconds << ',' << end << ',' << p.songMute[slot] << '\n';
    seconds = end;
  }
}
}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path output = "/tmp/dice-audition", library;
  std::vector<std::string> only;
  bool matrix = false, audio = false, sections = false, arrange = false, song = false, allDensity = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--output" && i + 1 < argc) output = argv[++i];
    else if (arg == "--library" && i + 1 < argc) library = argv[++i];
    else if (arg == "--matrix") matrix = true;
    else if (arg == "--render") audio = true;
    else if (arg == "--sections") sections = true;
    else if (arg == "--arrange") arrange = true;
    else if (arg == "--song") song = true;
    else if (arg == "--all-density") allDensity = true;
    else if (arg == "--only" && i + 1 < argc) only.emplace_back(argv[++i]);
    else { std::fprintf(stderr, "Unknown/incomplete argument: %s\n", argv[i]); return 2; }
  }
  std::filesystem::create_directories(output);
  std::vector<uint8_t> bytes;
  if (!library.empty()) {
    std::ifstream in(library, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), {});
  }
  DiceMidiLibrary midi(bytes.data(), bytes.size());
  if (!library.empty() && !midi.valid()) { std::fprintf(stderr, "Invalid DML1 library\n"); return 2; }
  Fixture fixture;
  const DicePhraseSource* source = midi.valid() ? static_cast<const DicePhraseSource*>(&midi) : &fixture;
  auto project = std::make_unique<Project>(); auto snap = std::make_unique<DiceSnap>();
  std::ofstream csv(output / "audit.csv"), rendered(output / "audio.csv");
  csv << "genre,style,source,seed,variety,form,requested_length,bpm,density,midi_mask,missing_mask";
  for (int pat = 0; pat < kDiceMaxPats; ++pat) csv << ",kick_p" << pat + 1;
  for (int r = 0; r < kDiceRoles; ++r) csv << ',' << diceRoleName(static_cast<DiceRole>(r)) << "_attacks," << diceRoleName(static_cast<DiceRole>(r)) << "_low," << diceRoleName(static_cast<DiceRole>(r)) << "_high," << diceRoleName(static_cast<DiceRole>(r)) << "_preset";
  for (int pat = 0; pat < kDiceMaxPats; ++pat)
    for (int role = 0; role < kDiceRoles; ++role)
      csv << ",P" << pat + 1 << '_' << diceRoleName(static_cast<DiceRole>(role)) << "_attacks";
  csv << '\n'; rendered << "name,source,seconds,peak,clips,rms_dbfs\n";
  int cases = 0, styles = 0, failures = 0;
  for (int g = 0; g < diceGenreCount(); ++g) for (int st = 0; st < diceStyleCount(g); ++st) {
    const std::string key = std::string(diceGenreName(g)) + '/' + diceStyleName(g, st);
    if (!only.empty() && std::find(only.begin(), only.end(), key) == only.end()) continue;
    ++styles;
    const std::vector<uint32_t> seeds = matrix ? std::vector<uint32_t>{1, 777, 8765} : std::vector<uint32_t>{777};
    const std::vector<int> varieties = matrix ? std::vector<int>{0, 30, 100} : std::vector<int>{30};
    const std::vector<int> lengths = matrix ? std::vector<int>{0, 16, 32, 64, 128} : std::vector<int>{0};
    const std::vector<DiceForm> forms = arrange ? std::vector<DiceForm>{DiceForm::Arrange} :
                                               std::vector<DiceForm>{DiceForm::Classic, DiceForm::Develop};
    const std::vector<DiceDensity> densities = allDensity ?
        std::vector<DiceDensity>{DiceDensity::Sparse, DiceDensity::Normal, DiceDensity::Busy} :
        std::vector<DiceDensity>{DiceDensity::Normal};
    for (uint32_t seed : seeds) for (int variety : varieties) for (int length : lengths)
      for (DiceForm form : forms) for (DiceDensity density : densities)
        for (DiceSource src : {DiceSource::Builtin, DiceSource::Midi, DiceSource::Hybrid}) {
          templateBuild(0, *project);
          DiceOpts o; o.genre = g; o.style = st; o.seed = seed; o.root = 0;
          o.variety = variety; o.len = length; o.form = form; o.source = src;
          o.density = density;
          if (arrange || song) o.layers |= kDiceSong;
          const auto result = diceRoll(*project, o, 0, seed, *snap, 0, source);
          const char* label = src == DiceSource::Builtin ? "builtin" : midi.valid() ?
                              (src == DiceSource::Midi ? "midi_library" : "hybrid_library") :
                              (src == DiceSource::Midi ? "midi_fixture" : "hybrid_fixture");
          row(csv, *project, o, seed, result, label);
          ++cases;
          if (!validTails(*project, dicePatternCount(o))) { ++failures; std::fprintf(stderr, "Invalid pattern tail %d/%d\n", g, st); }
          if (!validMusic(*project, o)) {
            ++failures;
            if (failures <= 20) std::fprintf(stderr, "Music constraints failed g=%d st=%d seed=%u variety=%d len=%d form=%d source=%d\n",
                                            g, st, seed, variety, length, int(form), int(src));
          }
        }
    if (audio) for (DiceSource src : {DiceSource::Builtin, DiceSource::Midi}) {
      if (src == DiceSource::Midi && !midi.valid()) continue;
      templateBuild(0, *project);
      DiceOpts o; o.genre = g; o.style = st; o.root = 0; o.seed = 777; o.source = src;
      if (arrange) o.form = DiceForm::Arrange;
      if (arrange || song) o.layers |= kDiceSong;
      diceRoll(*project, o, 0, 777, *snap, 0, source);
      const std::string name = slug(diceGenreName(g)) + "_" + slug(diceStyleName(g, st)) +
                               (src == DiceSource::Builtin ? "_builtin" : "_midi");
      FileSink sink(output / (name + ".mtp"));
      if (!saveProject(*project, sink)) return 2;
      if (arrange || song) timeline(*project, output / (name + "-timeline.csv"), arrange);
      AudioStats stats;
      if (!render(*project, output / (name + ".wav"), stats, sections, arrange || song, dicePatternCount(o))) return 2;
      const double rms = stats.frames ? std::sqrt(static_cast<double>(stats.energy / (stats.frames * 2))) : 0;
      rendered << name << ',' << (src == DiceSource::Builtin ? "builtin" : "midi_library") << ','
               << static_cast<double>(stats.frames) / kSynthRate << ',' << stats.peak << ',' << stats.clips
               << ',' << (rms > 0 ? 20 * std::log10(rms / 32768) : -200) << '\n';
      rendered.flush();
      std::printf("Rendered %s peak=%d clips=%llu\n", name.c_str(), stats.peak,
                  static_cast<unsigned long long>(stats.clips)); std::fflush(stdout);
    }
  }
  std::printf("styles=%d cases=%d failures=%d\n", styles, cases, failures);
  if (!styles) { std::fprintf(stderr, "No matching styles\n"); return 2; }
  return failures ? 1 : 0;
}
