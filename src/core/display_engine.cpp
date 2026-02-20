#include "core/display_engine.h"

#include <string.h>

namespace core {

namespace {
bool in_bounds(const DisplayConfig &cfg, int16_t x, int16_t y) {
  DisplayGeometry geom{};
  if (!compute_geometry(cfg, geom)) {
    return false;
  }
  return x >= 0 && y >= 0 && x < static_cast<int16_t>(geom.totalWidth) &&
         y < static_cast<int16_t>(geom.totalHeight);
}
}  // namespace

PhysicalPoint LinearPanelMapper::map(const DisplayConfig &cfg, int16_t x, int16_t y) const {
  if (!in_bounds(cfg, x, y)) {
    return {false, 0, 0, 0};
  }
  const uint16_t panelCol = static_cast<uint16_t>(x) / cfg.panelWidth;
  const uint16_t panelRow = static_cast<uint16_t>(y) / cfg.panelHeight;
  const uint16_t panelIndex = panelRow * cfg.panelCols + panelCol;
  return {
      true,
      panelIndex,
      static_cast<uint16_t>(x - panelCol * cfg.panelWidth),
      static_cast<uint16_t>(y - panelRow * cfg.panelHeight),
  };
}

PhysicalPoint SerpentinePanelMapper::map(const DisplayConfig &cfg, int16_t x, int16_t y) const {
  if (!in_bounds(cfg, x, y)) {
    return {false, 0, 0, 0};
  }
  const uint16_t panelRow = static_cast<uint16_t>(y) / cfg.panelHeight;
  uint16_t panelCol = static_cast<uint16_t>(x) / cfg.panelWidth;

  if ((panelRow & 1U) == 1U) {
    panelCol = static_cast<uint16_t>((cfg.panelCols - 1) - panelCol);
  }

  const uint16_t panelIndex = panelRow * cfg.panelCols + panelCol;
  return {
      true,
      panelIndex,
      static_cast<uint16_t>(x % cfg.panelWidth),
      static_cast<uint16_t>(y % cfg.panelHeight),
  };
}

DisplayEngine::DisplayEngine()
    : config_{1, 2, 64, 32, 80, false, true},
      geometry_{128, 32},
      ready_(false),
      linearMapper_(),
      serpentineMapper_(),
      mapper_(&linearMapper_) {}

bool DisplayEngine::begin(const DisplayConfig &config) {
  DisplayGeometry geom{};
  if (!compute_geometry(config, geom)) {
    return false;
  }
  config_ = config;
  geometry_ = geom;
  mapper_ = config_.serpentine ? static_cast<const IPanelMapper *>(&serpentineMapper_)
                               : static_cast<const IPanelMapper *>(&linearMapper_);
  ready_ = true;
  return true;
}

void DisplayEngine::end() { ready_ = false; }

bool DisplayEngine::is_ready() const { return ready_; }

const DisplayConfig &DisplayEngine::config() const { return config_; }

const DisplayGeometry &DisplayEngine::geometry() const { return geometry_; }

void DisplayEngine::set_brightness(uint8_t brightness) { config_.brightness = brightness; }

bool DisplayEngine::begin_frame() { return ready_; }

void DisplayEngine::clear(uint16_t color) {
  (void)color;
}

void DisplayEngine::draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size) {
  (void)x;
  (void)y;
  (void)text;
  (void)color;
  (void)size;
}

void DisplayEngine::draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)color;
}

void DisplayEngine::fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)color;
}

bool DisplayEngine::present() { return ready_; }

}  // namespace core
