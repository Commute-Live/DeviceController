#pragma once

#include <stdint.h>
#include "core/models.h"

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

class DisplayEngine final {
 public:
  DisplayEngine();

  bool begin(const DisplayConfig &config);
  void end();

  bool is_ready() const;
  const DisplayConfig &config() const;
  const DisplayGeometry &geometry() const;

  void set_brightness(uint8_t brightness);
  bool begin_frame();
  void clear(uint16_t color);
  void draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size = 1);
  void draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  bool present();

 private:
  DisplayConfig config_;
  DisplayGeometry geometry_;
  bool ready_;
  LinearPanelMapper linearMapper_;
  SerpentinePanelMapper serpentineMapper_;
  const IPanelMapper *mapper_;
};

}  // namespace core
