#pragma once
#include <SDL.h>
#include <array>
#include <cstdint>

class LGFX;
namespace ui {
class App;
}

namespace brick {
enum class Button { Down, Up, Left, Right, B, A, Y, X, L1, R1, L2, R2, Select, Start, Count };

class Controls {
 public:
  Controls(ui::App& app, LGFX& display);
  ~Controls();
  void press(Button key, bool down);
  void reset();
  void requestQuit();
  void tick();
  bool exiting() const;

 private:
  void tap();
  void arrow(Button key);
  bool navigation(Button key, bool down);
  void pointer(bool on);
  bool held(Button key) const { return held_[static_cast<int>(key)]; }
  ui::App& app_;
  LGFX& display_;
  std::array<bool, static_cast<int>(Button::Count)> held_{};
  bool aUsed_ = false, pointer_ = false;
  int pointerX_ = 240, pointerY_ = 160;
  bool consumeA_ = false, consumeB_ = false, consumeStart_ = false, undoB_ = false;
  bool leftPagePending_ = false, leftPageShift_ = false;
  uint32_t repeatAt_ = 0;
  Button repeatKey_ = Button::Count;
};

class Input {
 public:
  explicit Input(Controls& controls);
  ~Input();
  void event(const SDL_Event& ev);

 private:
  void open(int index);
  void close();
  Controls& controls_;
  SDL_GameController* controller_ = nullptr;
  SDL_Joystick* joystick_ = nullptr;
  SDL_JoystickID id_ = -1;
  uint8_t hat_ = 0;
};
}  // namespace brick
