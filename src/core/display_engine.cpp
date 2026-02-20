#include "core/display_engine.h"

#include <Adafruit_GFX.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>

namespace core {

namespace {

// MatrixPortal S3 HUB75 pins.
constexpr int8_t kR1Pin = 42;
constexpr int8_t kG1Pin = 40;
constexpr int8_t kB1Pin = 41;
constexpr int8_t kR2Pin = 38;
constexpr int8_t kG2Pin = 37;
constexpr int8_t kB2Pin = 39;
constexpr int8_t kAPin = 45;
constexpr int8_t kBPin = 36;
constexpr int8_t kCPin = 48;
constexpr int8_t kDPin = 35;
constexpr int8_t kEPin = 21;
constexpr int8_t kLatPin = 47;
constexpr int8_t kOePin = 14;
constexpr int8_t kClkPin = 2;

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
  const uint16_t logicalPanelCol = static_cast<uint16_t>(x) / cfg.panelWidth;

  uint16_t physicalPanelCol = logicalPanelCol;
  if ((panelRow & 1U) == 1U) {
    physicalPanelCol = static_cast<uint16_t>((cfg.panelCols - 1) - logicalPanelCol);
  }

  const uint16_t panelIndex = panelRow * cfg.panelCols + physicalPanelCol;

  return {
      true,
      panelIndex,
      static_cast<uint16_t>(x - logicalPanelCol * cfg.panelWidth),
      static_cast<uint16_t>(y - panelRow * cfg.panelHeight),
  };
}

DisplayEngine::DisplayEngine()
    : config_{1, 2, 64, 32, 80, false, true},
      geometry_{128, 32},
      ready_(false),
      matrix_(nullptr),
      virtualMatrix_(nullptr),
      canvas_(nullptr),
      linearMapper_(),
      serpentineMapper_(),
      mapper_(&linearMapper_) {}

DisplayEngine::~DisplayEngine() { end(); }

bool DisplayEngine::begin(const DisplayConfig &config) {
  end();

  DisplayGeometry geom{};
  if (!compute_geometry(config, geom)) {
    Serial.println("[DISPLAY] Invalid geometry config");
    return false;
  }

  config_ = config;
  geometry_ = geom;
  mapper_ = config_.serpentine ? static_cast<const IPanelMapper *>(&serpentineMapper_)
                               : static_cast<const IPanelMapper *>(&linearMapper_);

  HUB75_I2S_CFG::i2s_pins pins = {
      kR1Pin, kG1Pin, kB1Pin, kR2Pin, kG2Pin, kB2Pin, kAPin,
      kBPin,  kCPin,  kDPin,  kEPin,  kLatPin, kOePin, kClkPin,
  };

  const uint16_t chainLength = static_cast<uint16_t>(config_.panelRows) * config_.panelCols;
  HUB75_I2S_CFG mxConfig(config_.panelWidth,
                         config_.panelHeight,
                         chainLength,
                         pins,
                         HUB75_I2S_CFG::SHIFTREG,
                         HUB75_I2S_CFG::TYPE138,
                         config_.doubleBuffered,
                         HUB75_I2S_CFG::HZ_8M,
                         1,
                         true,
                         60);

  matrix_ = new MatrixPanel_I2S_DMA(mxConfig);
  if (!matrix_ || !matrix_->begin()) {
    Serial.println("[DISPLAY] Matrix initialization failed");
    end();
    return false;
  }

  const PANEL_CHAIN_TYPE chainType = config_.serpentine ? CHAIN_TOP_LEFT_DOWN_ZZ : CHAIN_TOP_LEFT_DOWN;
  virtualMatrix_ = new VirtualMatrixPanel(*matrix_, config_.panelRows, config_.panelCols, config_.panelWidth,
                                          config_.panelHeight, chainType);

  if (!virtualMatrix_) {
    Serial.println("[DISPLAY] Virtual matrix initialization failed");
    end();
    return false;
  }

  canvas_ = virtualMatrix_;
  matrix_->setBrightness8(config_.brightness);
  canvas_->setTextWrap(false);
  canvas_->setTextSize(1);
  canvas_->fillScreen(0);

  ready_ = true;
  Serial.printf("[DISPLAY] Ready %ux%u (%ux%u panels)\n", geometry_.totalWidth, geometry_.totalHeight,
                config_.panelCols, config_.panelRows);
  return true;
}

void DisplayEngine::end() {
  ready_ = false;

  if (virtualMatrix_) {
    delete virtualMatrix_;
    virtualMatrix_ = nullptr;
  }

  canvas_ = nullptr;

  if (matrix_) {
    delete matrix_;
    matrix_ = nullptr;
  }
}

bool DisplayEngine::is_ready() const { return ready_; }

const DisplayConfig &DisplayEngine::config() const { return config_; }

const DisplayGeometry &DisplayEngine::geometry() const { return geometry_; }

void DisplayEngine::set_brightness(uint8_t brightness) {
  config_.brightness = brightness;
  if (matrix_) {
    matrix_->setBrightness8(brightness);
  }
}

bool DisplayEngine::begin_frame() { return ready_ && canvas_ != nullptr; }

void DisplayEngine::clear(uint16_t color) {
  if (!canvas_) {
    return;
  }
  canvas_->fillScreen(color);
}

void DisplayEngine::draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size, uint16_t bg) {
  if (!canvas_ || !text) {
    return;
  }
  canvas_->setTextWrap(false);
  canvas_->setTextSize(size);
  canvas_->setTextColor(color, bg);
  canvas_->setCursor(x, y);
  canvas_->print(text);
}

void DisplayEngine::draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!canvas_) {
    return;
  }
  canvas_->drawRect(x, y, w, h, color);
}

void DisplayEngine::fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!canvas_) {
    return;
  }
  canvas_->fillRect(x, y, w, h, color);
}

void DisplayEngine::draw_pixel(int16_t x, int16_t y, uint16_t color) {
  if (!canvas_) {
    return;
  }
  canvas_->drawPixel(x, y, color);
}

void DisplayEngine::draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (!canvas_ || w <= 0) {
    return;
  }
  canvas_->drawFastHLine(x, y, w, color);
}

display::TextMetrics DisplayEngine::measure_text(const char *text, uint8_t size) {
  display::TextMetrics tm{};
  if (!canvas_ || !text) {
    return tm;
  }

  canvas_->setTextSize(size);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  canvas_->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  tm.xOffset = x1;
  tm.yOffset = y1;
  tm.width = static_cast<int16_t>(w);
  tm.height = static_cast<int16_t>(h);
  return tm;
}

bool DisplayEngine::present() {
  if (!ready_ || !matrix_) {
    return false;
  }
  matrix_->flipDMABuffer();
  return true;
}

uint16_t DisplayEngine::color565(uint8_t r, uint8_t g, uint8_t b) const {
  if (!matrix_) {
    return 0;
  }
  return matrix_->color565(r, g, b);
}

}  // namespace core
