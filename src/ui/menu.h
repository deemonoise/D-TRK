#pragma once
#include <functional>
#include "menu_history.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "touch.h"
#include "theme.h"

namespace ui {

struct MenuItem {
  const char* label;
  int id;
  bool enabled = true;
};

// Modal list. Encoder: turn = select, click = choose, long = close.
// Tap on an item = choose, tap outside = close. Labels and title are copied.
class Menu {
 public:
  static constexpr int kMaxItems = 24;
  static constexpr int kVisible = (kAreaH - 24) / 20;
  static constexpr int kW = 240;
  static constexpr int kRowH = 20;

  void open(const char* title, const MenuItem* items, int count, std::function<void(int id)> onChoose);
  bool openChild(const char* title, const MenuItem* items, int count, std::function<void(int id)> onChoose);
  void replace(const char* title, const MenuItem* items, int count, std::function<void(int id)> onChoose);
  void back();
  bool isOpen() const { return open_; }
  void close();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s);

 private:
  struct Item {
    char label[28];
    int id;
    bool enabled;
  };
  struct Frame {
    char title[28]{};
    Item items[kMaxItems]{};
    int count = 0, selection = 0, top = 0;
    std::function<void(int)> action;
  };
  Frame snapshot() const;
  void restore(Frame&& frame);
  void load(const char* title, const MenuItem* items, int count, std::function<void(int)> onChoose);
  mt::MenuHistory<Frame, 4> history_;
  bool choosing_ = false;
  int rows() const { return count_ < kVisible ? count_ : kVisible; }
  int height() const { return (rows() + 1) * kRowH + 4; }
  int left() const;
  int topY() const;
  void move(int delta);
  void ensureVisible();
  void choose(int idx);

  char title_[28] = {0};
  Item items_[kMaxItems];
  int count_ = 0;
  int sel_ = 0;
  int top_ = 0;
  int dragAcc_ = 0;  // drag px not yet turned into rows
  bool open_ = false;
  std::function<void(int)> onChoose_;
};

}  // namespace ui
