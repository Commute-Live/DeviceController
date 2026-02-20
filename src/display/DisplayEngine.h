#pragma once

#include <stdint.h>

namespace display {

struct TextMetrics {
  int16_t xOffset;
  int16_t yOffset;
  int16_t width;
  int16_t height;
};

class DisplayEngine {
 public:
  virtual ~DisplayEngine() = default;

  virtual void draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size, uint16_t bg) = 0;
  virtual void draw_text_transparent(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size) = 0;
  virtual void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) = 0;
  virtual void draw_pixel(int16_t x, int16_t y, uint16_t color) = 0;
  virtual void draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) = 0;
  virtual TextMetrics measure_text(const char *text, uint8_t size) = 0;
};

}  // namespace display
