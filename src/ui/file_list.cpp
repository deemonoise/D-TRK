#include "file_list.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "audio/audio.h"
#include "link/bank_client.h"
#include "esp_heap_caps.h"
#include "file_rules.h"
#include "storage/storage.h"

namespace ui {

bool FileList::alloc() {
  if (names_) return true;
  const size_t bytes = static_cast<size_t>(cap_) * (hw::kNameMax + sizeof(Meta));
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!p) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return false;
  }
  names_ = static_cast<char(*)[hw::kNameMax]>(p);
  meta_ = reinterpret_cast<Meta*>(static_cast<char*>(p) + static_cast<size_t>(cap_) * hw::kNameMax);
  clear();
  return true;
}

void FileList::free() {
  previewStop();
  heap_caps_free(names_);
  names_ = nullptr;
  meta_ = nullptr;
  count_ = sel_ = top_ = dragAcc_ = 0;
}

void FileList::clear() {
  previewStop();
  count_ = sel_ = top_ = dragAcc_ = 0;
}

bool FileList::add(Kind k, const char* name, uint8_t tag, int16_t index) {
  if (!names_ || count_ >= cap_) return false;
  strlcpy(names_[count_], name, hw::kNameMax);
  meta_[count_] = {k, tag, index};
  ++count_;
  return true;
}

void FileList::addFolder(const char* dir, const char* const* exts, int extCount, bool folders, uint8_t dirTag,
                         uint8_t fileTag) {
  if (!names_) return;
  int from = count_;
  if (folders) {
    count_ += hw::sdListDirs(dir, names_ + count_, cap_ - count_);
    for (int i = from; i < count_; ++i) meta_[i] = {Kind::Dir, dirTag, 0};
    from = count_;
  }
  if (extCount > 0) count_ += hw::sdListFiles(dir, exts, extCount, names_ + count_, cap_ - count_);
  for (int i = from; i < count_; ++i) meta_[i] = {Kind::File, fileTag, 0};
}

void FileList::addFiles(const char* dir, const char* ext, bool (*accept)(const char*), uint8_t tag) {
  if (!names_) return;
  const int from = count_;
  count_ += hw::sdList(dir, ext, names_ + count_, cap_ - count_, accept);
  for (int i = from; i < count_; ++i) meta_[i] = {Kind::File, tag, 0};
}

void FileList::finish(const char* cursor, bool dirOnly) {
  top_ = 0;
  dragAcc_ = 0;
  int row = count_ > 0 ? 1 : 0;
  for (int i = 0; cursor && cursor[0] && i < count_; ++i)
    if ((!dirOnly || meta_[i].kind == Kind::Dir) && strcasecmp(names_[i], cursor) == 0) {
      row = i + 1;
      break;
    }
  select(row);
}

void FileList::select(int row) {
  sel_ = row < 0 ? 0 : (row >= rows() ? rows() - 1 : row);
  top_ = mt::listKeepVisible(sel_, top_, kRows, rows());
}

void FileList::cursorName(char* out, size_t cap) const {
  if (!cap) return;
  if (sel_ >= 1 && sel_ <= count_) strlcpy(out, names_[sel_ - 1], cap);
  else out[0] = 0;
}

void FileList::moved() {
  if (sel_ != pvSel_) previewStop();
}

FileList::Act FileList::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: {
      const int prev = sel_;
      select(ev.shift ? mt::listPage(sel_, ev.delta, kRows, rows()) : mt::listMove(sel_, ev.delta, rows()));
      moved();
      return sel_ != prev ? Act::Moved : Act::None;
    }
    case InputType::EncClick: return sel_ == 0 ? Act::Back : Act::Open;
    case InputType::EncLong: return Act::Back;
    default: return Act::None;
  }
}

FileList::Act FileList::onTouch(const TouchEvent& ev, int top) {
  if (ev.type == TouchType::Drag) {
    dragAcc_ += ev.dy;
    const int r = dragAcc_ / kRowH;
    dragAcc_ -= r * kRowH;
    top_ = mt::listClampTop(top_ - r, kRows, rows());
    return Act::None;
  }
  dragAcc_ = 0;
  if (ev.type != TouchType::Tap || ev.y < top) return Act::None;
  const int row = top_ + (ev.y - top) / kRowH;
  if (row >= rows()) return Act::None;
  if (row == 0) {
    select(0);
    moved();
    return Act::Back;
  }
  // A file: the first tap selects (and previews), a tap on the selected one confirms.
  if (meta_[row - 1].kind == Kind::File && row != sel_) {
    const int prev = sel_;
    select(row);
    moved();
    if (pvDir_) previewToggle();
    return Act::Moved;
  }
  select(row);
  if (meta_[row - 1].kind != Kind::File) previewStop();
  return Act::Open;
}

bool FileList::onPlay() {
  if (!pvDir_) return false;
  // Folders and Back: nothing, but Play does not start the transport over the list either.
  if (sel_ >= 1 && sel_ <= count_ && meta_[sel_ - 1].kind == Kind::File) previewToggle();
  return true;
}

void FileList::previewToggle() {
  // Play again on the one still playing (by its length) stops it.
  const bool again = pvSel_ == sel_ && static_cast<int32_t>(pvEndMs_ - millis()) > 0;
  previewStop();
  if (again || !pvDir_ || sel_ < 1 || sel_ > count_) return;
  char path[192 + hw::kNameMax];
  const int n = snprintf(path, sizeof(path), "%s/%s", pvDir_, names_[sel_ - 1]);
  if (n < 0 || n >= static_cast<int>(sizeof(path))) {
    app_.toast("PATH TOO LONG");
    return;
  }
  uint32_t frames = 0, rate = 0;
  const audio::BankResult r = audio::previewFile(path, &frames, &rate);
  if (r != audio::BankResult::Ok) {
    app_.toast(audio::bankResultText(r));
    return;
  }
  pvSel_ = sel_;
  pvEndMs_ = millis() + static_cast<uint32_t>(static_cast<uint64_t>(frames) * 1000 / (rate ? rate : 1)) + 100;
}

void FileList::previewStop() {
  if (pvSel_ < 0) return;
  audio::previewStop();
  pvSel_ = -1;
}

void FileList::draw(LGFX_Sprite& s, int top) const {
  if (!names_) return;
  const int maxChars = (nameRight_ - 16) / kCharW;
  char shown[hw::kNameMax];
  for (int r = 0; r < kRows; ++r) {
    const int row = top_ + r;
    if (row >= rows()) break;
    const int ry = top + r * kRowH;
    const int ty = ry + (kRowH - kCharH) / 2;
    const bool sel = row == sel_;
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    if (row == 0) {
      s.setTextColor(sel ? kCursor : kDim);
      s.drawString(backTop_ ? "< Back" : "< Up (..)", 16, ty);
      continue;
    }
    const int i = row - 1;
    const bool dir = meta_[i].kind == Kind::Dir;
    s.setTextColor(sel ? kCursor : (color ? color(i) : kText));
    if (mark && mark(i)) s.drawString(">", 4, ty);
    mt::fitName(names_[i], maxChars - (dir ? 1 : 0), shown, sizeof(shown));
    s.drawString(shown, 16, ty);
    if (dir) s.drawString("/", 16 + static_cast<int>(strlen(shown)) * kCharW, ty);
  }
  s.setTextColor(kDim);
  if (count_ == 0) s.drawString(hw::sdReady() ? "EMPTY FOLDER" : "NO SD CARD", 16, top + kRowH + (kRowH - kCharH) / 2);
  if (top_ > 0) s.drawString("^", kMarkX, top + 4);
  if (top_ + kRows < rows()) s.drawString("v", kMarkX, top + (kRows - 1) * kRowH + 4);
}

void FileList::drawHeader(LGFX_Sprite& s, int y0, const char* title, const char* path, int right) {
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  s.drawString(title, 16, ty);
  const bool sd = hw::sdReady();
  const char* sdText = sd ? "SD OK" : "NO SD CARD";
  static constexpr int kSdChars = 10;  // room for the longer one: the path does not jump
  if (path && path[0]) {
    const int x = 16 + (static_cast<int>(strlen(title)) + 1) * kCharW;
    char tail[96];
    mt::fitTail(path, (right - kSdChars * kCharW - 8 - x) / kCharW, tail, sizeof(tail));
    s.drawString(tail, x, ty);
  }
  s.setTextColor(sd ? kDim : kEditCursor);
  s.drawString(sdText, right - static_cast<int>(strlen(sdText)) * kCharW, ty);
}

}  // namespace ui
