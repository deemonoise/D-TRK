#include "wifi_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "esp_heap_caps.h"
#include "net/web.h"
#include "storage/storage.h"

namespace ui {
namespace {

constexpr const char* kButtonLabels[] = {"EXIT", "NETWORK", "RETRY"};

}  // namespace

bool WifiDialog::open() {
  if (!storage::stopEngine()) {
    app_.toast(storage::resultText(storage::Result::EngineBusy));
    return false;
  }
  app_.lockTransport(true);
  open_ = true;
  logCount_ = 0;
  {
    char line[40];  // diagnostics: Wi-Fi fails without enough internal RAM
    snprintf(line, sizeof(line), "RAM %uK FREE, LARGEST %uK",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    addLog(line);
  }
  sel_ = kExit;
  saveCreds_ = false;
  if (net::loadCreds(creds_)) {
    connect();
  } else {
    setState(St::Setup);
    chooseNetwork();
  }
  return true;
}

void WifiDialog::close() {
  if (!open_) return;
  kb_.close();
  heap_caps_free(nets_);
  nets_ = nullptr;
  net::webEnd();
  net::off();
  app_.lockTransport(false);
  open_ = false;
}

void WifiDialog::setState(St s) {
  st_ = s;
  if (!enabled(sel_)) sel_ = kExit;
  redraw_ = true;
}

void WifiDialog::addLog(const char* line) {
  if (logCount_ == kLogLines) {
    memmove(log_[0], log_[1], sizeof(log_[0]) * (kLogLines - 1));
    --logCount_;
  }
  strlcpy(log_[logCount_++], line, kLogW);
  redraw_ = true;
}

void WifiDialog::connect() {
  net::webEnd();
  net::connect(creds_);
  setState(St::Connecting);
}

void WifiDialog::chooseNetwork() {
  net::webEnd();
  net::off();
  if (!nets_) nets_ = static_cast<char(*)[net::kSsidMax]>(heap_caps_malloc(kMaxNets * net::kSsidMax, MALLOC_CAP_SPIRAM));
  if (!nets_) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    setState(St::Failed);
    return;
  }
  app_.showBusy("SCANNING...");
  const int n = net::scan(nets_, kMaxNets);
  net::off();
  if (n == 0) {
    heap_caps_free(nets_);
    nets_ = nullptr;
    app_.toast("NO NETWORKS FOUND");
    setState(creds_.ssid[0] ? St::Failed : St::Setup);
    return;
  }
  MenuItem items[kMaxNets + 1];
  for (int i = 0; i < n; ++i) items[i] = {nets_[i], i};
  items[n] = {"Cancel", kMenuCancel};
  app_.menu().open("WI-FI NETWORK", items, n + 1, [this](int id) {
    if (id >= 0 && id < kMaxNets && nets_) {
      net::Creds c{};
      strlcpy(c.ssid, nets_[id], sizeof(c.ssid));
      const bool same = strcmp(c.ssid, creds_.ssid) == 0;
      creds_ = same ? creds_ : c;
      askPassword(same ? creds_.pass : "");
    } else {
      setState(creds_.ssid[0] ? St::Failed : St::Setup);
    }
    heap_caps_free(nets_);
    nets_ = nullptr;
  });
  setState(creds_.ssid[0] ? St::Failed : St::Setup);
}

void WifiDialog::askPassword(const char* initial) {
  kb_.open("PASS:", initial, [this](const char* text) {
    strlcpy(creds_.pass, text, sizeof(creds_.pass));
    saveCreds_ = true;
    connect();
  }, true);
}

void WifiDialog::goOnline() {
  if (saveCreds_) {
    net::saveCreds(creds_);
    saveCreds_ = false;
  }
  net::WebHooks h;
  h.log = [this](const char* line) { addLog(line); };
  h.busy = [this](const char* msg) { app_.showBusy(msg); };
  net::webBegin(h);
  addLog("ONLINE");
  setState(St::Online);
}

void WifiDialog::poll() {
  if (!open_) return;
  const net::Link l = net::link();
  switch (st_) {
    case St::Connecting:
      if (l == net::Link::Up) {
        if (net::webRunning()) setState(St::Online);  // reconnected
        else goOnline();
      } else if (l == net::Link::Failed) {
        net::webEnd();
        addLog("! NO CONNECTION");
        setState(St::Failed);
      }
      break;
    case St::Online:
      if (l != net::Link::Up) {
        addLog("! LINK LOST");
        setState(St::Connecting);
      }
      break;
    default: break;
  }
  net::webPoll();
}

bool WifiDialog::wantsRedraw() {
  const bool r = redraw_;
  redraw_ = false;
  return r;
}

bool WifiDialog::enabled(int b) const {
  switch (b) {
    case kRetry: return st_ == St::Failed && creds_.ssid[0];
    default: return true;
  }
}

void WifiDialog::press(int b) {
  if (!enabled(b)) return;
  sel_ = b;
  switch (b) {
    case kExit: close(); break;
    case kNetwork: chooseNetwork(); break;
    case kRetry: connect(); break;
    default: break;
  }
}

void WifiDialog::moveSel(int delta) {
  const int dir = delta >= 0 ? 1 : -1;
  int n = delta < 0 ? -delta : delta;
  for (int guard = 0; guard < kButtons * 2 && n > 0; ++guard) {
    sel_ = (sel_ + dir + kButtons) % kButtons;
    if (enabled(sel_)) --n;
  }
}

void WifiDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (kb_.isOpen()) {
    kb_.onInput(ev);
    return;
  }
  switch (ev.type) {
    case InputType::EncTurn: moveSel(ev.delta); break;
    case InputType::EncClick: press(sel_); break;
    case InputType::EncLong: close(); break;
    default: break;
  }
}

void WifiDialog::onTouch(const TouchEvent& ev) {
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    return;
  }
  if (ev.type != TouchType::Tap) return;
  const int by = y0_ + kAreaH - kBtnH - 4;
  if (ev.y < by || ev.y >= by + kBtnH) return;
  const int left = (kScreenW - kButtons * kBtnW - (kButtons - 1) * 8) / 2;
  const int b = (ev.x - left) / (kBtnW + 8);
  if (ev.x >= left && b < kButtons) press(b);
}

void WifiDialog::draw(LGFX_Sprite& s, int y0) {
  y0_ = y0;
  if (kb_.isOpen()) {
    kb_.draw(s, y0);
    return;
  }
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  s.drawString("WI-FI FIRMWARE", 16, ty);
  s.setTextColor(kDim);
  s.drawString(creds_.ssid, kScreenW - 16 - static_cast<int>(strlen(creds_.ssid)) * kCharW, ty);

  char buf[64];
  int y = y0 + kHeaderH;
  switch (st_) {
    case St::Setup:
      s.setTextColor(kDim);
      s.drawString("NO NETWORK SET: PRESS NETWORK", 16, y);
      break;
    case St::Connecting:
      s.setTextColor(kCursor);
      snprintf(buf, sizeof(buf), "CONNECTING TO %s...", creds_.ssid);
      s.drawString(buf, 16, y);
      break;
    case St::Online:
      s.setTextColor(kCursor);
      s.drawString("http://d-trk.local/firmware", 16, y);
      net::ip(buf, sizeof(buf));
      s.setTextColor(kText);
      if (buf[0]) {
        char url[40];
        snprintf(url, sizeof(url), "http://%s/firmware", buf);
        s.drawString(url, 16, y + kLineH);
      }
      break;
    case St::Failed:
      s.setTextColor(kEditCursor);
      s.drawString("NO NETWORK / WRONG PASSWORD", 16, y);
      break;
  }
  s.setTextColor(kDim);
  s.drawString("PLAY LOCKED. NO PASSWORD ON THE PAGE:", 16, y + 2 * kLineH);
  s.drawString("ANYONE ON THIS NETWORK CAN USE IT.", 16, y + 3 * kLineH - 2);

  s.setTextColor(kText);
  for (int i = 0; i < logCount_; ++i) s.drawString(log_[i], 16, y0 + kLogY + 8 + i * kLogH);

  const int by = y0 + kAreaH - kBtnH - 4;
  const int left = (kScreenW - kButtons * kBtnW - (kButtons - 1) * 8) / 2;
  for (int b = 0; b < kButtons; ++b) {
    const int x = left + b * (kBtnW + 8);
    const bool sel = b == sel_, on = enabled(b);
    s.fillRect(x, by, kBtnW, kBtnH, sel ? kPlayBg : kSelBg);
    if (sel) s.drawRect(x, by, kBtnW, kBtnH, kCursor);
    s.setTextColor(!on ? kDim : (sel ? kCursor : kText));
    const int lw = static_cast<int>(strlen(kButtonLabels[b])) * kCharW;
    s.drawString(kButtonLabels[b], x + (kBtnW - lw) / 2, by + (kBtnH - kCharH) / 2);
  }
}

}  // namespace ui
