#pragma once
#include "rg35xxsp/display_profile.h"
#include <SDL.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lgfx {
struct IFont {
  const uint8_t* bitmap;
  const uint8_t* info;
  int width, height, baseline;
};
using FixedBMPfont = IFont;
using swap565_t = uint16_t;
}  // namespace lgfx
namespace fonts {
extern const lgfx::IFont AsciiFont8x16;
extern const lgfx::IFont Font0;
}  // namespace fonts

// The firmware sprite stores RGB565 in panel byte order. PERF reads these
// bytes directly, transforms each line, then sends the lines back to the LCD.
class LGFX_Sprite;
class LGFX {
 public:
  bool begin(bool fullscreen, bool headless = false);
  ~LGFX();
  std::function<void(LGFX_Sprite&)> overlay;
  bool getTouch(int32_t* x, int32_t* y);
  void touch(int x, int y, bool pressed);
  void present(const uint16_t* frame, const uint16_t* sharp = nullptr);
  void startWrite() {}
  void endWrite() { present(lines_.data()); }
  void pushImageDMA(int x, int y, int width, int height, const lgfx::swap565_t* data);
  void invertDisplay(bool on) { inverted_ = on; }
  void writeCommand(int command) { idleColor_ = command == 0x39; }
  static constexpr int width = 480, height = 320;
  static constexpr int sharpWidth = rg35xxsp::kDisplayWidth, sharpHeight = rg35xxsp::kRasterHeight;
  bool sharpText() const { return sharpTexture_ != nullptr; }

 private:
  bool createTextures(bool sharpRequested);
  void copyFrame(bool sharp);
  std::unique_ptr<LGFX_Sprite> composite_;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  SDL_Texture* sharpTexture_ = nullptr;
  std::vector<uint16_t> lines_, native_;
  bool inverted_ = false, idleColor_ = false, pressed_ = false;
  int touchX_ = 0, touchY_ = 0;
};

class LGFX_Sprite {
 public:
  explicit LGFX_Sprite(LGFX* display) : display_(display) {}
  void setPsram(bool) {}
  void setColorDepth(int) {}
  void* createSprite(int w, int h);
  void* getBuffer() { return pixels_.data(); }
  void pushSprite(int, int) { display_->present(pixels_.data(), sharpPixels_.empty() ? nullptr : sharpPixels_.data()); }
  void fillScreen(uint16_t color) { fillRect(0, 0, width_, height_, color); }
  void setClipRect(int x, int y, int w, int h);
  void clearClipRect() { clip_ = {0, 0, width_, height_}; }
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t color);
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color);
  void drawLine(int x, int y, int x2, int y2, uint16_t color);
  void fillTriangle(int x, int y, int x2, int y2, int x3, int y3, uint16_t color);
  void fillCircle(int x, int y, int r, uint16_t color);
  void setTextColor(uint16_t color) { textColor_ = color; }
  void setTextSize(int size) { textSize_ = size; }
  void setFont(const lgfx::IFont* font) { font_ = font; }
  void drawString(const char* text, int x, int y);

 private:
  friend class LGFX;
  void drawSharpString(const char* text, int x, int y, uint16_t color);
  LGFX* display_;
  std::vector<uint16_t> pixels_, sharpPixels_;
  int width_ = 0, height_ = 0, textSize_ = 1;
  uint16_t textColor_ = 0xffff;
  const lgfx::IFont* font_ = &fonts::AsciiFont8x16;
  SDL_Rect clip_{};
};
