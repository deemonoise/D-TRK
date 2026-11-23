#include "engine/engine.h"
#include "ui/app.h"
#include "ui/bank_screen.h"
#include "ui/keyboard.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Axis = hw::InputAxis;
using Type = hw::InputType;
constexpr hw::InputEvent up{Type::EncTurn, -1, false, Axis::Vertical};
constexpr hw::InputEvent down{Type::EncTurn, 1, false, Axis::Vertical};
constexpr hw::InputEvent left{Type::EncTurn, -1, false, Axis::Horizontal};
constexpr hw::InputEvent right{Type::EncTurn, 1, false, Axis::Horizontal};
constexpr hw::InputEvent click{Type::EncClick, 0, false};

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

void keyboard() {
  ui::Keyboard kb;
  std::string text;
  auto accept = [&](const char* value) { text = value; };
  kb.open("NAME", "", accept);
  // Start on OK. Type '_KL', delete L, cross OK and DEL, and return to the same L.
  for (auto event : {up, click, up, click, right, click, down, click, down, up, up, click, down, down, click})
    kb.onInput(event);
  require(!kb.isOpen() && text == "_KL", "name keyboard lost its column at DEL/OK");

  kb.open("PASSWORD", "", accept, true);
  // OK -> symbols switch -> SPACE -> comma in the same column -> switch -> OK.
  for (auto event : {left, click, up, click, up, click, down, down, right, click}) kb.onInput(event);
  require(!kb.isOpen() && text == " ,", "symbol keyboard wide keys");

  kb.open("NAME", "", accept);
  kb.onInput({Type::EncTurn, 1, false});  // Firmware encoder wraps from OK to 1.
  for (auto event : {left, click}) kb.onInput(event);
  kb.onInput({Type::EncTurn, -1, false});
  kb.onInput(click);
  require(!kb.isOpen() && text == "1", "keyboard edges or encoder wrap");
}

void qwerty() {
  ui::Keyboard kb;
  std::string text;
  kb.open("PASS", "", [&](const char* value) { text = value; }, true);
  kb.onInput({Type::EncTurn, 1, false});  // First digit.
  const std::string rows[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_"};
  std::string expected;
  for (int row = 0; row < 4; ++row) {
    const auto& keys = rows[row];
    expected += keys;
    kb.onInput(click);
    for (size_t i = 1; i < keys.size(); ++i) {
      kb.onInput(right);
      kb.onInput(click);
    }
    if (row < 3) {
      for (size_t i = 1; i < keys.size(); ++i) kb.onInput(left);
      kb.onInput(down);
    }
  }
  kb.onInput(left);  // Shift + M on the password keyboard.
  kb.onInput({Type::EncClick, 0, true});
  kb.onInput(down);
  kb.onInput(click);
  require(text == expected + "M", "QWERTY rows, case or punctuation");
}

void textField() {
  ui::Keyboard kb;
  std::string text;
  auto accept = [&](const char* value) { text = value; };
  auto field = [&] {
    for (int i = 0; i < 5; ++i) kb.onInput(up);  // OK -> top key 8 -> text field
  };
  auto fromTopToOk = [&] {
    for (int i = 0; i < 4; ++i) kb.onInput(down);
    kb.onInput(click);
  };
  kb.open("NAME", "ABCD", accept);
  field();
  // Change C to D; return to the top key and insert 8 immediately before that D.
  for (auto ev : {left, left, up, click, click}) kb.onInput(ev);
  fromTopToOk();
  require(!kb.isOpen() && text == "AB8DD", "field cursor, single-character cycling, middle insertion");

  kb.open("NAME", "ABCD", accept);
  field();
  for (auto ev : {left, left, click, right, down, down, down, click, down, click}) kb.onInput(ev);
  require(!kb.isOpen() && text == "ACD", "DEL removes the character before the middle cursor");

  kb.open("NAME", "1234567890123456", accept);
  field();
  for (auto ev : {right, up, left, up, click, click}) kb.onInput(ev);
  fromTopToOk();
  require(text == "1234567890123457", "full name remains editable within its length limit");

  std::string password(63, 'x');
  kb.open("PASS", password.c_str(), accept, true);
  field();
  for (int i = 0; i < 70; ++i) kb.onInput(left);
  for (auto ev : {up, click, click}) kb.onInput(ev);
  fromTopToOk();
  require(text == "y" + password.substr(1), "long password cursor at first character");

  kb.open("PASS", "", accept, true);
  field();
  for (auto ev : {up, right, up, left, down, click}) kb.onInput(ev);
  fromTopToOk();
  require(text == "`a", "empty text and printable symbols");
}

void bank() {
  setenv("DTRK_MIDI_OUT", "/dev/null", 1);
  auto project = std::make_unique<mt::Project>();
  project->reset();
  engine::begin(project.get());
  auto app = std::make_unique<ui::App>();
  ui::BankScreen bank(*app);
  bank.onEnter();
  auto chosen = [&] {
    bank.onInput({Type::EncClick, 0, true});
    return engine::status().pattern;
  };
  for (auto event : {up, left, right, down}) bank.onInput(event);
  require(chosen() == 5, "BANK move from P01 to P06");
  for (auto event : {right, right, right, down, down, down, down}) bank.onInput(event);
  require(chosen() == 15, "BANK right and bottom boundaries");
  for (auto event : {up, left, left, left, left}) bank.onInput(event);
  require(chosen() == 8, "BANK left boundary jumped rows");
  bank.onInput({Type::EncTurn, -9, false});
  require(chosen() == 15, "BANK firmware encoder wrap");
}
}  // namespace

int main() {
  try {
    textField();
    qwerty();
    keyboard();
    bank();
    std::puts("Spatial keyboard, caret editing, BANK pattern tiles and encoder navigation OK");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
