#pragma once

#include <stdint.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "core/models.h"
#include "display/DisplayEngine.h"

class VirtualMatrixPanel;
class Adafruit_GFX;

namespace core {

struct LogicalPoint {
  int16_t x;
  int16_t y;
};

struct PhysicalPoint {
  bool valid;
  uint16_t panelIndex;
  uint16_t localX;
  uint16_t localY;
};

class IPanelMapper {
 public:
  virtual ~IPanelMapper() = default;
  virtual PhysicalPoint map(const DisplayConfig &cfg, int16_t x, int16_t y) const = 0;
};

class LinearPanelMapper final : public IPanelMapper {
 public:
  PhysicalPoint map(const DisplayConfig &cfg, int16_t x, int16_t y) const override;
};

class SerpentinePanelMapper final : public IPanelMapper {
 public:
  PhysicalPoint map(const DisplayConfig &cfg, int16_t x, int16_t y) const override;
};

class DisplayEngine final : public display::DisplayEngine {
 public:
  DisplayEngine();
  ~DisplayEngine();

  bool begin(const DisplayConfig &config);
  void end();

  bool is_ready() const;
  const DisplayConfig &config() const;
  const DisplayGeometry &geometry() const;

  void set_brightness(uint8_t brightness);
  bool begin_frame();
  void clear(uint16_t color);
  void draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size = 1, uint16_t bg = 0) override;
  void draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void draw_pixel(int16_t x, int16_t y, uint16_t color) override;
  void draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  display::TextMetrics measure_text(const char *text, uint8_t size) override;
  bool present();

  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) const;

 private:
  DisplayConfig config_;
  DisplayGeometry geometry_;
  bool ready_;

  MatrixPanel_I2S_DMA *matrix_;
  VirtualMatrixPanel *virtualMatrix_;
  Adafruit_GFX *canvas_;

  LinearPanelMapper linearMapper_;
  SerpentinePanelMapper serpentineMapper_;
  const IPanelMapper *mapper_;
};

}  // namespace core
