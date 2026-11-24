#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "rg351v/port_profile.h"
#include "ui/app.h"
#include "ui/wifi_dialog.h"

namespace {
std::string localAddress() {
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces)) return {};
  std::string ip;
  for (auto* item = interfaces; item; item = item->ifa_next) {
    if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || !(item->ifa_flags & IFF_UP) ||
        (item->ifa_flags & IFF_LOOPBACK))
      continue;
    char addr[INET_ADDRSTRLEN]{};
    auto* in = reinterpret_cast<sockaddr_in*>(item->ifa_addr);
    if (inet_ntop(AF_INET, &in->sin_addr, addr, sizeof(addr))) {
      ip = addr;
      break;
    }
  }
  freeifaddrs(interfaces);
  return ip;
}
bool listening(const char* ip, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return false;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  // A local firewall can silently drop probes. Keep the FILE screen responsive.
  bool ok = false;
  if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
    ok = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    if (!ok && errno == EINPROGRESS) {
      pollfd probe{fd, POLLOUT, 0};
      int error = 0;
      socklen_t size = sizeof(error);
      ok = poll(&probe, 1, 15) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
    }
  }
  close(fd);
  return ok;
}
}  // namespace
namespace ui {
bool WifiDialog::open() {
  open_ = true;
  redraw_ = true;
  app_.invalidate();
  return true;
}
void WifiDialog::close() { open_ = false; }
void WifiDialog::poll() {}
bool WifiDialog::wantsRedraw() {
  static uint32_t last = 0;
  if (open_ && (redraw_ || millis() - last > 3000)) {
    last = millis();
    redraw_ = false;
    return true;
  }
  return false;
}
void WifiDialog::onInput(const hw::InputEvent& ev) {
  if (ev.type == hw::InputType::EncClick || ev.type == hw::InputType::EncLong ||
      ev.type == hw::InputType::BUp)
    close();
}
void WifiDialog::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Tap) close();
}
void WifiDialog::draw(LGFX_Sprite& s, int y) {
  auto ip = localAddress();
  s.fillRect(0, y, kScreenW, kAreaH, kBg);
  s.setTextColor(kText);
  if (rg351v::selected()) {
    s.drawString("ARKOS FILE TRANSFER", 12, y + 12);
    s.drawString((ip.empty() ? "Offline - connect in ArkOS Options" : "IP  " + ip).c_str(), 12, y + 52);
    if (listening(ip.c_str(), 22))
      s.drawString(("SCP: ark@" + ip).c_str(), 12, y + 78);
    else
      s.drawString("Enable Remote Services in ArkOS Options", 12, y + 78);
    if (const char* data = std::getenv("DTRK_DATA"))
      s.drawString(("Data: " + std::string(data)).c_str(), 12, y + 124);
    s.drawString("App: /roms/ports or /roms2/ports", 12, y + 152);
    s.drawString("A / B  Back", 12, y + 212);
    return;
  }
  s.drawString("KNULLI FILE TRANSFER", 12, y + 12);
  if (ip.empty())
    s.drawString("Offline - connect in KNULLI Network Settings", 12, y + 52);
  else {
    s.drawString(("IP  " + ip).c_str(), 12, y + 52);
    auto share =
        listening(ip.c_str(), 445) ? "smb://" + ip + "/share" : "SMB offline - enable file sharing in KNULLI";
    s.drawString(share.c_str(), 12, y + 78);
    if (listening(ip.c_str(), 22)) s.drawString(("SCP: root@" + ip + ":/userdata").c_str(), 12, y + 100);
  }
  s.drawString("Projects and audio: saves/d-trk/", 12, y + 124);
  s.drawString("App: roms/ports/d-trk/", 12, y + 152);
  s.drawString("A / B  Back", 12, y + 212);
}
}  // namespace ui
