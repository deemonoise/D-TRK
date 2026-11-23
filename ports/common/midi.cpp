// USB uses ALSA directly. KNULLI's PipeWire BLE devices use private ALSA ports.
#include <atomic>
#include <mutex>

#include "Arduino.h"
#include "ble_midi_pipewire.h"
#include "hw/ble_midi.h"
#ifdef __linux__
#include <fcntl.h>
#include <sound/asequencer.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
namespace hw::ble {
namespace {
int fd = -1;
std::string selected[2], labels[2];
bool active[2]{};
std::atomic<bool> clockOn{true}, fresh{false}, ready{false};
std::atomic<uint32_t> losses{0};
uint32_t connections = 0;
std::mutex endpointMutex;
#ifdef __linux__
snd_seq_addr local{}, peers[2]{};
bool bluetooth[2]{};
int bridgePorts[2]{-1, -1};
pid_t bridgeChild = -1;
uint32_t lastBridgeStart = 0;
bool openPort() {
  if (fd >= 0) return true;
  fd = open("/dev/snd/seq", O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return false;
  int client = -1;
  snd_seq_port_info info{};
  info.capability = SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_READ |
                    SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
  info.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | SNDRV_SEQ_PORT_TYPE_APPLICATION;
  strcpy(info.name, "D-TRK");
  if (ioctl(fd, SNDRV_SEQ_IOCTL_CLIENT_ID, &client) >= 0) info.addr.client = client;
  if (client < 0 || ioctl(fd, SNDRV_SEQ_IOCTL_CREATE_PORT, &info) < 0) {
    close(fd);
    fd = -1;
    return false;
  }
  local = {static_cast<uint8_t>(client), info.addr.port};
  snd_seq_client_info c{};
  c.client = client;
  if (ioctl(fd, SNDRV_SEQ_IOCTL_GET_CLIENT_INFO, &c) == 0) {
    strcpy(c.name, "D-TRK");
    ioctl(fd, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, &c);
  }
  return true;
}
template <class Visitor>
void forPorts(Visitor visit) {
  snd_seq_client_info c{};
  c.client = -1;
  while (ioctl(fd, SNDRV_SEQ_IOCTL_QUERY_NEXT_CLIENT, &c) == 0) {
    if (!c.client || c.client == local.client) continue;
    snd_seq_port_info p{};
    p.addr.client = c.client;
    p.addr.port = 255;
    while (ioctl(fd, SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT, &p) == 0) {
      if (!(p.capability & (SNDRV_SEQ_PORT_CAP_SUBS_READ | SNDRV_SEQ_PORT_CAP_SUBS_WRITE))) continue;
      // ALSA client numbers change across boots. Persist client name and local port.
      uint64_t h = 14695981039346656037ull;
      for (auto* s = c.name; *s; ++s) h = (h ^ uint8_t(*s)) * 1099511628211ull;
      h = (h ^ p.addr.port) * 1099511628211ull;
      Found device{};
      snprintf(device.addr, sizeof(device.addr), "%02x:%02x:%02x:%02x:%02x:%02x", unsigned(h >> 40) & 255,
               unsigned(h >> 32) & 255, unsigned(h >> 24) & 255, unsigned(h >> 16) & 255,
               unsigned(h >> 8) & 255, unsigned(h) & 255);
      snprintf(device.name, sizeof(device.name), "%.*s", kNameMax - 1, p.name[0] ? p.name : c.name);
      visit(device, p);
    }
  }
}
void subscribe(int i, snd_seq_addr remote, bool connect) {
  snd_seq_port_subscribe sub{};
  sub.sender = i ? local : remote;
  sub.dest = i ? remote : local;
  bool ok = ioctl(fd, connect ? SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT : SNDRV_SEQ_IOCTL_UNSUBSCRIBE_PORT, &sub) == 0;
  active[i] = connect && (ok || errno == EBUSY && ioctl(fd, SNDRV_SEQ_IOCTL_GET_SUBSCRIPTION, &sub) == 0);
  if (i) {
    ready = active[i];
    fresh = active[i];
  } else if (active[i])
    ++connections;
  peers[i] = remote;
}
void closeBridge() {
  pipewire::stop(bridgeChild);
  for (auto& port : bridgePorts) {
    if (fd >= 0 && port >= 0) {
      snd_seq_port_info info{};
      info.addr = {local.client, static_cast<uint8_t>(port)};
      ioctl(fd, SNDRV_SEQ_IOCTL_DELETE_PORT, &info);
    }
    port = -1;
  }
}
int createBridgePort(int slot) {
  snd_seq_port_info info{};
  info.addr.client = local.client;
  info.capability = slot ? SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_SUBS_READ
                         : SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
  info.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | SNDRV_SEQ_PORT_TYPE_HARDWARE;
  strcpy(info.name, slot ? "BLE MIDI output" : "BLE MIDI input");
  return ioctl(fd, SNDRV_SEQ_IOCTL_CREATE_PORT, &info) == 0 ? info.addr.port : -1;
}
void refreshBridge() {
  if ((!bluetooth[0] && !bluetooth[1]) || pipewire::running(bridgeChild)) return;
  const auto now = millis();
  if (now - lastBridgeStart < 5000) return;
  lastBridgeStart = now;
  closeBridge();
  for (int i = 0; i < 2; ++i)
    if (bluetooth[i]) bridgePorts[i] = createBridgePort(i);
  if ((!bluetooth[0] || bridgePorts[0] >= 0) && (!bluetooth[1] || bridgePorts[1] >= 0))
    bridgeChild = pipewire::start(local.client, bluetooth[0] ? selected[0].c_str() : "", bridgePorts[0],
                                 bluetooth[1] ? selected[1].c_str() : "", bridgePorts[1]);
  if (bridgeChild < 0) closeBridge();
}
bool bridgeLinked(int i) {
  if (bridgeChild < 0 || bridgePorts[i] < 0) return false;
  snd_seq_port_info info{};
  info.addr = {local.client, static_cast<uint8_t>(bridgePorts[i])};
  return ioctl(fd, SNDRV_SEQ_IOCTL_GET_PORT_INFO, &info) == 0 &&
         (i ? info.read_use > 0 : info.write_use > 0);
}
void refresh() {
  if (!openPort()) return;
  refreshBridge();
  bool seen[2]{};
  forPorts([&](const Found& dev, const snd_seq_port_info& p) {
    for (int i = 0; i < 2; ++i)
      if (!bluetooth[i] && selected[i] == dev.addr) {
        seen[i] = true;
        if (!active[i] || peers[i].client != p.addr.client) subscribe(i, p.addr, true);
        if (active[i]) labels[i] = dev.name;
      }
  });
  for (int i = 0; i < 2; ++i) {
    if (bluetooth[i]) {
      const bool linked = bridgeLinked(i);
      if (linked && !active[i]) {
        if (i) fresh = true;
        else ++connections;
      }
      active[i] = linked;
      if (i) ready = linked;
    } else if (!seen[i]) {
      active[i] = false;
      if (i) ready = false;
    }
  }
}
bool decode(const snd_seq_event& ev, mt::MidiMsg& m) {
  auto n = ev.data.note;
  auto c = ev.data.control;
  auto ch = c.channel & 15;
  switch (ev.type) {
    case SNDRV_SEQ_EVENT_NOTEOFF:
    case SNDRV_SEQ_EVENT_NOTEON:
    case SNDRV_SEQ_EVENT_KEYPRESS:
      m = {uint8_t(((ev.type == SNDRV_SEQ_EVENT_NOTEON    ? 0x90
                     : ev.type == SNDRV_SEQ_EVENT_NOTEOFF ? 0x80
                                                          : 0xA0)) |
                   n.channel),
           n.note, n.velocity};
      return true;
    case SNDRV_SEQ_EVENT_CONTROLLER:
      m = {uint8_t(0xb0 | ch), uint8_t(c.param), uint8_t(c.value)};
      return true;
    case SNDRV_SEQ_EVENT_PGMCHANGE:
    case SNDRV_SEQ_EVENT_CHANPRESS:
      m = {uint8_t((ev.type == SNDRV_SEQ_EVENT_PGMCHANGE ? 0xc0 : 0xd0) | ch), uint8_t(c.value), 0};
      return true;
    case SNDRV_SEQ_EVENT_PITCHBEND:
      m = {uint8_t(0xe0 | ch), uint8_t((c.value + 8192) & 127), uint8_t((c.value + 8192) >> 7)};
      return true;
    case SNDRV_SEQ_EVENT_START:
      m = {0xfa, 0, 0};
      return true;
    case SNDRV_SEQ_EVENT_CONTINUE:
      m = {0xfb, 0, 0};
      return true;
    case SNDRV_SEQ_EVENT_STOP:
      m = {0xfc, 0, 0};
      return true;
    default:
      return false;
  }
}
bool output(const uint8_t* b, uint8_t n) {
  if (!n) return false;
  snd_seq_event ev{};
  if (bluetooth[1]) {
    ev.source = {local.client, static_cast<uint8_t>(bridgePorts[1])};
    ev.dest = {SNDRV_SEQ_ADDRESS_SUBSCRIBERS, 0};
  } else {
    ev.source = local;
    ev.dest = peers[1];
  }
  ev.queue = SNDRV_SEQ_QUEUE_DIRECT;
  const int high = b[0] >> 4, ch = b[0] & 15;
  if (high == 15) {
    switch (b[0]) {
      case 0xf8:
        if (!clockOn) return false;
        ev.type = SNDRV_SEQ_EVENT_CLOCK;
        break;
      case 0xfa:
        ev.type = SNDRV_SEQ_EVENT_START;
        break;
      case 0xfb:
        ev.type = SNDRV_SEQ_EVENT_CONTINUE;
        break;
      case 0xfc:
        ev.type = SNDRV_SEQ_EVENT_STOP;
        break;
      default:
        return false;
    }
  } else {
    if (high < 8 || n < 1 + mt::midiDataLen(b[0])) return false;
    static constexpr unsigned char types[] = {SNDRV_SEQ_EVENT_NOTEOFF,   SNDRV_SEQ_EVENT_NOTEON,
                                              SNDRV_SEQ_EVENT_KEYPRESS,  SNDRV_SEQ_EVENT_CONTROLLER,
                                              SNDRV_SEQ_EVENT_PGMCHANGE, SNDRV_SEQ_EVENT_CHANPRESS,
                                              SNDRV_SEQ_EVENT_PITCHBEND};
    ev.type = types[high - 8];
    if (high <= 10) {
      ev.data.note.channel = ch;
      ev.data.note.note = b[1];
      ev.data.note.velocity = b[2];
    } else {
      ev.data.control.channel = ch;
      ev.data.control.param = high == 11 ? b[1] : 0;
      ev.data.control.value = high == 11 ? b[2] : high == 14 ? ((b[2] << 7) | b[1]) - 8192 : b[1];
    }
  }
  if (write(fd, &ev, sizeof(ev)) == sizeof(ev)) return true;
  ++losses;
  return false;
}
#endif
}  // namespace
void begin() {
#ifdef __linux__
  std::lock_guard<std::mutex> lock(endpointMutex);
  refresh();
#endif
}
void end() {
  std::lock_guard<std::mutex> lock(endpointMutex);
#ifdef __linux__
  closeBridge();
  if (fd >= 0) close(fd);
#endif
  fd = -1;
  active[0] = active[1] = false;
  ready = false;
}
bool running() { return fd >= 0; }
bool wanted() { return !selected[0].empty() || !selected[1].empty(); }
State state(Slot s) {
  int i = int(s);
  return active[i] ? State::Connected : selected[i].empty() || fd < 0 ? State::Off : State::Searching;
}
const char* name(Slot s) { return active[int(s)] ? labels[int(s)].c_str() : ""; }
int scan(Found* out, int max) {
  int count = 0;
#ifdef __linux__
  {
    std::lock_guard<std::mutex> guard(endpointMutex);
    if (openPort())
      forPorts([&](const Found& d, const auto&) {
        if (count < max) out[count++] = d;
      });
  }
  if (count < max) count += pipewire::scan(out + count, max - count);
#endif
  return count;
}
void setActive(const char* ia, uint8_t inType, const char* in, const char* oa, uint8_t outType, const char* on) {
  std::lock_guard<std::mutex> lock(endpointMutex);
#ifdef __linux__
  bool restartBridge = false;
#endif
  for (int i = 0; i < 2; ++i) {
    const char* a = i ? oa : ia;
    bool changed = selected[i] != (a ? a : "");
#ifdef __linux__
    const bool throughBluetooth = a && *a && (i ? outType : inType) == pipewire::kDeviceType;
    changed = changed || bluetooth[i] != throughBluetooth;
    if (changed) {
      restartBridge = restartBridge || bluetooth[i] || throughBluetooth;
      if (active[i] && !bluetooth[i]) subscribe(i, peers[i], false);
      if (i) ready = false;
      active[i] = false;
    }
    bluetooth[i] = throughBluetooth;
#else
    (void)inType;
    (void)outType;
#endif
    if (changed) selected[i] = a ? a : "";
    const char* label = i ? on : in;
    labels[i] = label ? label : "";
  }
#ifdef __linux__
  if (restartBridge) {
    closeBridge();
    lastBridgeStart = millis() - 5000;
  }
  if (wanted()) refresh();
#endif
}
bool next(mt::MidiMsg& m) {
#ifdef __linux__
  static uint32_t last = 0;
  if (wanted() && millis() - last > 2000) {
    last = millis();
    begin();
  }
  snd_seq_event ev{};
  while (fd >= 0 && read(fd, &ev, sizeof(ev)) == sizeof(ev))
    if (active[0] && (bluetooth[0] ? ev.dest.port == bridgePorts[0]
                                   : ev.source.client == peers[0].client && ev.source.port == peers[0].port) &&
        decode(ev, m))
      return true;
#endif
  return false;
}
bool send(uint64_t, const uint8_t* b, uint8_t n) {
#ifdef __linux__
  std::unique_lock<std::mutex> guard(endpointMutex, std::try_to_lock);
  if (guard && ready) return output(b, n);
  if (!guard) ++losses;
#endif
  return false;
}
uint32_t connects() { return connections; }
bool clock() { return clockOn; }
void setClock(bool on) { clockOn = on; }
bool takeOutFresh() { return fresh.exchange(false); }
bool outReady() { return ready.load(); }
uint32_t droppedOut() { return losses; }
uint32_t dropped() { return 0; }
uint32_t stackFree() { return 0; }
uint32_t outStackFree() { return 0; }
}  // namespace hw::ble
