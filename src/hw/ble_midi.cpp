#include "ble_midi.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <strings.h>
#include <atomic>

namespace hw {
namespace ble {
namespace {

const char* const kSvc = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
const char* const kChr = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
constexpr int kSlots = static_cast<int>(Slot::Count);
constexpr int kLinks = kSlots;  // distinct devices: at most one per slot
constexpr int kQueueLen = 64;
constexpr uint32_t kConnectMs = 5000;
constexpr uint32_t kRetryMs = 2000;
constexpr uint32_t kIdleMs = 500;    // safety poll; setActive/disconnect wake the task anyway
constexpr uint32_t kQuietMs = 8000;  // longest wait for the task to leave NimBLE calls
constexpr uint32_t kDyingMs = 3000;  // a dropped client whose disconnect event never came
// Our side of connect + GATT discovery + GAP name read: blocking waits, std::vector/std::string from
// discovery and NimBLE's error logs (vprintf). The GATT event work runs on the NimBLE host task
// (nimconfig.h default 4096; the Arduino core's sdkconfig sets 5120), so ours needs about as much.
// Internal RAM is tight (31 KB free with two links) and end() deletes the task. stackFree() shows
// the real headroom on hardware.
constexpr uint32_t kStack = 5120;
// OUT writer ("bleout"): its own task, because bleconn sits in a blocking connect for up to 5 s while
// the IN device is away. Lives only while OUT is up (internal RAM). Its calls never block: blePack,
// getMTU, a write without response straight to the host (no NimBLE log on the way).
constexpr uint32_t kOutStack = 3072;
constexpr uint32_t kWriteMs = 5;   // back-off after the controller refused a write
constexpr int kRing = 256;         // power of 2
constexpr int kRoomUrgent = 32;    // fewer slots free: only note-offs, stop, all-notes-off
constexpr int kRoomClock = 64;     // fewer slots free: clock ticks dropped
constexpr int kPktMax = 128;       // packet buffer: MTU - 3, capped

struct Cfg {
  char addr[18];  // "" = slot not set
  uint8_t type;
  char name[kNameMax];  // from setActive; "" = unknown (read from GAP on connect)
};

// One connection, shared by both slots when they hold the same address.
struct Link {
  char addr[18];  // "" = free (unless dying)
  uint8_t type;
  NimBLEClient* client;
  NimBLERemoteCharacteristic* chr;  // valid while up (rebuilt on each connect); the writer sends OUT through it
  bool up;
  bool subscribed;  // notifications on for this connection (once, right after connect)
  bool inUp;        // task only: IN role live, to count connects()
  char name[kNameMax];
  // A dropped client still disconnecting: the entry stays taken until its onDisconnect, so a new
  // device never shares a parser with the old one's last notifications. Compared, never dereferenced.
  NimBLEClient* dying;
  uint32_t dyingMs;
  std::atomic<bool> in;        // deliver notifications (this device holds the IN slot)
  std::atomic<bool> resetReq;  // parser reset, done by the host task before its next feed
  mt::BleMidiParser parser;    // host task only (notify / disconnect callbacks)
};

// cfg and links are written by the task, the host callbacks and the UI: short copies under the
// spinlock, never a NimBLE call inside it.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Cfg cfg[kSlots];
Link links[kLinks];

TaskHandle_t task = nullptr;
QueueHandle_t queue = nullptr;
std::atomic<bool> on{false};       // stack up, task may work
std::atomic<bool> holdOff{false};  // scan in progress: no connect attempts
std::atomic<bool> busy{false};     // task is inside NimBLE calls
std::atomic<bool> inConnect{false};
std::atomic<bool> scanDone{false};
std::atomic<uint32_t> drops{0};
std::atomic<uint32_t> nconn{0};

// OUT: SPSC ring, engine (send) -> writer task. In PSRAM, allocated once by begin() and never freed:
// the engine may still be past its outUp check when the stack goes down. 2 KB, touched per message.
// outUp: the writer runs and links[outLink].chr was ready when it started; lowered by the host on
// that link's disconnect and by outStop(). inSend: the engine is between its outUp check and the
// notify (outStop waits it out, so the writer handle and the ring tail are then ours).
mt::TimedMsg* ring = nullptr;
std::atomic<uint32_t> head{0};  // engine writes
std::atomic<uint32_t> tail{0};  // writer writes (bleconn while no writer: the flush)
std::atomic<bool> outUp{false};
std::atomic<bool> outFresh{false};  // OUT came up since takeOutFresh()
std::atomic<bool> inSend{false};
std::atomic<int> outLink{0};
std::atomic<bool> wStop{false};
std::atomic<bool> wDone{false};
std::atomic<uint32_t> dropsOut{0};
std::atomic<bool> clockOn{true};  // setClock(): 0xF8 goes out
TaskHandle_t writer = nullptr;  // bleconn / end() only (end() once bleconn is gone)
// The writer's own stack high-water mark, bytes (it reports itself: the UI never touches a handle
// that may be deleting itself). 0 while there is no writer.
std::atomic<uint32_t> outStack{0};

// Scan results land here from the host task (nullptr outside scan()).
Found* scanOut = nullptr;
int scanMax = 0;
int scanN = 0;

bool sameAddr(const char* a, const char* b) { return a[0] && strcasecmp(a, b) == 0; }

int findLink(const char* addr) {
  for (int i = 0; i < kLinks; ++i)
    if (sameAddr(links[i].addr, addr)) return i;
  return -1;
}

void clearLink(Link& l) {  // under mux
  l.addr[0] = 0;
  l.client = nullptr;
  l.chr = nullptr;
  l.up = l.subscribed = l.inUp = false;
  l.name[0] = 0;
  l.in = false;
}

void wake() {
  if (task) xTaskNotifyGive(task);
}

// NimBLE host task. Real-time bytes (clock from a controller) are not ours to follow: the tracker
// is always the clock master.
void push(const mt::MidiMsg& m, void*) {
  if (m.status >= 0xF8) return;
  if (xQueueSend(queue, &m, 0) != pdTRUE) drops.fetch_add(1, std::memory_order_relaxed);
}

class ClientCbs : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* c, int) override {
    int i = -1;
    portENTER_CRITICAL(&mux);
    for (int k = 0; k < kLinks; ++k) {
      if (links[k].client == c) {
        i = k;
        links[k].up = false;
        links[k].subscribed = false;
        links[k].chr = nullptr;
        // The writer stops writing; bleconn (woken below) stops it. With no writer outUp is already
        // false and outLink stale: harmless.
        if (k == outLink) outUp = false;
      }
      if (links[k].dying == c) links[k].dying = nullptr;  // the entry is free again
    }
    portEXIT_CRITICAL(&mux);
    if (i >= 0) links[i].parser.reset();
    wake();
  }
};
ClientCbs clientCbs;

class ScanCbs : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    static const NimBLEUUID svc(kSvc);
    if (!d->isAdvertisingService(svc)) return;
    const std::string a = d->getAddress().toString();
    const std::string nm = d->getName();
    portENTER_CRITICAL(&mux);
    if (scanOut) {
      int i = 0;
      while (i < scanN && !sameAddr(scanOut[i].addr, a.c_str())) ++i;
      if (i == scanN && scanN < scanMax) {
        Found& f = scanOut[scanN++];
        strlcpy(f.addr, a.c_str(), sizeof(f.addr));
        f.type = d->getAddressType();
        f.name[0] = 0;
      }
      // The name often comes in the scan response, a second report for the same address.
      if (i < scanN && !scanOut[i].name[0] && !nm.empty())
        strlcpy(scanOut[i].name, nm.c_str(), kNameMax);
    }
    portEXIT_CRITICAL(&mux);
  }
  void onScanEnd(const NimBLEScanResults&, int) override { scanDone = true; }
};
ScanCbs scanCbs;

enum class Wr : uint8_t { Idle, Sent, Refused };

// Writer: one packet from the ring's oldest messages. Sent: the ring moved (more may follow).
// The link's client and chr stay valid while the writer runs: bleconn reconnects (which rebuilds the
// attributes) or deletes a client only after outStop(), and end() likewise.
Wr writeOne() {
  const uint32_t t = tail.load(std::memory_order_relaxed);
  const uint32_t h = head.load(std::memory_order_acquire);
  if (h == t || !outUp) return Wr::Idle;
  const int i = outLink;
  portENTER_CRITICAL(&mux);
  NimBLEClient* c = links[i].client;
  NimBLERemoteCharacteristic* chr = links[i].chr;
  const bool up = links[i].up;
  portEXIT_CRITICAL(&mux);
  if (!c || !chr || !up) return Wr::Idle;
  int cap = static_cast<int>(c->getMTU()) - 3;  // 0 once disconnected
  if (cap < 20) return Wr::Idle;
  if (cap > kPktMax) cap = kPktMax;
  const uint32_t at = t & (kRing - 1);
  int n = static_cast<int>(h - t);
  if (n > kRing - static_cast<int>(at)) n = kRing - static_cast<int>(at);  // up to the wrap; the rest next
  uint8_t pkt[kPktMax];
  int taken = 0;
  const int len = mt::blePack(&ring[at], n, pkt, cap, taken);
  if (len > 0) {
    // The host call itself: writeValue would log an error on every refusal and ask the MTU again.
    // Refused (ENOMEM) when the controller's buffers are full: kept for after the back-off.
    // Accepted window: OUT drops and a new connection (IN) takes the same handle between the read
    // above and this write; one packet would reach the wrong device's handle (most likely ignored).
    if (ble_gattc_write_no_rsp_flat(c->getConnHandle(), chr->getHandle(), pkt, static_cast<uint16_t>(len)) != 0)
      return Wr::Refused;
  } else if (taken == 0) {
    taken = 1;  // does not fit even alone (cannot happen at cap >= 20): drop it rather than stall
    dropsOut.fetch_add(1, std::memory_order_relaxed);
  }  // else only bad messages: skipped, nothing sent
  tail.store(t + static_cast<uint32_t>(taken), std::memory_order_release);
  return Wr::Sent;
}

// Sleeps until send() (or outStop) notifies. After a refusal it waits kWriteMs whatever the
// notifies: buffers free up per connection event, retrying on every send() would only spin.
void writerFn(void*) {
  outStack = uxTaskGetStackHighWaterMark(nullptr);  // non-zero as soon as OUT is up
  bool refused = false;
  while (!wStop) {
    if (refused) {
      const TickType_t t0 = xTaskGetTickCount(), w = pdMS_TO_TICKS(kWriteMs);
      for (TickType_t el = 0; !wStop && el < w; el = xTaskGetTickCount() - t0) ulTaskNotifyTake(pdTRUE, w - el);
    } else {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    Wr r = Wr::Idle;
    while (!wStop && (r = writeOne()) == Wr::Sent) {
    }
    refused = r == Wr::Refused;
    outStack = uxTaskGetStackHighWaterMark(nullptr);
  }
  wDone = true;
  vTaskDelete(nullptr);
}

// bleconn (or end() with bleconn gone): OUT is ready on link i. Stale messages are dropped first:
// no writer and no producer (outUp is down) at this point.
void outStart(int i) {
  if (!ring) return;  // PSRAM allocation failed in begin(): no OUT
  tail.store(head.load());
  outLink = i;
  wStop = false;
  wDone = false;
  if (xTaskCreatePinnedToCore(writerFn, "bleout", kOutStack, nullptr, 2, &writer, 0) != pdPASS) {
    writer = nullptr;  // no RAM: retried on the next pass
    return;
  }
  outUp = true;
  outFresh = true;
}

// bleconn (or end() with bleconn gone): no more sends; the writer leaves (it is never inside a
// blocking call). Afterwards nothing touches the link's chr until outStart.
void outStop() {
  // seq_cst on both sides (a Dekker pair with send()): store outUp then load inSend here, store
  // inSend then load outUp there. Weaker orders let both loads see the old values, and the engine
  // would notify a writer that is being deleted.
  outUp.store(false, std::memory_order_seq_cst);
  while (inSend.load(std::memory_order_seq_cst)) delay(1);  // the engine is between its check and the notify
  if (!writer) return;
  wStop = true;
  xTaskNotifyGive(writer);
  while (!wDone) delay(1);
  writer = nullptr;
  outStack = 0;
}

// Task: link i still belongs to client c and the stack is still wanted. Checked after connect,
// getService and the GAP name read (the calls that can stall, and where end() / scan() want us out
// early); not after getCharacteristic or subscribe. That is safe: end() never frees a client while
// the task is busy (it leaves the stack up instead), so c stays valid for the whole call.
bool alive(int i, NimBLEClient* c) {
  portENTER_CRITICAL(&mux);
  const bool ok = links[i].client == c;
  portEXIT_CRITICAL(&mux);
  return ok && on && !holdOff;
}

// Task: connect link i, find the MIDI characteristic, turn notifications on. The callback stays for
// the whole connection; links[i].in decides whether its data reaches the queue, so the IN role can
// move between devices without re-subscribing on a live link.
bool connectLink(int i) {
  Link& l = links[i];
  NimBLEClient* c = l.client;  // only this task writes it
  l.inUp = false;
  if (!c) {
    c = NimBLEDevice::createClient();
    if (!c) return false;
    c->setClientCallbacks(&clientCbs, false);
    c->setConnectTimeout(kConnectMs);
    portENTER_CRITICAL(&mux);
    l.client = c;
    portEXIT_CRITICAL(&mux);
  }
  if (!c->isConnected()) {
    inConnect = true;
    const bool ok = c->connect(NimBLEAddress(std::string(l.addr), l.type), true);
    inConnect = false;
    if (!ok) return false;
  }
  if (!alive(i, c)) return false;  // end() / scan() hold us off: the link is left to them
  // 7.5-15 ms interval: notes from a controller should not wait for a slow default interval.
  c->updateConnParams(6, 12, 0, 200);
  NimBLERemoteService* svc = c->getService(kSvc);
  if (!alive(i, c)) return false;
  NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(kChr) : nullptr;
  if (!chr) {
    c->disconnect();
    return false;
  }
  if (!l.name[0]) {
    // Unknown after a reboot: the GAP Device Name, else the address.
    std::string nm = c->getValue(NimBLEUUID(static_cast<uint16_t>(0x1800)),
                                 NimBLEUUID(static_cast<uint16_t>(0x2A00)));
    if (!alive(i, c)) return false;
    if (nm.empty()) nm = l.addr;
    portENTER_CRITICAL(&mux);
    strlcpy(l.name, nm.c_str(), kNameMax);
    portEXIT_CRITICAL(&mux);
  }
  l.resetReq = true;  // nothing from c reached the parser yet; clears what the previous client left
  bool sub = false;
  if (chr->canNotify()) {
    sub = chr->subscribe(true, [i, c](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
      Link& k = links[i];
      portENTER_CRITICAL(&mux);
      const bool mine = k.client == c;  // the entry may have moved on to another device
      portEXIT_CRITICAL(&mux);
      if (!mine || !k.in) return;
      if (k.resetReq.exchange(false)) k.parser.reset();
      k.parser.feed(d, static_cast<int>(n), push, nullptr);
    });
    // No alive() bail here: subscribed must be recorded, or a later pass would subscribe again and
    // replace the std::function while the host task may be calling it.
  }
  // An OUT-only device may do without notifications; the IN device may not.
  if (!sub && l.in) {
    c->disconnect();
    return false;
  }
  const bool up = c->isConnected();
  portENTER_CRITICAL(&mux);
  l.chr = chr;
  l.subscribed = sub;
  l.up = up;
  portEXIT_CRITICAL(&mux);
  return up;
}

// Task: bring links in line with cfg. Returns how long to sleep.
uint32_t work() {
  Cfg snap[kSlots];
  portENTER_CRITICAL(&mux);
  memcpy(snap, cfg, sizeof(snap));
  portEXIT_CRITICAL(&mux);

  // The writer's link went down, moved, or left the OUT slot (outUp also falls on a disconnect).
  if (writer) {
    const Link& o = links[outLink];
    if (!outUp || !sameAddr(snap[1].addr, o.addr) || !o.up || !o.client || !o.client->isConnected())
      outStop();
  }

  for (int i = 0; i < kLinks; ++i) {
    Link& l = links[i];
    portENTER_CRITICAL(&mux);
    if (l.dying && millis() - l.dyingMs > kDyingMs) l.dying = nullptr;
    portEXIT_CRITICAL(&mux);
    if (!l.addr[0]) continue;
    bool used = false;
    for (int s = 0; s < kSlots; ++s) used |= sameAddr(snap[s].addr, l.addr);
    l.in = sameAddr(snap[0].addr, l.addr);
    if (used) continue;
    if (writer && i == outLink) outStop();
    // No slot uses this device any more. deleteClient disconnects first and frees on the event.
    NimBLEClient* c = l.client;
    const bool conn = c && c->isConnected();
    portENTER_CRITICAL(&mux);
    clearLink(l);
    if (conn) {
      l.dying = c;
      l.dyingMs = millis();
    }
    portEXIT_CRITICAL(&mux);
    if (c) NimBLEDevice::deleteClient(c);
  }

  bool failed = false;
  for (int s = 0; s < kSlots && on && !holdOff; ++s) {
    if (!snap[s].addr[0]) continue;
    int i = findLink(snap[s].addr);
    if (i < 0) {
      for (int k = 0; k < kLinks && i < 0; ++k)
        if (!links[k].addr[0] && !links[k].dying) i = k;
      if (i < 0) {  // the old device is still disconnecting: its onDisconnect wakes us
        failed = true;
        continue;
      }
      portENTER_CRITICAL(&mux);
      strlcpy(links[i].addr, snap[s].addr, sizeof(links[i].addr));
      links[i].type = snap[s].type;
      strlcpy(links[i].name, snap[s].name, kNameMax);
      portEXIT_CRITICAL(&mux);
      links[i].in = sameAddr(snap[0].addr, links[i].addr);
    }
    Link& l = links[i];
    // isConnected too: a disconnect landing between connectLink's check and its up = true.
    if (!l.up || !l.client || !l.client->isConnected()) {
      if (writer && i == outLink) outStop();  // connect rebuilds the attributes the writer holds
      if (!connectLink(i)) failed = true;
    } else if (l.in && !l.subscribed) {
      // Was OUT-only without notifications, now IN: reconnect (onDisconnect wakes us).
      l.client->disconnect();
    }
  }

  // The IN role went live on a link (fresh connection or moved here): the router starts over.
  for (int i = 0; i < kLinks; ++i) {
    Link& l = links[i];
    const bool live = l.up && l.subscribed && l.in;
    if (live && !l.inUp) {
      l.resetReq = true;
      nconn.fetch_add(1);
    }
    l.inUp = live;
  }

  if (!writer && on && !holdOff && snap[1].addr[0]) {
    const int i = findLink(snap[1].addr);
    if (i >= 0 && links[i].up && links[i].chr && links[i].client && links[i].client->isConnected()) outStart(i);
  }
  return failed ? kRetryMs : kIdleMs;
}

void taskFn(void*) {
  uint32_t waitMs = 0;
  for (;;) {
    busy = false;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
    busy = true;
    // Checked after raising busy: quiesce() lowers on / raises holdOff, then waits for !busy.
    if (!on || holdOff) {
      waitMs = kIdleMs;
      continue;
    }
    waitMs = work();
  }
}

// UI: make the task leave NimBLE calls (the caller already lowered on or raised holdOff). A pending
// connect to an absent device would hold it for 5 s, so cancel that. False: still busy at kQuietMs.
bool quiesce() {
  const uint32_t t0 = millis();
  while (busy) {
    if (millis() - t0 >= kQuietMs) return false;
    if (inConnect) ble_gap_conn_cancel();
    wake();
    delay(10);
  }
  return true;
}

}  // namespace

void begin() {
  if (on) return;
  if (!queue) queue = xQueueCreate(kQueueLen, sizeof(mt::MidiMsg));
  if (!ring) ring = static_cast<mt::TimedMsg*>(heap_caps_malloc(kRing * sizeof(mt::TimedMsg), MALLOC_CAP_SPIRAM));
  if (!NimBLEDevice::isInitialized() && !NimBLEDevice::init("d-trk")) return;
  // No pairing by default. A controller that demands bonding: NimBLEDevice::setSecurityAuth(true, false, false);
  on = true;
  if (!task) xTaskCreatePinnedToCore(taskFn, "bleconn", kStack, nullptr, 1, &task, 0);
  wake();
}

void end() {
  if (!on) return;
  on = false;
  if (!quiesce()) {
    // A GATT call that never returned: deinit would free the client under it. Leave the stack up
    // (running() stays true); the caller may try again later.
    on = true;
    wake();
    return;
  }
  if (task) {
    vTaskDelete(task);  // idle: blocked in ulTaskNotifyTake or about to be, holds nothing
    task = nullptr;
  }
  outStop();
  NimBLEClient* cl[kLinks] = {};
  for (int i = 0; i < kLinks; ++i) cl[i] = links[i].client;
  for (int i = 0; i < kLinks; ++i)
    if (cl[i] && cl[i]->isConnected()) cl[i]->disconnect();
  // Let our disconnect events land before the host stops (deinit then frees the clients).
  const uint32_t t0 = millis();
  for (;;) {
    bool pending = false;
    for (int i = 0; i < kLinks; ++i) pending |= cl[i] && cl[i]->isConnected();
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < kLinks; ++i) pending |= links[i].dying != nullptr;
    portEXIT_CRITICAL(&mux);
    if (!pending || millis() - t0 >= 1000) break;
    delay(10);
  }
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < kLinks; ++i) {
    clearLink(links[i]);
    links[i].dying = nullptr;
  }
  portEXIT_CRITICAL(&mux);
  for (int i = 0; i < kLinks; ++i) {
    links[i].parser.reset();
    links[i].resetReq = false;
  }
  if (queue) xQueueReset(queue);
  NimBLEDevice::deinit(true);
}

bool running() { return on; }

State state(Slot s) {
  const int k = static_cast<int>(s);
  if (!on || k >= kSlots) return State::Off;
  State r;
  portENTER_CRITICAL(&mux);
  if (!cfg[k].addr[0]) {
    r = State::Off;
  } else {
    const int i = findLink(cfg[k].addr);
    const bool ok = i >= 0 && links[i].up && (s != Slot::In || (links[i].subscribed && links[i].in));
    r = ok ? State::Connected : State::Searching;
  }
  portEXIT_CRITICAL(&mux);
  return r;
}

const char* name(Slot s) {
  static char out[kSlots][kNameMax];
  const int k = static_cast<int>(s);
  if (k >= kSlots) return "";
  out[k][0] = 0;
  if (state(s) != State::Connected) return out[k];
  portENTER_CRITICAL(&mux);
  const int i = findLink(cfg[k].addr);
  if (i >= 0) memcpy(out[k], links[i].name, kNameMax);
  portEXIT_CRITICAL(&mux);
  return out[k];
}

int scan(Found* out, int max) {
  if (!on) begin();
  if (!on || max <= 0) return 0;
  // NimBLE refuses to scan while a connection is being set up: hold the task off for the scan.
  holdOff = true;
  if (!quiesce()) {
    holdOff = false;
    wake();
    return 0;
  }
  portENTER_CRITICAL(&mux);
  scanOut = out;
  scanMax = max;
  scanN = 0;
  portEXIT_CRITICAL(&mux);
  scanDone = false;
  NimBLEScan* sc = NimBLEDevice::getScan();
  sc->setScanCallbacks(&scanCbs, false);  // stays set: onResult ignores reports outside scan()
  sc->setActiveScan(true);                // names often sit in the scan response
  sc->setMaxResults(0);                   // callbacks only: no device list kept in internal RAM
  if (sc->start(3000, false)) {
    const uint32_t t0 = millis();
    while (!scanDone && millis() - t0 < 4000) delay(20);
    if (!scanDone) sc->stop();  // only on timeout: the host may still be delivering otherwise
  }
  portENTER_CRITICAL(&mux);
  const int n = scanN;
  scanOut = nullptr;
  scanMax = scanN = 0;
  portEXIT_CRITICAL(&mux);
  holdOff = false;
  wake();
  return n;
}

void setActive(const char* inAddr, uint8_t inType, const char* inName, const char* outAddr, uint8_t outType,
               const char* outName) {
  Cfg c[kSlots] = {};
  const char* addr[kSlots] = {inAddr, outAddr};
  const uint8_t type[kSlots] = {inType, outType};
  const char* nm[kSlots] = {inName, outName};
  for (int s = 0; s < kSlots; ++s) {
    if (!addr[s] || !addr[s][0]) continue;
    strlcpy(c[s].addr, addr[s], sizeof(c[s].addr));
    c[s].type = type[s];
    // A name that is only the address: left empty, so the connect reads the GAP name.
    if (nm[s] && !sameAddr(c[s].addr, nm[s])) strlcpy(c[s].name, nm[s], kNameMax);
  }
  portENTER_CRITICAL(&mux);
  for (int s = 0; s < kSlots; ++s) {
    cfg[s] = c[s];
    // A link already up keeps its name unless a real one is given.
    const int i = c[s].addr[0] ? findLink(c[s].addr) : -1;
    if (i >= 0 && c[s].name[0]) strlcpy(links[i].name, c[s].name, kNameMax);
  }
  portEXIT_CRITICAL(&mux);
  wake();  // the task drops links no slot uses and connects the new ones
}

bool wanted() {
  portENTER_CRITICAL(&mux);
  const bool any = cfg[0].addr[0] || cfg[1].addr[0];
  portEXIT_CRITICAL(&mux);
  return any;
}

bool next(mt::MidiMsg& m) { return queue && xQueueReceive(queue, &m, 0) == pdTRUE; }

uint32_t dropped() { return drops.load(std::memory_order_relaxed); }

uint32_t connects() { return nconn.load(); }

uint32_t stackFree() { return task ? uxTaskGetStackHighWaterMark(task) : 0; }

namespace {
// Room is kept for what stops sound: a full queue must not leave a note hanging.
bool urgent(const uint8_t* b, uint8_t len) {
  const uint8_t hi = b[0] & 0xF0;
  return hi == 0x80 || (hi == 0x90 && len == 3 && b[2] == 0) || b[0] == 0xFC ||
         (hi == 0xB0 && len >= 2 && b[1] == 123);
}
}  // namespace

void setClock(bool on) { clockOn.store(on, std::memory_order_relaxed); }

bool clock() { return clockOn.load(std::memory_order_relaxed); }

bool send(uint64_t us, const uint8_t* b, uint8_t len) {
  if (!outUp.load(std::memory_order_relaxed) || len == 0 || len > 3) return false;
  if (b[0] == 0xF8 && !clockOn.load(std::memory_order_relaxed)) return false;  // off: not a drop
  inSend.store(true, std::memory_order_seq_cst);  // seq_cst: the Dekker pair with outStop()
  if (!outUp.load(std::memory_order_seq_cst)) {  // went down since the first look: outStop() may be past its wait
    inSend.store(false, std::memory_order_release);
    return false;
  }
  const uint32_t h = head.load(std::memory_order_relaxed);
  const int room = kRing - static_cast<int>(h - tail.load(std::memory_order_acquire));
  bool ok = room > 0;
  if (ok && room < kRoomUrgent) ok = urgent(b, len);
  else if (ok && room < kRoomClock) ok = b[0] != 0xF8;
  if (ok) {
    mt::TimedMsg& m = ring[h & (kRing - 1)];
    m.ms = static_cast<uint32_t>(us / 1000);
    memcpy(m.b, b, len);
    m.len = len;
    head.store(h + 1, std::memory_order_release);
    xTaskNotifyGive(writer);
  } else {
    dropsOut.fetch_add(1, std::memory_order_relaxed);
  }
  inSend.store(false, std::memory_order_release);
  return ok;
}

uint32_t outStackFree() { return outStack.load(std::memory_order_relaxed); }

bool outReady() { return outUp.load(std::memory_order_relaxed); }
bool takeOutFresh() { return outFresh.exchange(false); }

uint32_t droppedOut() { return dropsOut.load(std::memory_order_relaxed); }

}  // namespace ble
}  // namespace hw
