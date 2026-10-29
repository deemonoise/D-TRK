#include "midi_dialog.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "engine/engine.h"
#include "midi_router.h"
#include "note_name.h"
#include "page_bar.h"
#include "storage/settings.h"

namespace ui {
namespace {

constexpr const char* kPageNames[] = {"DEVICES", "PADS", "KNOBS"};
constexpr int kSteps = static_cast<int>(mt::PadMode::Count);
constexpr int kNameCol = 16, kRoleCol = 16 + 25 * kCharW, kStateCol = kRoleCol + 8 * kCharW;  // 16, 216, 280
constexpr int kChCol = 16 + 8 * kCharW, kNoteCol = kChCol + 8 * kCharW;  // PADS: 80, 144
constexpr uint8_t kPadStartBase = 36;  // a cleared row's edit starts at CH 1, C-2
constexpr int kPadSpan = 16;            // a bank's notes: base .. base + 15
// KNOBS columns: CC 16, CH 64, TRK 104, screen 152, label 200, mode right-aligned.
constexpr int kKCcCol = 16, kKChCol = kKCcCol + 6 * kCharW, kKTrkCol = kKChCol + 5 * kCharW,
              kKScrCol = kKTrkCol + 6 * kCharW, kKLabelCol = kKScrCol + 6 * kCharW,
              kKModeCol = kScreenW - 16 - 4 * kCharW;
constexpr const char* kKnobModes[] = {"ABS", "REL1", "REL2"};

const char* knobModeName(uint8_t rel) { return rel == 1 || rel == 2 ? kKnobModes[rel] : kKnobModes[0]; }

using hw::ble::Slot;
using hw::ble::State;

// "9 NOTE 36 100", "0 CC 21 64": the channel as sent (0..15), the type, its data.
void fmtMsg(const mt::MidiMsg& m, char* out, int n) {
  const unsigned ch = (m.status & 0x0F) + 1;
  switch (m.status & 0xF0) {
    case 0x80: snprintf(out, n, "%u NOTE OFF %u %u", ch, m.d1, m.d2); break;
    case 0x90: snprintf(out, n, "%u NOTE %u %u", ch, m.d1, m.d2); break;
    case 0xA0: snprintf(out, n, "%u POLY AT %u %u", ch, m.d1, m.d2); break;
    case 0xB0: snprintf(out, n, "%u CC %u %u", ch, m.d1, m.d2); break;
    case 0xC0: snprintf(out, n, "%u PC %u", ch, m.d1); break;
    case 0xD0: snprintf(out, n, "%u AT %u", ch, m.d1); break;
    case 0xE0: snprintf(out, n, "%u BEND %d", ch, ((m.d2 << 7) | m.d1) - 8192); break;
    default: snprintf(out, n, "%02X %u %u", m.status, m.d1, m.d2); break;
  }
}

const char* roleName(mt::DevRole r) {
  switch (r) {
    case mt::DevRole::In: return "IN";
    case mt::DevRole::Out: return "OUT";
    default: return "IN+OUT";
  }
}

// "FM-1 OFF", "FM-1, MAC OFF" for the devices the one-IN / one-OUT rule switched off; nothing when none.
void toastOff(App& app, const mt::MidiDevices& d, uint8_t offMask) {
  if (!offMask) return;
  char msg[64] = "";
  for (int i = 0; i < d.count(); ++i) {
    if (!(offMask & (1u << i))) continue;
    if (msg[0]) strlcat(msg, ", ", sizeof(msg));
    strlcat(msg, d.at(i).name, sizeof(msg));
  }
  strlcat(msg, " OFF", sizeof(msg));
  app.toast(msg);
}

// An on device: CONNECTED once every slot it serves is up (one link for IN+OUT), else SEARCHING;
// BT OFF while the stack is down (the Wi-Fi dialog took it).
const char* devState(const mt::MidiDevice& d, uint16_t& color) {
  if (!d.on) {
    color = kDim;
    return "OFF";
  }
  const uint8_t role = static_cast<uint8_t>(d.role);
  bool up = true;
  if (role & static_cast<uint8_t>(mt::DevRole::In)) up = up && hw::ble::state(Slot::In) == State::Connected;
  if (role & static_cast<uint8_t>(mt::DevRole::Out)) up = up && hw::ble::state(Slot::Out) == State::Connected;
  if (up) {
    color = kCursor;
    return "CONNECTED";
  }
  color = kText;
  return hw::ble::running() ? "SEARCHING" : "BT OFF";
}

}  // namespace

void MidiDialog::open() {
  open_ = true;
  page_ = kPgDevices;
  bar_ = true;
  edit_ = kEditNone;
  editTouched_ = false;
  const int n = app_.devices().count();
  if (dev_ >= n) dev_ = n > 0 ? n - 1 : 0;
  deviceRow_ = dev_;
  learnStep_ = -1;
  monSeq_ = app_.monitorSeq();
  stateKey_ = stateKey();
  redraw_ = true;
}

void MidiDialog::close() {
  if (!open_) return;
  stopLearn();
  leaveEdit();
  if (menuOpen_) {  // closed under one of our menus
    menuOpen_ = false;
    endIdle();
  }
  open_ = false;
  app_.invalidate();
}

uint32_t MidiDialog::stateKey() const {
  const uint32_t in = static_cast<uint32_t>(hw::ble::state(Slot::In));
  const uint32_t out = static_cast<uint32_t>(hw::ble::state(Slot::Out));
  const uint32_t k = in | out << 2 | static_cast<uint32_t>(hw::ble::running()) << 4 |
                     static_cast<uint32_t>(learnStep_ + 1) << 5 | ((hw::ble::dropped() + hw::ble::droppedOut()) & 0xFFFFFF) << 8;
  return k + app_.devNamesGen() * 0x9E3779B1u + (page_ == kPgKnobs ? knobsKey() * 0x85EBCA6Bu : 0);
}

void MidiDialog::poll() {
  if (!open_) return;
  learnPoll();
  if (page_ == kPgKnobs) knobFix();  // a load / new under the page
  if (menuOpen_ && !app_.menu().isOpen()) {
    menuOpen_ = false;
    endIdle();
    redraw_ = true;
  }
  const uint32_t k = stateKey();
  if (k != stateKey_ || app_.monitorSeq() != monSeq_) {
    stateKey_ = k;
    redraw_ = true;
  }
}

bool MidiDialog::wantsRedraw() {
  const bool r = redraw_;
  redraw_ = false;
  return r;
}

// ---- learn (the PADS page arms it, D5; nothing on DEVICES does) ----

// The router disarms once the selected bank is learned (App saves it and toasts). IN not
// connected: learn ends. Only a drop and reconnect between two polls (pollMidi saw a new connection and
// reset the router, IN never looked down here) arms the same step again.
void MidiDialog::learnPoll() {
  if (learnStep_ < 0) return;
  const bool reconnected = app_.midiConnects() != learnConns_;
  if (app_.midiRouter().learning() != learnStep_) {
    if (reconnected) armStep(learnStep_);
    else stopLearn();
  } else if (!reconnected && hw::ble::state(Slot::In) != State::Connected) {
    stopLearn();
    app_.toast("LEARN: IN LOST");
  }
}

// Learn writes into the active IN's table (App saves it): only for dev_ when it is that, connected.
void MidiDialog::startLearn() {
  if (!padDevIsIn() || hw::ble::state(Slot::In) != State::Connected) {
    app_.toast("CONNECT AS IN FIRST");
    return;
  }
  leaveEdit();
  if (padRow_ < kSteps - 1) armStep(padRow_ + 1);
}

void MidiDialog::stopLearn() {
  if (learnStep_ < 0) return;
  app_.midiRouter().disarm();
  learnStep_ = -1;
  redraw_ = true;
}

void MidiDialog::armStep(int step) {
  redraw_ = true;
  if (step >= kSteps) {
    app_.midiRouter().disarm();
    learnStep_ = -1;
    app_.toast("PADS LEARNED");
    return;
  }
  learnStep_ = step;
  learnConns_ = app_.midiConnects();
  app_.midiRouter().armLearn(static_cast<mt::PadMode>(step));
}

// ---- devices ----

// No active device: no reason to keep the stack up (its internal RAM).
void MidiDialog::endIdle() {
  if (hw::ble::wanted() || !hw::ble::running()) return;
  app_.showBusy("BLUETOOTH OFF...");  // end() may wait up to 8 s on a stuck task
  hw::ble::end();
}

// The device list (App) decides; hw::ble follows through App::devicesChanged(). That does not start
// the stack: up here when a device is active, down when none is.
void MidiDialog::changed(uint8_t offMask) {
  app_.devicesChanged();
  if (hw::ble::wanted() && !hw::ble::running()) hw::ble::begin();
  endIdle();
  toastOff(app_, app_.devices(), offMask);
  const int n = app_.devices().count();
  if (dev_ >= n) dev_ = n > 0 ? n - 1 : 0;
  if (deviceRow_ > n) deviceRow_ = n;
  redraw_ = true;
}

void MidiDialog::addDevice() {
  const mt::MidiDevices& devs = app_.devices();
  if (devs.count() >= mt::kDevMax) {
    app_.toast("6 DEVICES MAX, DELETE ONE");
    return;
  }
  app_.padsAllOff();  // no pad messages for ~3 s: nothing may keep sounding
  app_.showBusy("SCANNING...");  // ~3 s, blocking; brings the stack up when it is down
  const int n = hw::ble::scan(found_, kMaxFound);
  if (!hw::ble::running()) {
    app_.toast("BLUETOOTH FAILED");
    return;
  }
  MenuItem items[kMaxFound + 1];
  int k = 0;
  for (int i = 0; i < n; ++i) {
    if (devs.find(found_[i].addr) >= 0) continue;
    items[k++] = {found_[i].name[0] ? found_[i].name : found_[i].addr, i};
  }
  if (k == 0) {
    app_.toast(n == 0 ? "NO BLE MIDI DEVICES" : "NO NEW DEVICES");
    endIdle();  // the scan brought it up
    return;
  }
  items[k++] = {"Cancel", kMenuCancel};
  menuOpen_ = true;  // poll() ends an idle stack once the menus close (chosen or not)
  app_.menu().open("ADD DEVICE", items, k, [this](int id) {
    if (id < 0 || id >= kMaxFound) return;
    foundPick_ = id;
    const MenuItem roles[] = {{"IN", kMenuRoleIn}, {"OUT", kMenuRoleOut}, {"IN+OUT", kMenuRoleBoth}, {"Cancel", kMenuCancel}};
    app_.menu().open("USE AS", roles, 4, [this](int r) {
      if (r >= kMenuRoleIn && r <= kMenuRoleBoth) addFound(r);
    });
  });
}

void MidiDialog::addFound(int roleId) {
  if (foundPick_ < 0 || foundPick_ >= kMaxFound) return;
  const hw::ble::Found& f = found_[foundPick_];
  foundPick_ = -1;
  mt::MidiDevices& devs = app_.devices();
  const mt::DevRole role = roleId == kMenuRoleIn ? mt::DevRole::In : (roleId == kMenuRoleOut ? mt::DevRole::Out : mt::DevRole::Both);
  uint8_t off = 0;
  const int i = devs.add(f.addr, f.type, f.name[0] ? f.name : f.addr, role, off);
  if (i < 0) {
    app_.toast(devs.count() >= mt::kDevMax ? "6 DEVICES MAX, DELETE ONE" : "ALREADY IN THE LIST");
    return;
  }
  dev_ = deviceRow_ = i;
  bar_ = false;
  changed(off);
}

void MidiDialog::toggleOn() {
  mt::MidiDevices& devs = app_.devices();
  if (dev_ < 0 || dev_ >= devs.count()) return;
  uint8_t off = 0;
  devs.setOn(dev_, !devs.at(dev_).on, off);
  changed(off);
}

void MidiDialog::setRole(int roleId) {
  mt::MidiDevices& devs = app_.devices();
  if (dev_ < 0 || dev_ >= devs.count()) return;
  const mt::DevRole role = roleId == kMenuRoleIn ? mt::DevRole::In : (roleId == kMenuRoleOut ? mt::DevRole::Out : mt::DevRole::Both);
  uint8_t off = 0;
  devs.setRole(dev_, role, off);
  changed(off);
}

void MidiDialog::removeSel() {
  mt::MidiDevices& devs = app_.devices();
  if (dev_ < 0 || dev_ >= devs.count()) return;
  devs.remove(dev_);
  changed(0);
}

void MidiDialog::moreMenu() {
  const mt::MidiDevices& devs = app_.devices();
  const bool has = !bar_ && deviceRow_ >= 0 && deviceRow_ < devs.count() && dev_ >= 0 && dev_ < devs.count();
  const mt::DevRole cur = has ? devs.at(dev_).role : mt::DevRole::In;
  const MenuItem items[] = {
      {"Add device...", kMenuAdd, devs.count() < mt::kDevMax},
      {has && devs.at(dev_).on ? "Turn off" : "Turn on", kMenuOnOff, has},
      {"Role IN", kMenuRoleIn, has && cur != mt::DevRole::In},
      {"Role OUT", kMenuRoleOut, has && cur != mt::DevRole::Out},
      {"Role IN+OUT", kMenuRoleBoth, has && cur != mt::DevRole::Both},
      {"Delete", kMenuDelete, has},
      {hw::ble::clock() ? "BLE clock: ON" : "BLE clock: OFF", kMenuClock},
      {"Cancel", kMenuCancel},
  };
  menuOpen_ = true;
  app_.menu().open(has ? devs.at(dev_).name : "MORE", items, sizeof(items) / sizeof(items[0]), [this](int id) {
    switch (id) {
      case kMenuAdd: addDevice(); break;
      case kMenuOnOff: toggleOn(); break;
      case kMenuRoleIn:
      case kMenuRoleOut:
      case kMenuRoleBoth: setRole(id); break;
      case kMenuDelete: removeSel(); break;
      case kMenuClock: {
        const bool on = !hw::ble::clock();
        hw::ble::setClock(on);
        storage::saveBleClock(on);
        app_.toast(on ? "BLE CLOCK ON" : "BLE CLOCK OFF");
        break;
      }
      default: break;
    }
    redraw_ = true;
  });
}

// ---- pads ----

mt::MidiDevice* MidiDialog::padDev() {
  mt::MidiDevices& devs = app_.devices();
  return dev_ >= 0 && dev_ < devs.count() ? &devs.at(dev_) : nullptr;
}

bool MidiDialog::padDevIsIn() const {
  const mt::MidiDevices& devs = app_.devices();
  return dev_ >= 0 && dev_ < devs.count() && devs.activeIn() == dev_;
}

// The router reads the active IN's table on every message: a held pad would look its release up in
// the changed table. Before the first change of an edit, and before any change while a pad is held.
void MidiDialog::padTouch() {
  const bool first = !editTouched_;
  editTouched_ = true;
  if (padDevIsIn() && (first || app_.padsHeld())) app_.padsAllOff();
}

void MidiDialog::padTurn(int delta) {
  mt::MidiDevice* d = padDev();
  if (!d) return;
  padTouch();
  mt::PadLearn& p = d->pads;
  if (edit_ == kEditCh) {
    const int ch = p.ch[padRow_ + 1] + delta;
    p.ch[padRow_ + 1] = static_cast<uint8_t>(ch < 0 ? 0 : (ch > 15 ? 15 : ch));
  } else {
    const int b = p.base[padRow_ + 1] + delta;
    p.base[padRow_ + 1] = static_cast<uint8_t>(b < 0 ? 0 : (b > 127 ? 127 : b));
  }
}

// Saved (live already: the router reads the table); a bank sharing notes with another on the same
// channel: the router takes the first, so say which.
void MidiDialog::padLeave() {
  mt::MidiDevice* d = padDev();
  if (!d) return;
  app_.devicesSave();
  const mt::PadLearn& p = d->pads;
  const int b = p.base[padRow_ + 1];
  if (b == 0xFF) return;
  for (int j = 1; j < static_cast<int>(mt::PadMode::Count); ++j) {
    if (j == padRow_ + 1 || p.base[j] == 0xFF || p.ch[j] != p.ch[padRow_ + 1]) continue;
    const int span = padRow_ + 1 == static_cast<int>(mt::PadMode::Perf) ? 8 : kPadSpan;
    const int otherSpan = j == static_cast<int>(mt::PadMode::Perf) ? 8 : kPadSpan;
    if (b < p.base[j] + otherSpan && p.base[j] < b + span) {
      char msg[32];
      snprintf(msg, sizeof(msg), "OVERLAPS %s", mt::padModeName(static_cast<mt::PadMode>(j)));
      app_.toast(msg);
      return;
    }
  }
}

void MidiDialog::clearRow() {
  mt::MidiDevice* d = padDev();
  if (!d) return;
  stopLearn();
  leaveEdit();
  if (padDevIsIn()) {
    app_.padsForget(static_cast<mt::PadMode>(padRow_ + 1));  // pads off, cleared, saved
  } else {
    d->pads.ch[padRow_ + 1] = 0;
    d->pads.base[padRow_ + 1] = 0xFF;
    app_.devicesSave();
  }
  redraw_ = true;
}

void MidiDialog::clearAll() {
  if (!padDev()) return;
  stopLearn();
  leaveEdit();
  const MenuItem items[] = {{"Clear all pads", kMenuClearAll}, {"Cancel", kMenuCancel}};
  app_.menu().open("CLEAR ALL PADS?", items, 2, [this](int id) {
    mt::MidiDevice* d = padDev();
    if (id != kMenuClearAll || !d) return;
    if (padDevIsIn()) app_.padsAllOff();
    d->pads.clear();
    app_.devicesSave();
    redraw_ = true;
  });
}

// ---- knobs ----

int MidiDialog::knobList(int* slots) const {
  const mt::Project& p = app_.project();
  int n = 0;
  for (int i = 0; i < mt::kKnobBinds; ++i)
    if (p.knobs[i].screen) slots[n++] = i;
  return n;
}

int MidiDialog::knobSlot() const {
  int slots[mt::kKnobBinds];
  const int n = knobList(slots);
  return knobRow_ >= 0 && knobRow_ < n ? slots[knobRow_] : -1;
}

uint32_t MidiDialog::knobsKey() const {
  const mt::Project& p = app_.project();
  uint32_t h = 2166136261u;
  for (const mt::KnobBind& k : p.knobs) {
    const uint8_t b[] = {k.screen, k.ch, k.cc, k.track, k.rel, k.page};
    for (const uint8_t c : b) h = (h ^ c) * 16777619u;
    for (int i = 0; i < mt::kKnobLabel && k.label[i]; ++i) h = (h ^ static_cast<uint8_t>(k.label[i])) * 16777619u;
  }
  return h;
}

void MidiDialog::knobFix() {
  int slots[mt::kKnobBinds];
  const int n = knobList(slots);
  if (edit_ != kEditNone && (editSlot_ < 0 || !app_.project().knobs[editSlot_].screen)) {
    edit_ = kEditNone;  // its bind went (load / new): nothing to keep
    editTouched_ = false;
    editSlot_ = -1;
    redraw_ = true;
  }
  if (edit_ != kEditNone) {  // the row follows its bind
    for (int i = 0; i < n; ++i)
      if (slots[i] == editSlot_) knobRow_ = i;
  }
  if (knobRow_ >= n) knobRow_ = n > 0 ? n - 1 : 0;
  if (n == 0) bar_ = true;
  if (knobTop_ > knobRow_) knobTop_ = knobRow_;
  if (knobRow_ >= knobTop_ + kKnobRows) knobTop_ = knobRow_ - kKnobRows + 1;
  if (knobTop_ > (n > kKnobRows ? n - kKnobRows : 0)) knobTop_ = n > kKnobRows ? n - kKnobRows : 0;
}

void MidiDialog::knobTurn(int delta) {
  if (editSlot_ < 0) return;
  mt::KnobBind& k = app_.project().knobs[editSlot_];
  if (!k.screen) return;
  auto step = [delta](int v, int lo, int hi) {
    v += delta;
    return static_cast<uint8_t>(v < lo ? lo : (v > hi ? hi : v));
  };
  uint8_t* f = nullptr;
  uint8_t v = 0;
  switch (edit_) {
    case kKnobCc: f = &k.cc, v = step(k.cc, 0, 127); break;
    case kKnobCh: f = &k.ch, v = step(k.ch, 0, 15); break;
    case kKnobTrk: f = &k.track, v = step(k.track, 0, mt::kTracks - 1); break;
    case kKnobMode: f = &k.rel, v = step(k.rel > 2 ? 0 : k.rel, 0, 2); break;
    default: break;
  }
  if (!f || *f == v) return;  // at the end of the range: nothing changed
  engine::lockProject();
  *f = v;
  engine::unlockProject();
  if (edit_ != kKnobTrk) app_.midiRouter().forget(editSlot_);  // a new reference for the new ch / cc / mode
  app_.knobEdited(editSlot_);
  app_.markDirty();
  editTouched_ = true;
}

// Another bind on the same CC and channel stays allowed (stepping through takes it on the way); the
// router takes the lower slot, so say which.
void MidiDialog::knobLeave() {
  if (editSlot_ < 0) return;
  const mt::Project& p = app_.project();
  const mt::KnobBind& k = p.knobs[editSlot_];
  for (int i = 0; i < mt::kKnobBinds && k.screen; ++i) {
    const mt::KnobBind& o = p.knobs[i];
    if (i == editSlot_ || !o.screen || o.ch != k.ch || o.cc != k.cc) continue;
    char name[mt::kKnobLabel], msg[48];
    mt::knobLabel(o.label, name, sizeof(name));
    snprintf(msg, sizeof(msg), "CC%u CH%u ALSO ON %s", k.cc, k.ch + 1u, name);
    app_.toast(msg);
    break;
  }
  editSlot_ = -1;
}

void MidiDialog::knobDelete() {
  leaveEdit();
  const int slot = knobSlot();
  if (slot < 0) return;
  engine::lockProject();
  app_.project().knobs[slot] = mt::KnobBind{};
  engine::unlockProject();
  app_.midiRouter().forget(slot);
  app_.knobEdited(slot);
  app_.markDirty();
  knobFix();
  redraw_ = true;
}

void MidiDialog::knobClearAll() {
  leaveEdit();
  int slots[mt::kKnobBinds];
  if (knobList(slots) == 0) return;
  const MenuItem items[] = {{"Clear all knobs", kMenuClearAll}, {"Cancel", kMenuCancel}};
  app_.menu().open("CLEAR ALL KNOBS?", items, 2, [this](int id) {
    if (id != kMenuClearAll) return;
    engine::lockProject();
    for (mt::KnobBind& k : app_.project().knobs) k = mt::KnobBind{};
    engine::unlockProject();
    for (int i = 0; i < mt::kKnobBinds; ++i) app_.midiRouter().forget(i);
    app_.knobEdited(-1);
    app_.markDirty();
    knobFix();
    redraw_ = true;
  });
}

// ---- edits (PADS, KNOBS) ----

// A + turn on a row with no edit open: PADS CH (Shift: note), KNOBS CC (Shift: CH). A cleared pad row
// starts at CH 1 / C-2.
void MidiDialog::editOpen(int field) {
  if (page_ == kPgPads) {
    mt::MidiDevice* d = padDev();
    if (!d) return;
    stopLearn();
    if (d->pads.base[padRow_ + 1] == 0xFF) {
      padTouch();
      d->pads.ch[padRow_ + 1] = 0;
      d->pads.base[padRow_ + 1] = kPadStartBase;
    }
  } else if (page_ == kPgKnobs) {
    editSlot_ = knobSlot();
    if (editSlot_ < 0) return;
  } else {
    return;
  }
  edit_ = field;
  redraw_ = true;
}

void MidiDialog::editTurn(int delta) {
  if (edit_ == kEditNone || !delta) return;
  if (page_ == kPgPads && edit_ == kEditLength) app_.setMidiRecHold(delta > 0);
  else if (page_ == kPgPads) padTurn(delta);
  else if (page_ == kPgKnobs) knobTurn(delta);
  redraw_ = true;
}

// A load / new: an open KNOBS edit ends without its save / clash toasts (the rows are the new project's).
void MidiDialog::projectReplaced() {
  if (page_ != kPgKnobs || edit_ == kEditNone) return;  // a PADS edit is the device list's: kept
  editTouched_ = false;
  leaveEdit();
}

void MidiDialog::leaveEdit() {
  if (edit_ == kEditNone) return;
  if (editTouched_) {
    if (page_ == kPgPads && edit_ != kEditLength) padLeave();
    else if (page_ == kPgKnobs) knobLeave();
  }
  edit_ = kEditNone;
  editSlot_ = -1;
  editTouched_ = false;
  redraw_ = true;
}

// ---- frame ----

int MidiDialog::rows() const {
  const int n = app_.devices().count();
  switch (page_) {
    case kPgDevices: return n + 1;
    case kPgPads: return n > 0 ? kSteps : 0;
    default: {
      int slots[mt::kKnobBinds];
      return knobList(slots);
    }
  }
}

int& MidiDialog::rowSel() {
  switch (page_) {
    case kPgPads: return padRow_;
    case kPgKnobs: return knobRow_;
    default: return deviceRow_;
  }
}

void MidiDialog::setPage(int p) {
  stopLearn();
  leaveEdit();
  page_ = (p % kPages + kPages) % kPages;
  bar_ = true;
  if (page_ == kPgKnobs) knobFix();
  redraw_ = true;
}

void MidiDialog::contextMenu() {
  leaveEdit();
  if (page_ == kPgDevices) { moreMenu(); return; }
  if (page_ == kPgPads) {
    const bool bank = !bar_ && padDev() && padRow_ < kSteps - 1;
    const MenuItem items[] = {
        {learnStep_ >= 0 ? "Stop learning" : "Learn this bank", kMenuLearn, bank || learnStep_ >= 0},
        {"Edit channel / note", kMenuEdit, bank}, {"Unassign", kMenuClear, bank},
        {"Clear all pads", kMenuClearAll, padDev() != nullptr}, {"Cancel", kMenuCancel}};
    app_.menu().open("PADS", items, 5, [this](int id) {
      if (id == kMenuLearn) { if (learnStep_ >= 0) stopLearn(); else startLearn(); }
      else if (id == kMenuEdit) editOpen(kEditCh);
      else if (id == kMenuClear) clearRow();
      else if (id == kMenuClearAll) clearAll();
      redraw_ = true;
    });
    return;
  }
  const MenuItem items[] = {{"Edit binding", kMenuEdit, !bar_ && rows() > 0},
      {"Delete binding", kMenuKnobDelete, !bar_ && rows() > 0},
      {"Clear all knobs", kMenuKnobClearAll, rows() > 0}, {"Cancel", kMenuCancel}};
  app_.menu().open("KNOBS", items, 4, [this](int id) {
    if (id == kMenuEdit) editOpen(kKnobCc);
    else if (id == kMenuKnobDelete) knobDelete();
    else if (id == kMenuKnobClearAll) knobClearAll();
    redraw_ = true;
  });
}

// DEVICES: the MORE menu for the row. PADS: CH -> note -> out (saved); a cleared row starts at
// CH 1 / C-2. KNOBS: CC -> CH -> TRK (not for PROJ) -> MODE -> out.
void MidiDialog::rowClick() {
  switch (page_) {
    case kPgDevices:
      if (deviceRow_ == app_.devices().count()) addDevice();
      else { dev_ = deviceRow_; moreMenu(); }
      return;
    case kPgPads:
      if (padRow_ == kSteps - 1) { edit_ = edit_ == kEditLength ? kEditNone : kEditLength; redraw_ = true; return; }
      if (edit_ == kEditNone) {
        const MenuItem items[] = {{"Learn this bank", 0}, {"Edit channel / note", 1}, {"Unassign", 2}, {"Cancel", 3}};
        app_.menu().open(mt::padModeName(static_cast<mt::PadMode>(padRow_ + 1)), items, 4, [this](int id) {
          if (id == 0) startLearn();
          else if (id == 1) editOpen(kEditCh);
          else if (id == 2) clearRow();
          redraw_ = true;
        });
      }
      else if (edit_ == kEditCh) edit_ = kEditNote;
      else leaveEdit();
      break;
    default: {
      if (edit_ == kEditNone) {
        editOpen(kKnobCc);
        break;
      }
      const int slot = editSlot_;
      const bool proj = slot >= 0 && app_.project().knobs[slot].screen == mt::kKnobProj;
      if (edit_ == kKnobMode) leaveEdit();
      else edit_ = edit_ == kKnobCh && proj ? kKnobMode : edit_ + 1;
      break;
    }
  }
  redraw_ = true;
}

// The page bar is the cursor position before row 0, like ParamList.
void MidiDialog::moveSel(int delta) {
  const int n = rows();
  int& row = rowSel();
  const int pos = ((bar_ ? 0 : row + 1) + delta % (n + 1) + n + 1) % (n + 1);
  bar_ = pos == 0;
  if (!bar_) {
    row = pos - 1;
    if (page_ == kPgDevices && row < app_.devices().count()) dev_ = row;
  }
  if (page_ == kPgKnobs) knobFix();
  redraw_ = true;
}

void MidiDialog::onBack() {
  // A bank learned (or learn ended) since the last poll: that press only catches up, it skips nothing.
  const int was = learnStep_;
  learnPoll();
  if (learnStep_ != was) return;
  if (edit_ != kEditNone) {  // the edit ends, its value kept
    leaveEdit();
    return;
  }
  if (learnStep_ < 0) {
    close();
    return;
  }
  stopLearn();  // cancel only this learn; keep its previous assignment
}

void MidiDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn:
      if (edit_ != kEditNone) editTurn(ev.delta);
      else moveSel(ev.delta);
      break;
    case InputType::EditTurn:  // A + turn: the row's field, opened when none is (Shift: the second)
      if (bar_) { setPage(page_ + ev.delta); break; }
      if (page_ == kPgPads && padRow_ == kSteps - 1) { app_.setMidiRecHold(ev.delta > 0); redraw_ = true; break; }
      if (edit_ == kEditNone && !bar_ && page_ != kPgDevices && !(page_ == kPgPads && padRow_ == kSteps - 1)) {
        editOpen(page_ == kPgPads ? (ev.shift ? kEditNote : kEditCh) : (ev.shift ? kKnobCh : kKnobCc));
      }
      editTurn(ev.delta);
      break;
    case InputType::EditEnd: leaveEdit(); break;  // A released
    case InputType::EncClick:
      if (!bar_) rowClick();
      break;
    case InputType::EncLong: contextMenu(); break;
    default: break;
  }
}

void MidiDialog::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Drag) {
    if (edit_ != kEditNone) editTurn(-ev.dy / TouchTracker::kDragStep);
    else if (page_ == kPgKnobs && rows() > kKnobRows) {
      dragAcc_ += ev.dy;
      const int delta = dragAcc_ / kRowH;
      dragAcc_ -= delta * kRowH;
      knobTop_ -= delta;
      if (knobTop_ < 0) knobTop_ = 0;
      if (knobTop_ > rows() - kKnobRows) knobTop_ = rows() - kKnobRows;
      if (knobRow_ < knobTop_) knobRow_ = knobTop_;
      if (knobRow_ >= knobTop_ + kKnobRows) knobRow_ = knobTop_ + kKnobRows - 1;
      redraw_ = true;
    }
    return;
  }
  dragAcc_ = 0;
  if (ev.type != TouchType::Tap && ev.type != TouchType::LongPress) return;
  if (ev.y >= y0_ + kTitleH && ev.y < y0_ + kHeaderH) {
    if (ev.type == TouchType::LongPress) contextMenu();
    else setPage(PageBar::at(ev.x, kPages));
    return;
  }
  const int ry = y0_ + kHeaderH;
  const int shown = page_ == kPgKnobs ? (rows() - knobTop_ < kKnobRows ? rows() - knobTop_ : kKnobRows) : rows();
  if (ev.y < ry || ev.y >= ry + shown * kRowH) return;
  const int r = (ev.y - ry) / kRowH + (page_ == kPgKnobs ? knobTop_ : 0);
  int& row = rowSel();
  if (ev.type == TouchType::Tap && !bar_ && r == row) { rowClick(); return; }
  leaveEdit();
  row = r;
  bar_ = false;
  if (page_ == kPgDevices && r < app_.devices().count()) dev_ = r;
  if (ev.type == TouchType::LongPress) contextMenu();
  redraw_ = true;
}

void MidiDialog::drawDevices(LGFX_Sprite& s, int y0) {
  const mt::MidiDevices& devs = app_.devices();
  char buf[64];
  int y = y0 + kHeaderH;
  for (int i = 0; i < devs.count(); ++i, y += kRowH) {
    const mt::MidiDevice& d = devs.at(i);
    const bool sel = !bar_ && i == deviceRow_;
    const bool cur = i == dev_;  // the device whose pad banks PADS shows
    if (sel) s.fillRect(0, y, kScreenW, kRowH, kSelBg);
    else if (cur) s.fillRect(0, y, 4, kRowH, kCursor);
    const int ty = y + (kRowH - kCharH) / 2;
    uint16_t stColor = kText;
    const char* st = devState(d, stColor);
    const uint16_t text = d.on ? kText : kDim;
    s.setTextColor(sel ? kCursor : text);
    snprintf(buf, sizeof(buf), "%.24s", d.name[0] ? d.name : d.addr);
    s.drawString(buf, kNameCol, ty);
    s.setTextColor(text);
    s.drawString(roleName(d.role), kRoleCol, ty);
    s.setTextColor(stColor);
    s.drawString(st, kStateCol, ty);
  }
  const bool addSel = !bar_ && deviceRow_ == devs.count();
  if (addSel) s.fillRect(0, y, kScreenW, kRowH, kSelBg);
  s.setTextColor(addSel ? kCursor : kText);
  s.drawString("Add device...", kNameCol, y + (kRowH - kCharH) / 2);
  drawMonitor(s, y0 + kMonY, kMonLines);
}

// The newest IN messages from y, the drop counters on the first line.
void MidiDialog::drawMonitor(LGFX_Sprite& s, int y, int lines) {
  char buf[64];
  s.drawFastHLine(16, y - 2, kScreenW - 32, kSelBg);
  mt::MidiMsg mon[kMonLines];
  const int n = app_.monitor(mon, lines < kMonLines ? lines : kMonLines);
  s.setTextColor(kText);
  for (int i = 0; i < n; ++i) {
    fmtMsg(mon[i], buf, sizeof(buf));
    s.drawString(buf, 16, y + 2 + i * kMonH);
  }
  if (n == 0) {
    s.setTextColor(kDim);
    s.drawString("NO MIDI IN", 16, y + 2);
  }
  const uint32_t dropIn = hw::ble::dropped(), dropOut = hw::ble::droppedOut();
  if (dropIn || dropOut) {
    snprintf(buf, sizeof(buf), "DROP IN %lu OUT %lu", static_cast<unsigned long>(dropIn),
             static_cast<unsigned long>(dropOut));
    s.setTextColor(kRed);
    s.drawString(buf, kScreenW - 16 - static_cast<int>(strlen(buf)) * kCharW, y + 2);
  }
}

void MidiDialog::drawPads(LGFX_Sprite& s, int y0) {
  const mt::MidiDevice* d = padDev();
  if (!d) {
    s.setTextColor(kDim);
    s.drawString("ADD A DEVICE FIRST", kNameCol, y0 + kHeaderH + (kRowH - kCharH) / 2);
    drawMonitor(s, y0 + kMonY, kMonLines);
    return;
  }
  char buf[16];
  for (int i = 1; i < static_cast<int>(mt::PadMode::Count); ++i) {
    const int y = y0 + kHeaderH + (i - 1) * kRowH, ty = y + (kRowH - kCharH) / 2;
    const bool sel = !bar_ && i == padRow_ + 1, learning = i == learnStep_;
    if (sel) s.fillRect(0, y, kScreenW, kRowH, kSelBg);
    else if (i == padRow_ + 1) s.fillRect(0, y, 4, kRowH, kCursor);
    s.setTextColor(sel || learning ? kCursor : kText);
    s.drawString(mt::padModeName(static_cast<mt::PadMode>(i)), kNameCol, ty);
    const bool editing = sel && edit_ != kEditNone;
    if (d->pads.base[i] == 0xFF) {
      s.setTextColor(kDim);
      s.drawString("UNASSIGNED", kChCol, ty);
      continue;
    }
    snprintf(buf, sizeof(buf), "CH %u", d->pads.ch[i] + 1u);
    s.setTextColor(editing && edit_ == kEditCh ? kEditCursor : kText);
    s.drawString(buf, kChCol, ty);
    mt::noteName(d->pads.base[i], buf);
    s.setTextColor(editing && edit_ == kEditNote ? kEditCursor : kText);
    s.drawString(buf, kNoteCol, ty);
  }
  const int recY = y0 + kHeaderH + (kSteps - 1) * kRowH;
  if (!bar_ && padRow_ == kSteps - 1) s.fillRect(0, recY, kScreenW, kRowH, kSelBg);
  s.setTextColor(!bar_ && padRow_ == kSteps - 1 ? kCursor : kDim);
  s.drawString("REC LENGTH", 16, recY + 4);
  s.setTextColor(!bar_ && padRow_ == kSteps - 1 && edit_ == kEditLength ? kEditCursor : kText);
  s.drawString(app_.midiRecHold() ? "HOLD" : "TRACK", 240, recY + 4);
  if (learnStep_ >= 0) {
    const int ty = recY + kRowH + 2;
    char msg[64];
    snprintf(msg, sizeof(msg), "%s: HIT FIRST PAD",
             mt::padModeName(static_cast<mt::PadMode>(learnStep_)));
    s.setTextColor(kCursor);
    s.drawString(msg, 16, ty);
    s.setTextColor(kDim);
    s.drawString("B: CANCEL", kScreenW - 16 - 9 * kCharW, ty);
  }
  drawMonitor(s, y0 + kMonY, kMonLines);

}

// `CC21  CH1  TRK2  INST  CUTOFF  ABS`; PROJ rows without a track.
void MidiDialog::drawKnobs(LGFX_Sprite& s, int y0) {
  int slots[mt::kKnobBinds];
  const int n = knobList(slots);
  const mt::Project& p = app_.project();
  if (n == 0) {
    s.setTextColor(kDim);
    s.drawString("NO KNOBS - SHIFT+TURN A KNOB ON A ROW", 16, y0 + kHeaderH + (kRowH - kCharH) / 2);
  }
  char buf[24];
  for (int r = knobTop_; r < n && r < knobTop_ + kKnobRows; ++r) {
    const mt::KnobBind& k = p.knobs[slots[r]];
    const int y = y0 + kHeaderH + (r - knobTop_) * kRowH, ty = y + (kRowH - kCharH) / 2;
    const bool sel = !bar_ && r == knobRow_;
    const int ed = sel ? edit_ : kEditNone;
    if (sel) s.fillRect(0, y, kScreenW, kRowH, kSelBg);
    auto field = [&](int f, const char* text, int x) {
      s.setTextColor(ed == f ? kEditCursor : (sel ? kCursor : kText));
      s.drawString(text, x, ty);
    };
    snprintf(buf, sizeof(buf), "CC%u", k.cc);
    field(kKnobCc, buf, kKCcCol);
    snprintf(buf, sizeof(buf), "CH%u", k.ch + 1u);
    field(kKnobCh, buf, kKChCol);
    if (k.screen != mt::kKnobProj) {
      snprintf(buf, sizeof(buf), "TRK%u", k.track + 1u);
      field(kKnobTrk, buf, kKTrkCol);
    }
    s.setTextColor(kDim);
    s.drawString(mt::knobScreenName(k.screen), kKScrCol, ty);
    mt::knobLabel(k.label, buf, sizeof(buf));
    s.setTextColor(sel ? kCursor : kText);
    s.drawString(buf, kKLabelCol, ty);
    field(kKnobMode, knobModeName(k.rel), kKModeCol);
  }
  s.setTextColor(kDim);
  if (knobTop_ > 0) s.drawString("^", kScreenW - 10, y0 + kHeaderH);
  if (knobTop_ + kKnobRows < n) s.drawString("v", kScreenW - 10, y0 + kHeaderH + (kKnobRows - 1) * kRowH);
  drawMonitor(s, y0 + kKnobMonY, kKnobMonLines);
}

void MidiDialog::draw(LGFX_Sprite& s, int y0) {
  y0_ = y0;
  stateKey_ = stateKey();
  monSeq_ = app_.monitorSeq();
  s.fillRect(0, y0, kScreenW, kTitleH - 4, kBeatBg);
  s.setTextColor(kText);
  s.drawString("MIDI DEVICES", 16, y0 + 2);
  const mt::MidiDevice* d = padDev();
  if (page_ == kPgPads && d) {
    s.setTextColor(kDim);
    s.drawString(d->name, 240, y0 + 2);
  }
  PageBar::draw(s, y0 + kTitleH, kPageNames, kPages, page_);
  if (bar_) PageBar::drawFocus(s, y0 + kTitleH);
  if (page_ == kPgDevices) drawDevices(s, y0);
  else if (page_ == kPgPads) drawPads(s, y0);
  else drawKnobs(s, y0);
}

}  // namespace ui
