#include "ui_brick_controls.h"
#include "brick_runtime.h"
#include "recorder.h"
#include "output_gain.h"
#include "audio/audio.h"
#include "engine/engine.h"
#include "demos.h"
#include "hw/sdcard.h"
#include "hw/ble_midi.h"
#include "storage/settings.h"
#include "storage/project_memory.h"
#include "storage/storage.h"
#include "link/rec_client.h"
#include "ui/app.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unistd.h>

namespace {
using B = brick::Button;
void audioCallback(void* context, Uint8* data, int bytes) {
  auto* samples = reinterpret_cast<int16_t*>(data);
  const int frames = bytes / (2 * sizeof(int16_t));
  brick::render(samples, frames);
  static_cast<brick::OutputGain*>(context)->apply(samples, frames);
}
struct Device {
  brick::OutputGain volume;
  SDL_AudioDeviceID id = 0;
  ~Device() {
    if (id) SDL_CloseAudioDevice(id);
  }
  bool open() {
    if (!volume.open(SDL_getenv("DTRK_VOLUME_FILE"))) {
      SDL_SetError("Unable to map the playback volume control");
      return false;
    }
    SDL_AudioSpec wanted{};
    wanted.freq = audio::kRate;
    wanted.channels = 2;
    wanted.format = AUDIO_S16SYS;
    wanted.samples = 512;
    wanted.callback = audioCallback;
    wanted.userdata = &volume;
    id = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    return id != 0;
  }
};
struct Scratch {
  std::filesystem::path path;
  bool open() {
    auto pattern = (std::filesystem::temp_directory_path() / "d-trk-check-XXXXXX").string();
    if (!mkdtemp(pattern.data())) return false;
    path = pattern;
    return setenv("DTRK_DATA", pattern.c_str(), 1) == 0;
  }
  ~Scratch() {
    if (!path.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  }
};
bool checkOutputGain() {
  // Exercise the same live mapping as the playback callback, without changing system volume.
  const auto file = std::filesystem::path(SDL_getenv("DTRK_DATA")) / "gain-test.bin";
  std::ofstream writer(file, std::ios::binary);
  auto set = [&](uint32_t left, uint32_t right) {
    uint32_t pair[]{left, right};
    writer.seekp(0);
    writer.write(reinterpret_cast<const char*>(pair), sizeof(pair)).flush();
  };
  set(65536, 65536);
  brick::OutputGain gain;
  if (!writer || !gain.open(file.c_str())) return false;
  const std::array<int16_t, 4> source{20000, -20000, -32768, 32767};
  auto matches = [&](uint32_t left, uint32_t right, std::array<int16_t, 4> expected) {
    set(left, right);
    auto pcm = source;
    gain.apply(pcm.data(), pcm.size() / 2);
    return writer && pcm == expected;
  };
  return matches(65536, 65536, source) && matches(0, 0, {0, 0, 0, 0}) &&
         matches(32768, 131072, {10000, -32768, -16384, 32767});
}
bool checkHandheldInput(brick::Input& input, LGFX& lcd, ui::App& app) {
  struct Mapping {
    const char* name;
    uint8_t a, b, x, y, l1, r1, l2, r2, ignored;
  };
  static constexpr Mapping maps[] = {
      {"TRIMUI Brick Controller", 5, 4, 6, 7, 8, 9, 10, 11, 16},
      {"Anbernic RG35XX-SP Controller", 3, 4, 6, 5, 7, 8, 12, 13, 11},
  };
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    const char* name = SDL_JoystickNameForIndex(i);
    if (!name) continue;
    for (const auto& map : maps) {
      if (std::strcmp(name, map.name) != 0) continue;
      auto tapRaw = [&](uint8_t id) {
        SDL_Event event{};
        event.jbutton.which = SDL_JoystickGetDeviceInstanceID(i);
        event.jbutton.button = id;
        event.type = SDL_JOYBUTTONDOWN;
        input.event(event);
        event.type = SDL_JOYBUTTONUP;
        input.event(event);
      };
      tapRaw(map.y);
      if (!app.grid().blocksTabChange() || lcd.overlay || app.menu().isOpen()) return false;
      tapRaw(map.y);
      if (app.grid().blocksTabChange()) return false;
      tapRaw(map.l2);
      if (!app.grid().wantsHDrag()) return false;
      tapRaw(map.r2);
      if (app.grid().wantsHDrag()) return false;
      tapRaw(map.x);
      if (!app.menu().isOpen()) return false;
      SDL_Event hat{};
      hat.jhat.type = SDL_JOYHATMOTION;
      hat.jhat.which = SDL_JoystickGetDeviceInstanceID(i);
      hat.jhat.hat = 0;
      hat.jhat.value = SDL_HAT_DOWN;
      input.event(hat);
      hat.jhat.value = SDL_HAT_CENTERED;
      input.event(hat);
      tapRaw(map.b);
      if (app.menu().isOpen()) return false;
      for (int tab = 0; tab < 3; ++tab) tapRaw(map.r1);
      if (app.tab() != ui::Tab::Mix || app.curTrack() != 0) return false;
      tapRaw(map.x);
      tapRaw(map.a);
      tapRaw(map.ignored);
      if (app.curTrack() != 0) return false;
      tapRaw(map.r2);
      if (app.curTrack() != 1) return false;
      tapRaw(map.l2);
      if (app.curTrack() != 0) return false;
      for (int tab = 0; tab < 3; ++tab) tapRaw(map.l1);
      if (app.tab() != ui::Tab::Grid) return false;
      std::printf("%s: raw buttons, hat, views, menu and mixer tracks OK\n", name);
      return true;
    }
  }
  return true;
}
bool verify(brick::Controls& controls, ui::App& app, Device& device, LGFX& lcd) {
  auto fail = [](const char* phase) {
    SDL_Log("SELF-TEST failed: %s", phase);
    return false;
  };
  auto& p = app.project();
  auto tap = [&](B b) {
    controls.press(b, true);
    controls.press(b, false);
  };
  auto producesAudio = []() {
    std::array<int16_t, 2048> frames{};
    brick::render(frames.data(), frames.size() / 2);
    return std::any_of(frames.begin(), frames.end(), [](int16_t s) { return s != 0; });
  };
  // Listen to pads in isolation, so note-entry previews cannot mask a silent pad.
  app.setPreview(false);
  for (int i = 0; i < 16; ++i) tap(B::A);
  auto& steps = p.patterns[0].steps[0];
  for (int i = 0; i < 16; ++i)
    if (steps[i].note != 60) {
      std::fprintf(stderr, "Step %d not filled\n", i);
      return false;
    }
  controls.press(B::A, true);
  tap(B::Up);
  controls.press(B::A, false);
  const auto edited = steps[0].note;
  if (edited <= 60 || steps[1].note != 60) return fail("A+Up");
  controls.press(B::Select, true);
  controls.press(B::A, true);
  if (!steps[0].isEmpty()) return fail("Select+A clears on press");
  controls.press(B::Select, false);  // Releasing the modifier first must not reinsert or advance.
  controls.press(B::A, false);
  if (!steps[0].isEmpty() || steps[1].note != 60) return fail("clear release guard");
  controls.press(B::Select, true);
  controls.press(B::B, true);
  controls.press(B::Select, false);
  controls.press(B::B, false);
  if (steps[0].note != edited || steps[1].note != 60) return fail("Select+B restores cleared cell");
  tap(B::Y);
  if (!app.grid().blocksTabChange() || lcd.overlay || app.menu().isOpen()) return fail("Y enters GRID edit");
  controls.press(B::Select, true);
  tap(B::Y);
  controls.press(B::Select, false);
  if (app.grid().blocksTabChange() || steps[0].note != edited) return fail("Select+Y cancels GRID edit");
  tap(B::L2);  // Overview -> Automation.
  if (!app.grid().wantsHDrag()) return fail("L2 wraps GRID to Automation");
  tap(B::R2);  // Automation -> Overview.
  if (app.grid().wantsHDrag()) return fail("R2 wraps GRID to Overview");
  for (int view = 0; view < 3; ++view) tap(B::R2);
  if (!app.grid().wantsHDrag()) return fail("R2 cycles through all GRID views");
  tap(B::X);
  tap(B::L2);
  tap(B::R2);
  if (!app.menu().isOpen() || !app.grid().wantsHDrag()) return fail("view keys leave the GRID menu open");
  tap(B::B);
  tap(B::R2);  // Automation -> Overview.
  tap(B::X);
  tap(B::Up);  // Grid settings is the last item.
  tap(B::Y);
  if (lcd.overlay || !app.menu().isOpen()) return fail("GRID X -> Grid settings");
  tap(B::B);
  if (!app.menu().isOpen()) return fail("return to GRID menu");
  tap(B::B);
  if (app.menu().isOpen()) return fail("close GRID menu");
  controls.press(B::A, true);
  tap(B::Down);
  controls.press(B::A, false);
  if (steps[0].note != 60) return fail("cursor after GRID menu");
  tap(B::R2);  // GRID Detail.
  tap(B::Right);  // NOTE -> VEL.
  auto& velocity = steps[0].vel;
  const int startVelocity = velocity;
  controls.press(B::A, true);
  tap(B::Right);
  if (velocity != startVelocity + 1) return fail("GRID velocity RIGHT +1");
  tap(B::Up);
  if (velocity != startVelocity + 11) return fail("GRID velocity UP +10");
  tap(B::Left);
  if (velocity != startVelocity + 10) return fail("GRID velocity LEFT -1");
  tap(B::Down);
  controls.press(B::A, false);
  if (velocity != startVelocity) return fail("GRID velocity DOWN -10");
  tap(B::L2);  // GRID Overview.
  controls.press(B::Select, true);
  controls.press(B::Start, true);
  controls.press(B::Select, false);
  controls.press(B::Start, false);
  if (engine::status().playing) return fail("Select+Start leaves transport alone");
  tap(B::Start);
  if (!engine::status().playing || !producesAudio()) return fail("transport audio");
  app.tick();
  controls.press(B::L2, true);
  controls.press(B::Start, true);
  if (!engine::status().fill) return fail("L2+Start engages Fill");
  controls.press(B::Start, false);
  controls.press(B::L2, false);
  if (engine::status().fill || !engine::status().playing || app.grid().wantsHDrag())
    return fail("Fill release preserves GRID and transport");
  tap(B::Start);
  std::array<mt::Step, 16> expected;
  std::copy_n(steps, expected.size(), expected.begin());
  if (storage::save(p, "BRICKCHECK") != storage::Result::Ok) return fail("save");
  std::fill_n(steps, expected.size(), mt::Step{});
  if (storage::load(p, "BRICKCHECK") != storage::Result::Ok ||
      std::memcmp(expected.data(), steps, sizeof expected) != 0)
    return fail("load");
  app.projectReplaced();
  app.tick();
  tap(B::Start);
  if (!engine::status().playing || !producesAudio()) return fail("reloaded pattern audio");
  const auto before = engine::nowUs();
  SDL_PauseAudioDevice(device.id, 0);
  tap(B::R1);
  app.tick();
  SDL_Delay(120);
  tap(B::L1);
  app.tick();
  SDL_Delay(120);
  SDL_PauseAudioDevice(device.id, 1);
  if (engine::nowUs() == before || !engine::status().playing || app.tab() != ui::Tab::Grid)
    return fail("SDL output callback during navigation");
  std::printf("SDL %s output advances while changing screens\n", SDL_GetCurrentAudioDriver());
  engine::post(engine::Cmd::Stop);
  app.tick();
  auto switchScreen = [](brick::Controls& input, B direction) {
    input.press(direction, true);
    input.press(direction, false);
  };
  tap(B::Select);
  if (app.menu().isOpen() || app.tab() != ui::Tab::Grid) return fail("standalone Select");
  auto trackChord = [&](B key) {
    controls.press(B::Select, true);
    tap(key);
    controls.press(B::Select, false);
  };
  const int tabs = static_cast<int>(ui::Tab::Count);
  for (int i = 1; i <= tabs; ++i) {
    switchScreen(controls, B::R1);
    if (app.tab() != static_cast<ui::Tab>(i % tabs)) return fail("next screen");
    if (app.tab() == ui::Tab::Track || app.tab() == ui::Tab::Inst || app.tab() == ui::Tab::Grid) {
      trackChord(B::R2);
      if (app.curTrack() != 1) return fail("Select+R2 track");
      trackChord(B::L2);
      if (app.curTrack() != 0) return fail("Select+L2 track");
    }
    if (app.tab() == ui::Tab::Track) {
      // TRACK has pages: using L2 for Fill must leave the MAIN/Name row active.
      tap(B::Start);
      app.tick();
      controls.press(B::L2, true);
      controls.press(B::Start, true);
      if (!engine::status().fill) return fail("TRACK L2+Start engages Fill");
      controls.press(B::L2, false);
      controls.press(B::Start, false);
      tap(B::Start);
      auto& track = p.tracks[0];
      const std::string label = track.name;
      if (label.size() < 2) return fail("TRACK name fixture");
      tap(B::Y);
      tap(B::Right);
      tap(B::Up);
      if (track.name[0] != label[0] || track.name[1] == label[1]) return fail("Y opens TRACK Name edit");
      tap(B::Down);
      tap(B::Y);
      if (track.name != label) return fail("name letter restores");
      for (int row = 0; row < 3; ++row) tap(B::Down);  // Name -> Volume.
      const int volume = track.vol;
      tap(B::A);
      tap(B::Left);
      if (track.vol != volume - 1 || app.shift()) return fail("tap edit LEFT -1 without Shift");
      tap(B::Up);
      if (track.vol != volume + 9) return fail("tap edit UP +10");
      tap(B::Down);
      tap(B::Right);
      tap(B::A);
      if (track.vol != volume) return fail("tap edit DOWN -10, RIGHT +1");
      controls.press(B::A, true);
      tap(B::Left);
      if (track.vol != volume - 1) return fail("hold edit LEFT -1");
      tap(B::Up);
      if (track.vol != volume + 9) return fail("hold edit UP +10");
      tap(B::Down);
      tap(B::Right);
      controls.press(B::A, false);
      if (track.vol != volume) return fail("hold edit DOWN -10, RIGHT +1");
    }
    if (app.tab() == ui::Tab::Mix) {
      const int master = p.masterVol;
      const int fade = p.xfade;
      const auto first = p.tracks[0];
      tap(B::Down);
      tap(B::Right);
      if (p.masterVol != master - 1 || p.xfade != fade + 1 ||
          p.tracks[0].pan != first.pan || p.tracks[0].vol != first.vol)
        return fail("MIX opens with direct master and crossfader");
      tap(B::Up);
      controls.press(B::Select, true);
      tap(B::Right);
      if (p.xfade != fade + 9) return fail("MAIN coarse crossfader");
      tap(B::Left);
      controls.press(B::Select, false);
      tap(B::Left);
      tap(B::A);
      tap(B::B);
      tap(B::R2);
      if (p.masterVol != master || p.xfade != fade || app.curTrack() != 0 ||
          p.tracks[0].solo != first.solo || p.tracks[0].mute != first.mute)
        return fail("MAIN input leaves track focus and mute/solo alone");
      controls.press(B::Down, true);
      SDL_Delay(360);
      controls.tick();
      if (p.masterVol != master - 2) return fail("MAIN held arrow repeat");
      tap(B::X);
      if (!app.menu().isOpen()) return fail("open MIX mode menu");
      tap(B::A);
      SDL_Delay(80);
      controls.tick();
      controls.press(B::Down, false);
      if (app.menu().isOpen() || p.masterVol != master - 2 || p.xfade != fade ||
          p.tracks[0].vol != first.vol)
        return fail("enter track controls without carried repeat");
      tap(B::X);
      tap(B::A);
      tap(B::Up);
      tap(B::Up);
      tap(B::X);
      tap(B::A);
      tap(B::L2);
      if (app.curTrack() != mt::kTracks - 1) return fail("mixer track wraps");
      tap(B::R2);
      tap(B::R2);
      if (app.curTrack() != 1) return fail("R2 selects next mixer track");
      auto& track = p.tracks[1];
      const auto original = track;
      const auto adjacent = p.tracks[0];
      tap(B::Left);
      tap(B::Down);
      if (track.pan != original.pan - 1 || track.vol != original.vol - 1) return fail("mixer pan and volume");
      tap(B::Right);
      tap(B::Up);
      tap(B::A);
      tap(B::B);
      if (track.pan != original.pan || track.vol != original.vol || track.solo == original.solo ||
          track.mute == original.mute) return fail("mixer restore, solo and mute");
      tap(B::A);
      tap(B::B);
      if (track.solo != original.solo || track.mute != original.mute ||
          p.tracks[0].pan != adjacent.pan || p.tracks[0].vol != adjacent.vol ||
          p.tracks[0].mute != adjacent.mute || p.tracks[0].solo != adjacent.solo) return fail("mixer track isolation");
      tap(B::X);
      if (!app.menu().isOpen()) return fail("mixer X menu");
      controls.press(B::B, true);
      tap(B::Up);
      if (app.tab() != ui::Tab::Mix || !app.menu().isOpen()) return fail("back plus arrow preserves the modal");
      controls.press(B::B, false);
      if (app.menu().isOpen()) return fail("back closes the mixer menu");
      controls.requestQuit();
      if (!app.menu().isOpen()) return fail("mixer quit modal");
      tap(B::R2);
      tap(B::R1);
      tap(B::Down);
      tap(B::B);
      controls.requestQuit();
      tap(B::A);  // Keep editing.
      if (app.menu().isOpen() || controls.exiting() || track.solo != original.solo || app.tab() != ui::Tab::Mix ||
          app.curTrack() != 1 || track.vol != original.vol || track.mute != original.mute)
        return fail("modal intercepts mixer edits");
      tap(B::L2);
      if (app.curTrack() != 0) return fail("L2 selects previous mixer track");
      tap(B::X);
      tap(B::A);
      tap(B::Down);
      if (p.masterVol != master - 1) return fail("X returns to MAIN");
      tap(B::Up);
      tap(B::X);
      tap(B::A);
      tap(B::R1);  // Leave from TRACKS.
      tap(B::L1);
      tap(B::Down);
      if (app.tab() != ui::Tab::Mix || p.masterVol != master - 1) return fail("MIX reopens in MAIN");
      tap(B::Up);
    }
    SDL_Delay(26);
    app.tick();
  }
  controls.press(B::L1, true);
  SDL_Delay(360);
  controls.tick();
  if (app.tab() != ui::Tab::File || app.menu().isOpen()) return fail("one screen per press");
  controls.press(B::L1, false);
  switchScreen(controls, B::R1);
  if (app.tab() != ui::Tab::Grid || app.curTrack() != 0) return fail("wrap to GRID preserves track");
  auto openFile = [&](ui::App& tracker, brick::Controls& input) {
    if (tracker.tab() == ui::Tab::File) switchScreen(input, B::R1);
    switchScreen(input, B::L1);  // GRID -> FILE
    return tracker.tab() == ui::Tab::File && !tracker.menu().isOpen();
  };
  auto openSystemRow = [&](int row) {
    if (!openFile(app, controls)) return false;
    tap(B::R2);  // PROJECTS -> SAMPLES
    tap(B::L2);  // SAMPLES -> PROJECTS
    tap(B::L2);  // PROJECTS -> SYSTEM, first row
    for (int i = 0; i < row; ++i) tap(B::Down);
    tap(B::A);
    controls.tick();
    return true;
  };
  if (!openSystemRow(6) || !lcd.overlay) return fail("FILE touch pointer");
  switchScreen(controls, B::R1);
  if (app.tab() != ui::Tab::Grid || !lcd.overlay) return fail("pointer tab switch");
  tap(B::R2);
  if (lcd.overlay) return fail("exit touch pointer");
  if (!openSystemRow(7)) return fail("FILE MIDI devices after touch pointer");
  switchScreen(controls, B::R1);
  if (app.tab() != ui::Tab::File) return fail("MIDI devices keeps FILE focus");
  tap(B::B);
  switchScreen(controls, B::R1);
  if (app.tab() != ui::Tab::Grid) return fail("B closes MIDI devices");
  auto quitFromFile = [&](ui::App& tracker, brick::Controls& input, auto& press) {
    if (!openFile(tracker, input)) return false;
    for (int i = 0; i < 6; ++i) press(B::Down);
    press(B::A);
    return true;
  };
  controls.press(B::A, true);
  tap(B::Up);
  controls.press(B::A, false);
  const auto newPitch = steps[0].note;
  if (!app.projectDirty() || newPitch == expected[0].note) return fail("unsaved edit");
  if (!quitFromFile(app, controls, tap) || !app.menu().isOpen() || controls.exiting()) return fail("unsaved exit guard");
  switchScreen(controls, B::R1);
  if (!app.menu().isOpen() || app.tab() != ui::Tab::File) return fail("quit confirmation keeps focus");
  tap(B::A);  // Keep editing is the default.
  if (app.menu().isOpen() || controls.exiting() || !app.projectDirty() || steps[0].note != newPitch)
    return fail("keep editing");
  if (!quitFromFile(app, controls, tap) || !app.menu().isOpen()) return fail("open FILE quit guard again");
  tap(B::B);
  if (app.menu().isOpen() || controls.exiting() || !app.projectDirty()) return fail("cancel FILE quit");
  controls.requestQuit();  // The desktop window-close command uses the same guard.
  if (!app.menu().isOpen() || controls.exiting()) return fail("open window close guard");
  tap(B::B);
  if (app.menu().isOpen() || controls.exiting() || !app.projectDirty()) return fail("cancel window close");
  if (!openFile(app, controls)) return fail("open FILE to save");
  tap(B::A);  // FILE -> PROJECTS -> Save
  if (app.projectDirty() || controls.exiting()) return fail("save before quit");
  auto saved = std::make_unique<mt::Project>();
  if (!storage::autoload(*saved) || saved->patterns[0].steps[0][0].note != newPitch)
    return fail("saved edit on disk");
  if (!quitFromFile(app, controls, tap) || !controls.exiting() || app.menu().isOpen()) return fail("quit saved project");

  // A confirmed exit ends the previous app session. Reopen the saved project for the discard case.
  auto reopened = std::make_unique<ui::App>();
  if (!storage::autoload(p)) return fail("reopen saved project");
  reopened->begin(&lcd, &p);
  brick::Controls discard(*reopened, lcd);
  auto press = [&](B key) {
    discard.press(key, true);
    discard.press(key, false);
  };
  discard.press(B::A, true);
  press(B::Up);
  discard.press(B::A, false);
  if (!reopened->projectDirty() || steps[0].note == newPitch)
    return fail("edit before discard");
  if (!quitFromFile(*reopened, discard, press) || !reopened->menu().isOpen() || discard.exiting())
    return fail("open FILE discard guard");
  press(B::Down);
  press(B::A);
  if (!discard.exiting() || !storage::autoload(*saved) || saved->patterns[0].steps[0][0].note != newPitch)
    return fail("discard preserved saved file");
  std::puts("FILE Quit tracker: keep editing, cancel, save, clean exit and explicit discard OK");
  std::puts("Brick UI: numeric and name axes, GRID clear/undo, 16 steps, transport, save/load, L1/R1 screens, L2/R2 pages/views/tracks, Y encoder click, MIX pan/volume/solo/mute and modal input OK");
  return true;
}
int run(bool test, bool fullscreen, bool headless) {
  Scratch scratch;
  if (test && !scratch.open()) return 1;
  LGFX lcd;
  if (!lcd.begin(fullscreen, headless)) {
    SDL_Log("Display: %s", SDL_GetError());
    return 1;
  }
  auto project = std::make_unique<mt::Project>();
  auto app = std::make_unique<ui::App>();
  if (!storage::reserveProjectMemory() || !hw::sdBegin()) {
    SDL_Log("Cannot open tracker data");
    return 1;
  }
  bool backup = false;
  storage::Result error = storage::Result::Ok;
  const bool loaded = !test && storage::autoload(*project, &backup, &error);
  if (!loaded) {
    if (test)
      project->reset();
    else
      mt::demoBuild(0, *project);
  }
  audio::begin(project.get());
  engine::begin(project.get());
  app->begin(&lcd, project.get());
  engine::post(engine::Cmd::SongEdit, static_cast<uint16_t>(mt::SongOp::Edit));
  if (loaded) {
    int missing = 0;
    bool folderError = storage::pullSamples(*project, &missing, ui::App::syncProgress, app.get()) ==
                       storage::Result::SamplesNotSaved;
    if (missing || backup || folderError)
      app->loadedToast(backup ? "LOADED BACKUP" : nullptr, missing, folderError);
  } else if (error != storage::Result::Ok)
    app->toast(storage::resultText(error));
  hw::ble::setClock(storage::loadBleClock());
  if (hw::ble::wanted()) hw::ble::begin();
  brick::Controls controls(*app, lcd);
  brick::Input input(controls);
  Device device;
  if (!device.open()) {
    SDL_Log("Audio output: %s", SDL_GetError());
    return 1;
  }
  if (test) {
    if (!checkOutputGain()) {
      SDL_Log("SELF-TEST failed: live playback gain");
      return 1;
    }
    std::puts("Live stereo playback gain: unity, attenuation, mute and boost saturation OK");
    if (!checkHandheldInput(input, lcd, *app)) {
      SDL_Log("SELF-TEST failed: handheld raw controller mapping");
      return 1;
    }
    return verify(controls, *app, device, lcd) ? 0 : 1;
  }
  SDL_PauseAudioDevice(device.id, 0);
  while (!controls.exiting()) {
    SDL_Event e;
    while (!controls.exiting() && SDL_PollEvent(&e)) {
      input.event(e);
      switch (e.type) {
        case SDL_QUIT:
          controls.requestQuit();
          break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) controls.reset();
          if (e.window.event == SDL_WINDOWEVENT_EXPOSED) app->invalidate();
          break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
          if (e.button.button == SDL_BUTTON_LEFT) {
            lcd.touch(e.button.x, e.button.y, e.type == SDL_MOUSEBUTTONDOWN);
            app->tick();
          }
          break;
        case SDL_MOUSEMOTION:
          lcd.touch(e.motion.x, e.motion.y, e.motion.state & SDL_BUTTON_LMASK);
          break;
      }
    }
    controls.tick();
    app->tick();
    SDL_Delay(2);
  }
  controls.reset();
  if (brick::recording()) {
    mt::link::RecStopRep rep{};
    if (audio::recStop(rep) != audio::RecResult::Ok) SDL_Log("Saving recording failed");
  } else
    audio::recCancel();
  hw::ble::end();
  return 0;
}
}  // namespace
int main(int argc, char** argv) {
  bool test = false, fullscreen = true;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--self-test")) {
      test = true;
      fullscreen = false;
    } else if (!std::strcmp(argv[i], "--windowed"))
      fullscreen = false;
    else {
      std::fprintf(stderr, "Usage: %s [--windowed] [--self-test]\n", argv[0]);
      return 2;
    }
  }
  bool headless = test && !SDL_getenv("SDL_VIDEODRIVER");
  if (test) {
    SDL_setenv("DTRK_MIDI_OUT", "/dev/null", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
  }
  Uint32 subsystems = SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER;
  if (!headless) subsystems |= SDL_INIT_VIDEO;
  if (SDL_Init(subsystems) != 0) {
    SDL_Log("%s", SDL_GetError());
    return 1;
  }
  int result = run(test, fullscreen, headless);
  SDL_Quit();
  return result;
}
