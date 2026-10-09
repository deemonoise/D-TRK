#pragma once
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "keyboard.h"
#include "net/wifi.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// FILE -> Wi-Fi: joins the home network and serves the firmware page (src/net/web.cpp) until EXIT.
// The engine is stopped and Play locked while open. Fills the work area.
class WifiDialog {
 public:
  explicit WifiDialog(App& app) : app_(app) {}
  // False (with a toast) when the engine did not stop. The caller checks unsaved changes.
  bool open();
  // Radio off, Play unlocked.
  void close();
  bool isOpen() const { return open_; }
  void poll();
  bool wantsRedraw();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum class St : uint8_t { Setup, Connecting, Online, Failed };
  enum Button : int { kExit, kNetwork, kRetry, kButtons };
  enum MenuId : int { kMenuCancel = 100 };
  static constexpr int kMaxNets = 15;
  static constexpr int kLogLines = 7;
  static constexpr int kLogW = 56;
  static constexpr int kHeaderH = 28;
  static constexpr int kLineH = 20;
  static constexpr int kLogY = 96;
  static constexpr int kLogH = 18;
  static constexpr int kBtnH = 36;
  static constexpr int kBtnW = 148;

  bool enabled(int b) const;
  void press(int b);
  void moveSel(int delta);
  void chooseNetwork();
  void askPassword(const char* initial);
  void connect();
  void goOnline();
  void addLog(const char* line);
  void setState(St s);

  App& app_;
  Keyboard kb_;
  net::Creds creds_{};
  bool saveCreds_ = false;  // store creds_ once they work
  char (*nets_)[net::kSsidMax] = nullptr;  // PSRAM while the scan menu is open
  char log_[kLogLines][kLogW] = {};
  int logCount_ = 0;
  St st_ = St::Setup;
  int sel_ = kExit;
  int y0_ = kAreaY;
  bool open_ = false;
  bool redraw_ = false;
};

}  // namespace ui
