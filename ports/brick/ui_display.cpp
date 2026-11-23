#include "hw/lgfx_config.h"
#include "ui/opal_font.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fonts {
const lgfx::IFont AsciiFont8x16 = ui::kOpalFont;
const lgfx::IFont Font0{ui::kOpalFontBitmap, ui::kOpalFontInfo, 6, 8, 7};
}  // namespace fonts

bool LGFX::begin(bool fullscreen, bool headless) {
  composite_ = std::make_unique<LGFX_Sprite>(this);
  composite_->createSprite(width, height);
  lines_.resize(width * height);
  native_.resize(width * height);
  if (headless) return true;
  window_ = SDL_CreateWindow("MIDI Tracker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                             fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);
  if (!window_) return false;
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer_) return false;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  if (SDL_RenderSetLogicalSize(renderer_, width, height) != 0) return false;
  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, width, height);
  return texture_ != nullptr;
}
LGFX::~LGFX() {
  if (texture_) SDL_DestroyTexture(texture_);
  if (renderer_) SDL_DestroyRenderer(renderer_);
  if (window_) SDL_DestroyWindow(window_);
}
bool LGFX::getTouch(int32_t* x, int32_t* y) {
  *x = touchX_;
  *y = touchY_;
  return pressed_;
}
void LGFX::touch(int x, int y, bool pressed) {
  // SDL_RenderSetLogicalSize converts mouse events, including letterboxing.
  touchX_ = x;
  touchY_ = y;
  pressed_ = pressed;
}
void LGFX::present(const uint16_t* frame) {
  if (!frame) return;
  if (overlay) {
    auto* composite = static_cast<uint16_t*>(composite_->getBuffer());
    std::copy_n(frame, native_.size(), composite);
    overlay(*composite_);
    frame = composite;
  }
  for (size_t i = 0; i < native_.size(); ++i) {
    uint16_t v = SDL_SwapBE16(frame[i]);
    if (idleColor_)
      v = ((v & 0x8000) ? 0xf800 : 0) | ((v & 0x0400) ? 0x07e0 : 0) | ((v & 0x0010) ? 0x001f : 0);
    native_[i] = inverted_ ? static_cast<uint16_t>(~v) : v;
  }
  if (!texture_) return;
  SDL_UpdateTexture(texture_, nullptr, native_.data(), width * sizeof(uint16_t));
  SDL_RenderClear(renderer_);
  SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}
void LGFX::pushImageDMA(int x, int y, int w, int h, const lgfx::swap565_t* data) {
  if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > width || y + h > height) return;
  for (int row = 0; row < h; ++row) std::copy_n(data + row * w, w, lines_.data() + (row + y) * width + x);
}

void* LGFX_Sprite::createSprite(int w, int h) {
  width_ = w;
  height_ = h;
  pixels_.resize(w * h);
  clearClipRect();
  return pixels_.data();
}
void LGFX_Sprite::setClipRect(int x, int y, int w, int h) {
  SDL_Rect requested{x, y, w, h}, bounds{0, 0, width_, height_};
  SDL_IntersectRect(&requested, &bounds, &clip_);
}
void LGFX_Sprite::fillRect(int x, int y, int w, int h, uint16_t color) {
  SDL_Rect requested{x, y, w, h}, area{};
  if (!SDL_IntersectRect(&clip_, &requested, &area)) return;
  uint16_t* dst = pixels_.data() + area.y * width_ + area.x;
  const uint16_t value = SDL_SwapBE16(color);
  for (int row = 0; row < area.h; ++row) {
    std::fill_n(dst, area.w, value);
    if (row + 1 < area.h) dst += width_;
  }
}
void LGFX_Sprite::drawRect(int x, int y, int w, int h, uint16_t c) {
  drawFastHLine(x, y, w, c);
  drawFastHLine(x, y + h - 1, w, c);
  drawFastVLine(x, y, h, c);
  drawFastVLine(x + w - 1, y, h, c);
}
void LGFX_Sprite::drawLine(int x, int y, int x2, int y2, uint16_t c) {
  int dx = std::abs(x2 - x), dy = -std::abs(y2 - y), err = dx + dy;
  int sx = x < x2 ? 1 : -1, sy = y < y2 ? 1 : -1;
  for (;;) {
    fillRect(x, y, 1, 1, c);
    if (x == x2 && y == y2) return;
    const int twice = 2 * err;
    if (twice >= dy) {
      err += dy;
      x += sx;
    }
    if (twice <= dx) {
      err += dx;
      y += sy;
    }
  }
}
void LGFX_Sprite::drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  r = std::min({r, w / 2, h / 2});
  drawFastHLine(x + r, y, w - 2 * r, c);
  drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
  drawFastVLine(x, y + r, h - 2 * r, c);
  drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
  for (int t = 0; t <= r; ++t) {
    int u = static_cast<int>(std::lround(std::sqrt(r * r - t * t)));
    for (int sx : {-1, 1})
      for (int sy : {-1, 1}) {
        int cx = sx < 0 ? x + r : x + w - 1 - r, cy = sy < 0 ? y + r : y + h - 1 - r;
        fillRect(cx + sx * t, cy + sy * u, 1, 1, c);
        fillRect(cx + sx * u, cy + sy * t, 1, 1, c);
      }
  }
}
void LGFX_Sprite::fillCircle(int x, int y, int r, uint16_t c) {
  for (int dy = -r; dy <= r; ++dy) {
    int dx = static_cast<int>(std::sqrt(r * r - dy * dy));
    drawFastHLine(x - dx, y + dy, 2 * dx + 1, c);
  }
}
void LGFX_Sprite::fillTriangle(int x, int y, int x2, int y2, int x3, int y3, uint16_t c) {
  auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
  };
  for (int py = std::min({y, y2, y3}); py <= std::max({y, y2, y3}); ++py) {
    for (int px = std::min({x, x2, x3}); px <= std::max({x, x2, x3}); ++px) {
      int a = edge(x, y, x2, y2, px, py), b = edge(x2, y2, x3, y3, px, py), d = edge(x3, y3, x, y, px, py);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) fillRect(px, py, 1, 1, c);
    }
  }
}
void LGFX_Sprite::drawString(const char* text, int x, int y) {
  if (!text || !font_) return;
  if (textSize_ == 1 && font_->width == 8 && font_->height == 16) {
    if (clip_.w <= 0 || clip_.h <= 0) return;
    const int top = std::max(0, clip_.y - y);
    const int bottom = std::min(16, clip_.y + clip_.h - y);
    if (top >= bottom) return;
    const int clipRight = clip_.x + clip_.w;
    const uint16_t color = SDL_SwapBE16(textColor_);
    for (; *text && x < clipRight; ++text, x += 8) {
      const int left = std::max(0, clip_.x - x);
      const int right = std::min(8, clipRight - x);
      if (left >= right) continue;
      const unsigned char ch = static_cast<unsigned char>(*text);
      const uint8_t* glyph = font_->bitmap + 16 * ((ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32);
      for (int row = top; row < bottom; ++row) {
        const int bits = glyph[row];
        uint16_t* dst = pixels_.data() + ((y + row) * width_ + x + left);
        for (int col = left; col < right; ++col)
          if (bits & (0x80 >> col)) dst[col - left] = color;
      }
    }
    return;
  }
  for (; *text; ++text, x += font_->width * textSize_) {
    const unsigned char ch = static_cast<unsigned char>(*text);
    const uint8_t* glyph = font_->bitmap + 16 * ((ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32);
    for (int row = 0; row < font_->height; ++row) {
      const int bits = glyph[row * 16 / font_->height];
      for (int col = 0; col < font_->width; ++col) {
        if (bits & (0x80 >> (col * 8 / font_->width)))
          fillRect(x + col * textSize_, y + row * textSize_, textSize_, textSize_, textColor_);
      }
    }
  }
}
