#include "ui_brick_controls.h"
#include "ui/app.h"
#include <algorithm>

namespace brick {
namespace {
using hw::InputType;
using B = Button;
int pos(B b) { return static_cast<int>(b); }
bool arrowKey(B b) { return pos(b) <= pos(B::Right); }
int delta(B b) { return b == B::Up || b == B::Left || b == B::L1 || b == B::L2 ? -1 : 1; }
bool horizontal(B b) { return b == B::Left || b == B::Right; }
}  // namespace

Controls::Controls(ui::App& app, LGFX& display) : app_(app), display_(display) {
  app_.enterBrickPointer = [this] {
    reset();
    pointer(true);
  };
}

Controls::~Controls() { app_.enterBrickPointer = {}; }

bool Controls::navigation(B key, bool down) {
  if (key == B::Select) {
    app_.onInput({down ? InputType::ShiftDown : InputType::ShiftUp, 0, down});
    return true;
  }
  const bool screen = key == B::L1 || key == B::R1;
  if (!screen && (pointer_ || (key != B::L2 && key != B::R2))) return false;
  const bool blocked = held(B::A) || held(B::B) || held(B::Start);
  if (down) repeatKey_ = B::Count;
  if (key == B::L2) {
    // A standalone L2 tap changes page. A chord such as L2+Start consumes it.
    if (down) {
      leftPagePending_ = !blocked;
      leftPageShift_ = held(B::Select);
      return true;
    }
    const bool pending = leftPagePending_;
    leftPagePending_ = false;
    if (!pending) return true;
  } else if (!down) {
    return true;
  }
  if (blocked) return true;
  if (screen) {
    app_.cycleBrickTab(delta(key));
  } else if (app_.brickMixer() || (key == B::L2 ? leftPageShift_ : held(B::Select))) {
    app_.moveBrickTrack(delta(key));
  } else {
    app_.cycleBrickPage(delta(key));
  }
  return true;
}

bool Controls::exiting() const { return app_.quitConfirmed(); }

void Controls::requestQuit() {
  // SDL may request a window close during a pointer drag.
  reset();
  app_.requestQuit();
}
void Controls::tap() {
  if (app_.tab() == ui::Tab::Mix && app_.menu().isOpen()) repeatKey_ = B::Count;
  if (aUsed_) {
    app_.onInput({InputType::EditEnd, 0, false});
  } else if (app_.brickGridActive() && !held(B::L2)) {
    app_.grid().insertAndAdvance();
  } else {
    app_.onInput({InputType::EncClick, 0, held(B::Select) || held(B::L2)});
  }
}
void Controls::arrow(B b) {
  if (pointer_) {
    int dx = horizontal(b) ? delta(b) : 0;
    int dy = horizontal(b) ? 0 : delta(b);
    int speed = held(B::L2) ? 1 : 6;
    pointerX_ = std::clamp(pointerX_ + dx * speed, 0, ui::kScreenW - 1);
    pointerY_ = std::clamp(pointerY_ + dy * speed, 0, ui::kScreenH - 1);
    display_.touch(pointerX_, pointerY_, held(B::A));
    app_.invalidate();
    return;
  }
  if (consumeA_) return;
  if (auto* mixer = app_.brickMixer()) {
    mixer->brickMixArrow(horizontal(b), horizontal(b) ? delta(b) : -delta(b), held(B::Select));
    return;
  }
  if (held(B::B)) return;  // B remains a back/undo tap while the D-pad moves focus.
  const bool edit = held(B::A) && !app_.menu().isOpen();
  bool coarse = horizontal(b) || held(B::Select) || held(B::L2);
  int d = edit && !horizontal(b) ? -delta(b) : delta(b);
  if (edit) aUsed_ = true;
  app_.onInput({edit ? InputType::EditTurn : InputType::EncTurn, static_cast<int8_t>(d), coarse,
                horizontal(b) ? hw::InputAxis::Horizontal : hw::InputAxis::Vertical, held(B::Select)});
}
void Controls::press(B b, bool down) {
  if (b == B::Count || held(b) == down) return;
  held_[pos(b)] = down;
  if (down && b != B::L2 && b != B::Select) leftPagePending_ = false;
  if (navigation(b, down)) return;
  if (b == B::Start) {
    if (down && held(B::Select)) consumeStart_ = true;  // KNULLI screenshot
    if (consumeStart_) {
      if (!down) consumeStart_ = false;
      return;
    }
  }
  if (arrowKey(b)) {
    if (down) {
      arrow(b);
      repeatKey_ = b;
      repeatAt_ = SDL_GetTicks() + 340;
    } else if (repeatKey_ == b)
      repeatKey_ = B::Count;
    return;
  }
  if (b == B::Start) {
    app_.onInput({down ? InputType::PlayPress : InputType::PlayRelease, 0, held(B::L2)});
    return;
  }
  if (pointer_) {
    if (b == B::A) display_.touch(pointerX_, pointerY_, down);
    if (down && (b == B::R2 || b == B::B)) pointer(false);
    return;
  }
  if (b == B::A) {
    if (down) {
      aUsed_ = false;
      if (auto* mixer = app_.brickMixer()) {
        mixer->brickMixToggle(true);
        consumeA_ = true;
      } else if (held(B::Select) && app_.brickGridActive()) {
        app_.clearBrickGrid();
        consumeA_ = true;
      }
    } else if (consumeA_) {
      consumeA_ = false;
    } else {
      tap();
    }
  } else if (b == B::B) {
    if (down) {
      undoB_ = held(B::Select) && app_.brickUndoAvailable();
      if (auto* mixer = app_.brickMixer()) {
        mixer->brickMixToggle(false);
        consumeB_ = true;
      }
    }
    if (consumeB_) {
      if (!down) consumeB_ = false;
    } else {
      app_.onInput({down ? InputType::BDown : InputType::BUp, 0, undoB_ && !app_.menu().isOpen()});
    }
  } else if (down && b == B::X) {
    if (app_.tab() == ui::Tab::Mix) repeatKey_ = B::Count;
    if (auto* mixer = app_.brickMixer()) mixer->brickMixMenu();
    else app_.onInput({InputType::EncLong, 0, held(B::Select) && app_.tab() == ui::Tab::Inst});
  } else if (down && b == B::Y && !held(B::A) && !held(B::B)) {
    repeatKey_ = B::Count;
    app_.onInput({InputType::EncClick, 0, held(B::Select)});
  }
}
void Controls::pointer(bool on) {
  pointer_ = on;
  display_.touch(pointerX_, pointerY_, false);
  aUsed_ = held(B::A);
  if (on) {
    display_.overlay = [this](LGFX_Sprite& s) {
      s.drawRect(pointerX_ - 3, pointerY_ - 3, 7, 7, ui::kBg);
      s.drawRect(pointerX_ - 4, pointerY_ - 4, 9, 9, ui::kCursor);
    };
    app_.toast("A TOUCH   L2 FINE   R2 BACK");
  } else {
    display_.overlay = {};
  }
  app_.invalidate();
}
void Controls::tick() {
  if (repeatKey_ != B::Count && SDL_TICKS_PASSED(SDL_GetTicks(), repeatAt_)) {
    arrow(repeatKey_);
    repeatAt_ = SDL_GetTicks() + (pointer_ ? 18 : 70);
  }
}
void Controls::reset() {
  if (held(B::Start) && !consumeStart_) app_.onInput({InputType::PlayRelease, 0, false});
  if (aUsed_) app_.onInput({InputType::EditEnd, 0, false});
  if (held(B::B)) app_.dropInput({InputType::BUp, 0, false});
  held_.fill(false);
  consumeA_ = consumeB_ = consumeStart_ = undoB_ = false;
  leftPagePending_ = leftPageShift_ = false;
  app_.onInput({InputType::ShiftUp, 0, false});
  repeatKey_ = B::Count;
  aUsed_ = false;
  pointer(false);
}
}  // namespace brick
