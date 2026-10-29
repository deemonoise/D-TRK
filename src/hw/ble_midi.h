#pragma once
#include <stdint.h>
#include "ble_midi_parse.h"

namespace hw {
// BLE MIDI devices (NimBLE central). Two slots: IN (a controller: its notifications fill a queue) and
// OUT (a sound module: send() feeds it). One device may sit in both slots: one connection.
// UI task calls everything below; connecting runs in a background task.
namespace ble {

enum class State : uint8_t { Off, Searching, Connected };
enum class Slot : uint8_t { In, Out, Count };
constexpr int kNameMax = 24;
struct Found {
  char addr[18];  // "aa:bb:cc:dd:ee:ff"
  uint8_t type;   // BLE address type (public / random) as advertised
  char name[kNameMax];
};

void begin();  // stack up; connects to the setActive() devices in the background (none: idle)
// Disconnect all, stack down (the Wi-Fi dialog needs the internal RAM). If the connect task is stuck
// in a GATT call for 8 s, the stack is left up and running() stays true.
void end();
bool running();
State state(Slot s);       // Off when the slot is not set or the stack is down
const char* name(Slot s);  // the connected device's name, "" otherwise
// Blocking scan (~3 s) for devices advertising the BLE-MIDI service. Returns the count. Brings the
// stack up when it is down. A connect attempt in flight is cancelled first (retried after).
int scan(Found* out, int max);
// The devices for the slots (null / empty address = slot not set); the same address in both = one
// connection. Names come from the device list (a name equal to the address counts as none: the GAP
// name is read on connect). Nothing stored here: the caller keeps the list. Wakes the connect task;
// does not bring the stack up (begin() when wanted()).
void setActive(const char* inAddr, uint8_t inType, const char* inName, const char* outAddr, uint8_t outType,
               const char* outName);
bool wanted();  // a device is set in either slot (BLE should run)
// IN messages received since the last call, oldest first (queue of 64). Real-time bytes are not queued.
bool next(mt::MidiMsg& m);
uint32_t dropped();   // IN messages lost to a full queue since boot
uint32_t connects();  // bumps on every new IN connection (the router resets its knob state)
uint32_t stackFree(); // bleconn task stack never touched so far, bytes (0 while the task is down)

// Engine task only (single producer): queues a MIDI message for the OUT slot, stamped `us`
// (engine::nowUs()). False when dropped (full, or OUT not connected: then nothing is queued). Never
// blocks; a disconnected OUT costs one atomic load.
bool send(uint64_t us, const uint8_t* b, uint8_t len);
// Clock ticks (0xF8) to OUT, on by default; off: send() refuses them (not counted as drops).
// Start / Continue / Stop always go. Any task.
void setClock(bool on);
bool clock();
uint32_t outStackFree();  // bleout writer stack never touched so far, bytes (0 while OUT is down)
bool outReady();        // OUT is up (a relaxed load: lets the caller skip stamping the time)
bool takeOutFresh();    // OUT connected since the last call (then the caller sends a MIDI panic). Any task.
uint32_t droppedOut();  // OUT messages lost to a full queue since boot

}  // namespace ble
}  // namespace hw
