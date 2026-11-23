#include "ui_brick_controls.h"
#include <cstring>

namespace brick {
namespace {
using B = Button;
B keyboard(SDL_Keycode key) {
  switch (key) {
    case SDLK_LEFT:
      return B::Left;
    case SDLK_RIGHT:
      return B::Right;
    case SDLK_UP:
      return B::Up;
    case SDLK_DOWN:
      return B::Down;
    case SDLK_z:
      return B::B;
    case SDLK_x:
      return B::A;
    case SDLK_a:
      return B::Y;
    case SDLK_s:
      return B::X;
    case SDLK_q:
      return B::L1;
    case SDLK_w:
      return B::R1;
    case SDLK_LSHIFT:
      return B::L2;
    case SDLK_e:
      return B::R2;
    case SDLK_TAB:
      return B::Select;
    case SDLK_RETURN:
      return B::Start;
    default:
      return B::Count;
  }
}
B controller(int key) {
  switch (key) {
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
      return B::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
      return B::Right;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
      return B::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
      return B::Down;
    case SDL_CONTROLLER_BUTTON_A:
      return B::B;  // south
    case SDL_CONTROLLER_BUTTON_B:
      return B::A;  // east
    case SDL_CONTROLLER_BUTTON_X:
      return B::Y;  // west
    case SDL_CONTROLLER_BUTTON_Y:
      return B::X;  // north
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
      return B::L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
      return B::R1;
    case SDL_CONTROLLER_BUTTON_BACK:
      return B::Select;
    case SDL_CONTROLLER_BUTTON_START:
      return B::Start;
    default:
      return B::Count;
  }
}
// KNULLI's es_input.cfg physical IDs for "TRIMUI Brick Controller".
// 0/1 are volume, 14 is the system hotkey, and front F1/F2 (15/16) are unused.
B brickButton(int id) {
  static constexpr B keys[] = {B::B, B::A, B::X, B::Y, B::L1, B::R1, B::L2, B::R2, B::Select, B::Start};
  return id >= 4 && id <= 13 ? keys[id - 4] : B::Count;
}
}  // namespace
Input::Input(Controls& controls) : controls_(controls) {
  for (int i = 0; i < SDL_NumJoysticks() && id_ < 0; ++i) open(i);
}
Input::~Input() { close(); }
void Input::open(int index) {
  if (id_ >= 0) return;
  const char* name = SDL_JoystickNameForIndex(index);
  if (name && std::strcmp(name, "TRIMUI Brick Controller") == 0)
    joystick_ = SDL_JoystickOpen(index);
  else if (SDL_IsGameController(index)) {
    controller_ = SDL_GameControllerOpen(index);
    if (controller_) joystick_ = SDL_GameControllerGetJoystick(controller_);
  }
  if (joystick_) id_ = SDL_JoystickInstanceID(joystick_);
}
void Input::close() {
  if (controller_)
    SDL_GameControllerClose(controller_);
  else if (joystick_)
    SDL_JoystickClose(joystick_);
  controller_ = nullptr;
  joystick_ = nullptr;
  id_ = -1;
  hat_ = 0;
}
void Input::event(const SDL_Event& e) {
  switch (e.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      if (!e.key.repeat) controls_.press(keyboard(e.key.keysym.sym), e.type == SDL_KEYDOWN);
      break;
    case SDL_JOYDEVICEADDED:
      open(e.jdevice.which);
      break;
    case SDL_JOYDEVICEREMOVED:
      if (e.jdevice.which == id_) {
        controls_.reset();
        close();
        for (int i = 0; i < SDL_NumJoysticks() && id_ < 0; ++i) open(i);
      }
      break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
      if (!controller_ && e.jbutton.which == id_)
        controls_.press(brickButton(e.jbutton.button), e.type == SDL_JOYBUTTONDOWN);
      break;
    case SDL_JOYHATMOTION:
      if (!controller_ && e.jhat.which == id_ && e.jhat.hat == 0) {
        const uint8_t bits[] = {SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT};
        const B buttons[] = {B::Up, B::Down, B::Left, B::Right};
        for (int i = 0; i < 4; ++i)
          if ((hat_ ^ e.jhat.value) & bits[i]) controls_.press(buttons[i], e.jhat.value & bits[i]);
        hat_ = e.jhat.value;
      }
      break;
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERBUTTONDOWN:
      if (controller_ && e.cbutton.which == id_)
        controls_.press(controller(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN);
      break;
    case SDL_CONTROLLERAXISMOTION:
      if (controller_ && e.caxis.which == id_ &&
          (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
           e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
        controls_.press(e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? B::L2 : B::R2,
                        e.caxis.value > 16000);
      break;
    default:
      break;
  }
}
}  // namespace brick

#include "hw/input.h"
namespace hw {
void inputBegin() {}
bool inputPoll(InputEvent& out, TickType_t) {
  // The normal event loop is suspended inside a synchronous render/export.
  // Drain just the input queues so SDL_QUIT and device changes reach the main loop.
  SDL_PumpEvents();
  const uint32_t kinds[] = {
      SDL_KEYDOWN,           SDL_KEYUP, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP, SDL_CONTROLLERBUTTONDOWN,
      SDL_CONTROLLERBUTTONUP};
  SDL_Event e;
  for (auto type : kinds) {
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, type, type) > 0) {
      bool cancel = type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_s || e.key.keysym.sym == SDLK_RETURN);
      if (type == SDL_JOYBUTTONDOWN) {
        const char* name = SDL_JoystickName(SDL_JoystickFromInstanceID(e.jbutton.which));
        cancel = name && std::strcmp(name, "TRIMUI Brick Controller") == 0 &&
                 (brick::brickButton(e.jbutton.button) == brick::Button::X ||
                  brick::brickButton(e.jbutton.button) == brick::Button::Start);
      }
      if (type == SDL_CONTROLLERBUTTONDOWN)
        cancel =
            e.cbutton.button == SDL_CONTROLLER_BUTTON_Y || e.cbutton.button == SDL_CONTROLLER_BUTTON_START;
      if (cancel) {
        out = {InputType::EncLong, 0, false};
        return true;
      }
    }
  }
  return false;
}
}  // namespace hw
