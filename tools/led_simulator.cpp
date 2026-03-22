// Live LED Matrix Simulator — SDL2 window showing exactly what the 128x32 HUB75 matrix displays.
// Uses the same LayoutEngine, BadgeRenderer, and MtaColorMap as the real firmware.
//
// Build:  make -C tools simulator
// Run:    ./tools/led_simulator [--scale 10] [--scenario transit]
//         ./tools/led_simulator --mqtt --device-id YOUR_DEVICE_ID
// Keys:   1-5 = display type, R = cycle rows, S = cycle scenario,
//         Left/Right = cycle route1, Q = quit

#include <SDL.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#ifdef HAS_MOSQUITTO
#include <mosquitto.h>
#endif

#include "core/layout_engine.h"
#include "display/badge_renderer.h"

namespace {

// ── Hardware config (mirrors main.cpp setup) ────────────────────────────────
// Panel: 1 row x 2 cols of 64x32 HUB75 panels = 128x32 total
// Chain: linear (top-left-down), no serpentine
// Driver: ESP32-HUB75-MatrixPanel-I2S-DMA, SHIFTREG, TYPE138
// Brightness: 32/255 (~12.5%)
constexpr int kPanelRows = 1;
constexpr int kPanelCols = 2;
constexpr int kPanelWidth = 64;
constexpr int kPanelHeight = 32;
constexpr int kMatrixWidth = kPanelCols * kPanelWidth;   // 128
constexpr int kMatrixHeight = kPanelRows * kPanelHeight;  // 32
constexpr uint8_t kHwBrightness = 32;  // device default brightness (0-255)
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint8_t kTextSizeTiny = 0;
constexpr uint8_t kTextSizeTinyPlus = 255;
constexpr int kDefaultScale = 10;
constexpr int kLedGapPx = 1;  // dark gap between LED dots (in output pixels)

// ── Options ──────────────────────────────────────────────────────────────────

struct SimOptions {
  int scale = kDefaultScale;
  int displayType = 1;
  int rows = 2;
  int scenarioIndex = 0;
  bool ledDots = true;  // draw round LED dots (vs flat pixels)

  // MQTT live mode
  bool mqttEnabled = false;
  std::string mqttHost = "localhost";
  int mqttPort = 1883;
  std::string mqttUser;
  std::string mqttPass;
  std::string deviceId;
};

// ── Font glyph tables (identical to led_preview.cpp / firmware) ─────────────

struct Glyph {
  uint8_t width;
  uint8_t height;
  const uint8_t *rows;
};

template <size_t N>
void copy_cstr(char (&dst)[N], const std::string &src) {
  strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0';
}

template <size_t N>
void copy_cstr(char (&dst)[N], const char *src) {
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, N - 1);
  dst[N - 1] = '\0';
}

Glyph regular_glyph(char c) {
  static constexpr uint8_t kSpace[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kDash[] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
  static constexpr uint8_t kColon[] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
  static constexpr uint8_t kComma[] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08};
  static constexpr uint8_t kPeriod[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
  static constexpr uint8_t kSlash[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
  static constexpr uint8_t kApostrophe[] = {0x0C, 0x0C, 0x08, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t k0[] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
  static constexpr uint8_t k1[] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
  static constexpr uint8_t k2[] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F};
  static constexpr uint8_t k3[] = {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E};
  static constexpr uint8_t k4[] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
  static constexpr uint8_t k5[] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
  static constexpr uint8_t k6[] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
  static constexpr uint8_t k7[] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
  static constexpr uint8_t k8[] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
  static constexpr uint8_t k9[] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
  static constexpr uint8_t kA[] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static constexpr uint8_t kB[] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
  static constexpr uint8_t kC[] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
  static constexpr uint8_t kD[] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
  static constexpr uint8_t kE[] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
  static constexpr uint8_t kF[] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
  static constexpr uint8_t kG[] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
  static constexpr uint8_t kH[] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static constexpr uint8_t kI[] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
  static constexpr uint8_t kJ[] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
  static constexpr uint8_t kK[] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
  static constexpr uint8_t kL[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
  static constexpr uint8_t kM[] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
  static constexpr uint8_t kN[] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
  static constexpr uint8_t kO[] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
  static constexpr uint8_t kP[] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
  static constexpr uint8_t kQ[] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
  static constexpr uint8_t kR[] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
  static constexpr uint8_t kS[] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
  static constexpr uint8_t kT[] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
  static constexpr uint8_t kU[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
  static constexpr uint8_t kV[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
  static constexpr uint8_t kW[] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
  static constexpr uint8_t kX[] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
  static constexpr uint8_t kY[] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
  static constexpr uint8_t kZ[] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};

  // Lowercase letter glyphs (Adafruit GFX default 5x7 font)
  static constexpr uint8_t ka[] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F};
  static constexpr uint8_t kb[] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E};
  static constexpr uint8_t kc[] = {0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E};
  static constexpr uint8_t kd[] = {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F};
  static constexpr uint8_t ke[] = {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E};
  static constexpr uint8_t kf[] = {0x06, 0x08, 0x08, 0x1E, 0x08, 0x08, 0x08};
  static constexpr uint8_t kg[] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E};
  static constexpr uint8_t kh[] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11};
  static constexpr uint8_t ki[] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E};
  static constexpr uint8_t kj[] = {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C};
  static constexpr uint8_t kk[] = {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12};
  static constexpr uint8_t kl[] = {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
  static constexpr uint8_t km[] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11};
  static constexpr uint8_t kn[] = {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11};
  static constexpr uint8_t ko[] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E};
  static constexpr uint8_t kp[] = {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10};
  static constexpr uint8_t kq[] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01};
  static constexpr uint8_t kr[] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10};
  static constexpr uint8_t ks[] = {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E};
  static constexpr uint8_t kt[] = {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06};
  static constexpr uint8_t ku[] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D};
  static constexpr uint8_t kv[] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04};
  static constexpr uint8_t kw[] = {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A};
  static constexpr uint8_t kx[] = {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11};
  static constexpr uint8_t ky[] = {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E};
  static constexpr uint8_t kz[] = {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F};

  switch (c) {
    case ' ': return {3,7,kSpace}; case '-': return {5,7,kDash}; case ':': return {3,7,kColon};
    case ',': return {3,7,kComma}; case '.': return {2,7,kPeriod}; case '/': return {5,7,kSlash};
    case '\'': return {2,7,kApostrophe};
    case '0': return {5,7,k0}; case '1': return {5,7,k1}; case '2': return {5,7,k2};
    case '3': return {5,7,k3}; case '4': return {5,7,k4}; case '5': return {5,7,k5};
    case '6': return {5,7,k6}; case '7': return {5,7,k7}; case '8': return {5,7,k8};
    case '9': return {5,7,k9};
    case 'A': return {5,7,kA}; case 'B': return {5,7,kB}; case 'C': return {5,7,kC};
    case 'D': return {5,7,kD}; case 'E': return {5,7,kE}; case 'F': return {5,7,kF};
    case 'G': return {5,7,kG}; case 'H': return {5,7,kH}; case 'I': return {5,7,kI};
    case 'J': return {5,7,kJ}; case 'K': return {5,7,kK}; case 'L': return {5,7,kL};
    case 'M': return {5,7,kM}; case 'N': return {5,7,kN}; case 'O': return {5,7,kO};
    case 'P': return {5,7,kP}; case 'Q': return {5,7,kQ}; case 'R': return {5,7,kR};
    case 'S': return {5,7,kS}; case 'T': return {5,7,kT}; case 'U': return {5,7,kU};
    case 'V': return {5,7,kV}; case 'W': return {5,7,kW}; case 'X': return {5,7,kX};
    case 'Y': return {5,7,kY}; case 'Z': return {5,7,kZ};
    case 'a': return {5,7,ka}; case 'b': return {5,7,kb}; case 'c': return {5,7,kc};
    case 'd': return {5,7,kd}; case 'e': return {5,7,ke}; case 'f': return {5,7,kf};
    case 'g': return {5,7,kg}; case 'h': return {5,7,kh}; case 'i': return {5,7,ki};
    case 'j': return {5,7,kj}; case 'k': return {5,7,kk}; case 'l': return {5,7,kl};
    case 'm': return {5,7,km}; case 'n': return {5,7,kn}; case 'o': return {5,7,ko};
    case 'p': return {5,7,kp}; case 'q': return {5,7,kq}; case 'r': return {5,7,kr};
    case 's': return {5,7,ks}; case 't': return {5,7,kt}; case 'u': return {5,7,ku};
    case 'v': return {5,7,kv}; case 'w': return {5,7,kw}; case 'x': return {5,7,kx};
    case 'y': return {5,7,ky}; case 'z': return {5,7,kz};
    default: return {5,7,kSpace};
  }
}

Glyph tiny_glyph(char c) {
  static constexpr uint8_t kSpace[] = {0x0,0x0,0x0,0x0,0x0};
  static constexpr uint8_t kDash[] = {0x0,0x0,0x7,0x0,0x0};
  static constexpr uint8_t kColon[] = {0x0,0x2,0x0,0x2,0x0};
  static constexpr uint8_t kComma[] = {0x0,0x0,0x0,0x6,0x2};
  static constexpr uint8_t kPeriod[] = {0x0,0x0,0x0,0x0,0x2};
  static constexpr uint8_t kSlash[] = {0x1,0x1,0x2,0x4,0x4};
  static constexpr uint8_t k0[] = {0x7,0x5,0x5,0x5,0x7};
  static constexpr uint8_t k1[] = {0x2,0x6,0x2,0x2,0x7};
  static constexpr uint8_t k2[] = {0x7,0x1,0x7,0x4,0x7};
  static constexpr uint8_t k3[] = {0x7,0x1,0x3,0x1,0x7};
  static constexpr uint8_t k4[] = {0x5,0x5,0x7,0x1,0x1};
  static constexpr uint8_t k5[] = {0x7,0x4,0x7,0x1,0x7};
  static constexpr uint8_t k6[] = {0x3,0x4,0x7,0x5,0x7};
  static constexpr uint8_t k7[] = {0x7,0x1,0x2,0x2,0x2};
  static constexpr uint8_t k8[] = {0x7,0x5,0x7,0x5,0x7};
  static constexpr uint8_t k9[] = {0x7,0x5,0x7,0x1,0x6};
  static constexpr uint8_t kA[] = {0x2,0x5,0x7,0x5,0x5};
  static constexpr uint8_t kB[] = {0x6,0x5,0x6,0x5,0x6};
  static constexpr uint8_t kC[] = {0x3,0x4,0x4,0x4,0x3};
  static constexpr uint8_t kD[] = {0x6,0x5,0x5,0x5,0x6};
  static constexpr uint8_t kE[] = {0x7,0x4,0x6,0x4,0x7};
  static constexpr uint8_t kF[] = {0x7,0x4,0x6,0x4,0x4};
  static constexpr uint8_t kG[] = {0x3,0x4,0x5,0x5,0x3};
  static constexpr uint8_t kH[] = {0x5,0x5,0x7,0x5,0x5};
  static constexpr uint8_t kI[] = {0x7,0x2,0x2,0x2,0x7};
  static constexpr uint8_t kJ[] = {0x1,0x1,0x1,0x5,0x2};
  static constexpr uint8_t kK[] = {0x5,0x5,0x6,0x5,0x5};
  static constexpr uint8_t kL[] = {0x4,0x4,0x4,0x4,0x7};
  static constexpr uint8_t kM[] = {0x5,0x7,0x7,0x5,0x5};
  static constexpr uint8_t kN[] = {0x5,0x7,0x7,0x7,0x5};
  static constexpr uint8_t kO[] = {0x2,0x5,0x5,0x5,0x2};
  static constexpr uint8_t kP[] = {0x6,0x5,0x6,0x4,0x4};
  static constexpr uint8_t kQ[] = {0x2,0x5,0x5,0x3,0x1};
  static constexpr uint8_t kR[] = {0x6,0x5,0x6,0x5,0x5};
  static constexpr uint8_t kS[] = {0x3,0x4,0x2,0x1,0x6};
  static constexpr uint8_t kT[] = {0x7,0x2,0x2,0x2,0x2};
  static constexpr uint8_t kU[] = {0x5,0x5,0x5,0x5,0x7};
  static constexpr uint8_t kV[] = {0x5,0x5,0x5,0x5,0x2};
  static constexpr uint8_t kW[] = {0x5,0x5,0x7,0x7,0x5};
  static constexpr uint8_t kX[] = {0x5,0x5,0x2,0x5,0x5};
  static constexpr uint8_t kY[] = {0x5,0x5,0x2,0x2,0x2};
  static constexpr uint8_t kZ[] = {0x7,0x1,0x2,0x4,0x7};

  if (c >= 'a' && c <= 'z') c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

  switch (c) {
    case ' ': return {3,5,kSpace}; case '-': return {3,5,kDash}; case ':': return {1,5,kColon};
    case ',': return {2,5,kComma}; case '.': return {1,5,kPeriod}; case '/': return {3,5,kSlash};
    case '0': return {3,5,k0}; case '1': return {3,5,k1}; case '2': return {3,5,k2};
    case '3': return {3,5,k3}; case '4': return {3,5,k4}; case '5': return {3,5,k5};
    case '6': return {3,5,k6}; case '7': return {3,5,k7}; case '8': return {3,5,k8};
    case '9': return {3,5,k9};
    case 'A': return {3,5,kA}; case 'B': return {3,5,kB}; case 'C': return {3,5,kC};
    case 'D': return {3,5,kD}; case 'E': return {3,5,kE}; case 'F': return {3,5,kF};
    case 'G': return {3,5,kG}; case 'H': return {3,5,kH}; case 'I': return {3,5,kI};
    case 'J': return {3,5,kJ}; case 'K': return {3,5,kK}; case 'L': return {3,5,kL};
    case 'M': return {3,5,kM}; case 'N': return {3,5,kN}; case 'O': return {3,5,kO};
    case 'P': return {3,5,kP}; case 'Q': return {3,5,kQ}; case 'R': return {3,5,kR};
    case 'S': return {3,5,kS}; case 'T': return {3,5,kT}; case 'U': return {3,5,kU};
    case 'V': return {3,5,kV}; case 'W': return {3,5,kW}; case 'X': return {3,5,kX};
    case 'Y': return {3,5,kY}; case 'Z': return {3,5,kZ};
    default: return {3,5,kSpace};
  }
}

// ── Host DisplayEngine (renders into pixel buffer) ──────────────────────────

class SimDisplayEngine final : public display::DisplayEngine {
 public:
  SimDisplayEngine(int width, int height)
      : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height, kColorBlack) {}

  void clear(uint16_t color) { std::fill(pixels_.begin(), pixels_.end(), color); }

  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<uint16_t> &pixels() const { return pixels_; }

  void draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size, uint16_t bg) override {
    draw_text_internal(x, y, text, color, size, false, bg);
  }

  void draw_text_transparent(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size) override {
    draw_text_internal(x, y, text, color, size, true, 0);
  }

  void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    for (int16_t yy = 0; yy < h; ++yy)
      for (int16_t xx = 0; xx < w; ++xx)
        draw_pixel(static_cast<int16_t>(x + xx), static_cast<int16_t>(y + yy), color);
  }

  void draw_pixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pixels_[static_cast<size_t>(y) * width_ + x] = color;
  }

  void draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    for (int16_t xx = 0; xx < w; ++xx)
      draw_pixel(static_cast<int16_t>(x + xx), y, color);
  }

  // Compute tight bounding box of a glyph's set pixels (matches Adafruit GFX getTextBounds).
  static void glyph_tight_bounds(const Glyph &g, int &minCol, int &maxCol, int &minRow, int &maxRow) {
    minCol = g.width; maxCol = -1; minRow = g.height; maxRow = -1;
    for (int row = 0; row < g.height; ++row)
      for (int col = 0; col < g.width; ++col)
        if (g.rows[row] & (1U << (g.width - 1 - col))) {
          if (col < minCol) minCol = col;
          if (col > maxCol) maxCol = col;
          if (row < minRow) minRow = row;
          if (row > maxRow) maxRow = row;
        }
  }

  display::TextMetrics measure_text(const char *text, uint8_t size) override {
    display::TextMetrics metrics{};
    if (!text || text[0] == '\0') return metrics;

    const bool tiny = (size == kTextSizeTiny || size == kTextSizeTinyPlus);
    const bool embolden = (size == kTextSizeTinyPlus);

    // For single characters (badge text), return tight bounds like Adafruit getTextBounds.
    // For multi-char strings, accumulate cursor-advance widths.
    const int16_t spacing = tiny ? 1 : static_cast<int16_t>(size);
    int16_t cursorX = 0;
    int16_t globalMinX = 32767;
    int16_t globalMaxX = -1;
    int16_t globalMinY = 32767;
    int16_t globalMaxY = -1;

    for (size_t i = 0; text[i] != '\0'; ++i) {
      const Glyph glyph = tiny ? tiny_glyph(text[i]) : regular_glyph(text[i]);
      int minC, maxC, minR, maxR;
      glyph_tight_bounds(glyph, minC, maxC, minR, maxR);

      if (maxC >= 0) {
        int16_t scale = tiny ? 1 : static_cast<int16_t>(size);
        int16_t px0 = static_cast<int16_t>(cursorX + minC * scale);
        int16_t px1 = static_cast<int16_t>(cursorX + (maxC + 1) * scale - 1);
        int16_t py0 = static_cast<int16_t>(minR * scale);
        int16_t py1 = static_cast<int16_t>((maxR + 1) * scale - 1);
        if (px0 < globalMinX) globalMinX = px0;
        if (px1 > globalMaxX) globalMaxX = px1;
        if (py0 < globalMinY) globalMinY = py0;
        if (py1 > globalMaxY) globalMaxY = py1;
      }

      if (tiny) {
        cursorX = static_cast<int16_t>(cursorX + glyph.width + spacing);
      } else {
        cursorX = static_cast<int16_t>(cursorX + glyph.width * size + spacing);
      }
    }

    if (globalMaxX < 0) return metrics;  // no pixels at all

    metrics.xOffset = globalMinX;
    metrics.yOffset = globalMinY;
    metrics.width = static_cast<int16_t>(globalMaxX - globalMinX + 1);
    metrics.height = static_cast<int16_t>(globalMaxY - globalMinY + 1);
    if (embolden) ++metrics.width;
    return metrics;
  }

 private:
  void draw_text_internal(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size, bool transparent, uint16_t bg) {
    if (!text) return;
    const bool tiny = (size == kTextSizeTiny || size == kTextSizeTinyPlus);
    const bool embolden = (size == kTextSizeTinyPlus);
    const int16_t spacing = tiny ? 1 : static_cast<int16_t>(size);
    int16_t cursorX = x;
    for (size_t i = 0; text[i] != '\0'; ++i) {
      if (tiny) {
        draw_tiny_char(cursorX, y, text[i], color, transparent, bg, embolden);
        cursorX = static_cast<int16_t>(cursorX + tiny_glyph(text[i]).width + spacing);
      } else {
        draw_regular_char(cursorX, y, text[i], color, transparent, bg, size);
        cursorX = static_cast<int16_t>(cursorX + regular_glyph(text[i]).width * size + spacing);
      }
    }
  }

  void draw_regular_char(int16_t x, int16_t y, char c, uint16_t color, bool transparent, uint16_t bg, uint8_t scale) {
    const Glyph glyph = regular_glyph(c);
    for (uint8_t row = 0; row < glyph.height; ++row)
      for (uint8_t col = 0; col < glyph.width; ++col) {
        const bool on = (glyph.rows[row] & (1U << (glyph.width - 1 - col))) != 0;
        if (!on && transparent) continue;
        const uint16_t px = on ? color : bg;
        for (uint8_t yy = 0; yy < scale; ++yy)
          for (uint8_t xx = 0; xx < scale; ++xx)
            draw_pixel(static_cast<int16_t>(x + col * scale + xx), static_cast<int16_t>(y + row * scale + yy), px);
      }
  }

  void draw_tiny_char(int16_t x, int16_t y, char c, uint16_t color, bool transparent, uint16_t bg, bool embolden) {
    const Glyph glyph = tiny_glyph(c);
    for (uint8_t row = 0; row < glyph.height; ++row)
      for (uint8_t col = 0; col < glyph.width; ++col) {
        const bool on = (glyph.rows[row] & (1U << (glyph.width - 1 - col))) != 0;
        if (!on && transparent) continue;
        const uint16_t px = on ? color : bg;
        draw_pixel(static_cast<int16_t>(x + col), static_cast<int16_t>(y + row), px);
        if (on && embolden)
          draw_pixel(static_cast<int16_t>(x + col + 1), static_cast<int16_t>(y + row), px);
      }
  }

  int width_;
  int height_;
  std::vector<uint16_t> pixels_;
};

// ── Minimal JSON helpers (host-side reimplementation of payload_parser) ──────

std::string json_string(const std::string &json, const char *field) {
  std::string key = "\"";
  key += field;
  key += "\"";
  auto pos = json.find(key);
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos + key.size());
  if (pos == std::string::npos) return "";
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
  if (pos >= json.size()) return "";
  if (json[pos] == '"') {
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
  }
  auto end = pos;
  while (end < json.size() && (isalnum(static_cast<unsigned char>(json[end])) || json[end] == '_' || json[end] == '-'))
    ++end;
  return json.substr(pos, end - pos);
}

int json_int(const std::string &json, const char *field, int fallback) {
  std::string key = "\"";
  key += field;
  key += "\"";
  auto pos = json.find(key);
  if (pos == std::string::npos) return fallback;
  pos = json.find(':', pos + key.size());
  if (pos == std::string::npos) return fallback;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
  if (pos >= json.size()) return fallback;
  bool neg = false;
  if (json[pos] == '-') { neg = true; ++pos; }
  int val = 0;
  bool found = false;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    found = true;
    val = val * 10 + (json[pos] - '0');
    ++pos;
  }
  if (!found) return fallback;
  return neg ? -val : val;
}

// Find matching bracket, handling quotes and nesting.
size_t find_bracket(const std::string &s, size_t openPos, char openCh, char closeCh) {
  if (openPos >= s.size() || s[openPos] != openCh) return std::string::npos;
  int depth = 0;
  bool inQ = false, esc = false;
  for (size_t i = openPos; i < s.size(); ++i) {
    char c = s[i];
    if (esc) { esc = false; continue; }
    if (c == '\\') { esc = true; continue; }
    if (c == '"') { inQ = !inQ; continue; }
    if (inQ) continue;
    if (c == openCh) ++depth;
    else if (c == closeCh) { --depth; if (depth == 0) return i; }
  }
  return std::string::npos;
}

// Extract the Nth object from a "lines" JSON array.
bool extract_line_object(const std::string &json, int index, std::string &out) {
  auto lk = json.find("\"lines\"");
  if (lk == std::string::npos) return false;
  auto arrOpen = json.find('[', lk);
  if (arrOpen == std::string::npos) return false;
  auto arrClose = find_bracket(json, arrOpen, '[', ']');
  if (arrClose == std::string::npos) return false;
  std::string arr = json.substr(arrOpen + 1, arrClose - arrOpen - 1);
  size_t cursor = 0;
  int count = 0;
  while (count <= index) {
    auto objStart = arr.find('{', cursor);
    if (objStart == std::string::npos) return false;
    auto objEnd = find_bracket(arr, objStart, '{', '}');
    if (objEnd == std::string::npos) return false;
    if (count == index) {
      out = arr.substr(objStart, objEnd - objStart + 1);
      return true;
    }
    cursor = objEnd + 1;
    ++count;
  }
  return false;
}

// Extract all "eta" values from nextArrivals in a line object.
std::vector<std::string> extract_eta_values(const std::string &lineObj) {
  std::vector<std::string> etas;
  size_t pos = 0;
  while (pos < lineObj.size()) {
    auto kp = lineObj.find("\"eta\":\"", pos);
    if (kp == std::string::npos) break;
    auto vs = kp + 7;
    auto ve = lineObj.find('"', vs);
    if (ve == std::string::npos) break;
    etas.push_back(lineObj.substr(vs, ve - vs));
    pos = ve + 1;
  }
  return etas;
}

std::string normalize_eta_token(const std::string &raw) {
  std::string s = raw;
  // trim
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  // uppercase
  for (auto &c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  if (s == "NOW" || s == "DUE") return "DUE";
  if (s.empty()) return "--";
  return s;
}

// ── MQTT live state ─────────────────────────────────────────────────────────

struct LiveState {
  std::mutex mu;
  core::RenderModel model{};
  bool hasUpdate = false;
  bool connected = false;
};

static LiveState *gLiveState = nullptr;

template <size_t N>
void safe_copy(char (&dst)[N], const std::string &src) {
  strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0';
}

void apply_mqtt_payload(const std::string &payload, LiveState &live) {
  int displayType = json_int(payload, "displayType", 1);
  if (displayType < 1) displayType = 1;
  if (displayType > 5) displayType = 5;
  int arrivalsToDisplay = json_int(payload, "arrivalsToDisplay", 1);
  if (arrivalsToDisplay < 1) arrivalsToDisplay = 1;
  if (arrivalsToDisplay > 3) arrivalsToDisplay = 3;

  std::string topProvider = json_string(payload, "provider");
  std::string topDirection = json_string(payload, "directionLabel");
  if (topDirection.empty()) topDirection = json_string(payload, "direction");

  core::RenderModel model{};
  model.uiState = core::UiState::kTransit;
  model.hasData = true;
  model.displayType = static_cast<uint8_t>(displayType);

  // Parse lines array
  int rowCount = 0;
  for (int li = 0; li < 3; ++li) {
    std::string lineObj;
    if (!extract_line_object(payload, li, lineObj)) break;

    std::string line = json_string(lineObj, "line");
    if (line.empty()) continue;

    std::string provider = json_string(lineObj, "provider");
    if (provider.empty()) provider = topProvider;

    std::string dest = json_string(lineObj, "destination");
    if (dest.empty()) dest = json_string(lineObj, "directionLabel");
    if (dest.empty()) dest = json_string(lineObj, "stop");
    if (dest.empty()) dest = json_string(lineObj, "label");

    std::string dirLabel = json_string(lineObj, "directionLabel");
    if (dirLabel.empty()) dirLabel = json_string(lineObj, "direction");

    // ETA: prefer top-level "eta" field, then build from nextArrivals
    std::string eta = json_string(lineObj, "eta");
    if (eta.empty()) {
      auto etaVals = extract_eta_values(lineObj);
      if (!etaVals.empty()) {
        std::string merged;
        int count = 0;
        for (auto &ev : etaVals) {
          if (count >= arrivalsToDisplay) break;
          std::string norm = normalize_eta_token(ev);
          if (norm == "--") continue;
          if (!merged.empty()) merged += "/";
          merged += norm;
          ++count;
        }
        eta = merged.empty() ? "--" : merged;
      } else {
        eta = "--";
      }
    }

    safe_copy(model.rows[rowCount].providerId, provider);
    safe_copy(model.rows[rowCount].routeId, line);
    safe_copy(model.rows[rowCount].destination, dest.empty() ? "--" : dest);
    safe_copy(model.rows[rowCount].direction, dirLabel);
    safe_copy(model.rows[rowCount].eta, eta.empty() ? "--" : eta);

    // etaExtra for compact presets (4/5): remaining ETAs after the first
    if (displayType == 4 || displayType == 5) {
      auto etaVals = extract_eta_values(lineObj);
      if (etaVals.size() > 1) {
        std::string extra;
        for (size_t ei = 1; ei < etaVals.size(); ++ei) {
          std::string norm = normalize_eta_token(etaVals[ei]);
          if (norm == "--") continue;
          if (!extra.empty()) extra += ",";
          extra += norm;
        }
        safe_copy(model.rows[rowCount].etaExtra, extra);
      }
    }

    ++rowCount;
  }

  if (rowCount == 0) {
    // No lines parsed — show waiting state
    model.uiState = core::UiState::kConnectedWaitingData;
    model.hasData = false;
    safe_copy(model.statusLine, "NO DATA");
    safe_copy(model.statusDetail, "Waiting for server");
  }

  // If single line with multi-ETA, split into rows (mirroring device behavior)
  if (rowCount == 1 && (displayType != 4 && displayType != 5)) {
    std::string etaStr = model.rows[0].eta;
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= etaStr.size()) {
      auto sep = etaStr.find('/', start);
      std::string token = (sep == std::string::npos)
                              ? etaStr.substr(start)
                              : etaStr.substr(start, sep - start);
      if (!token.empty()) parts.push_back(token);
      if (sep == std::string::npos) break;
      start = sep + 1;
    }

    int rowsToRender = arrivalsToDisplay;
    if (static_cast<int>(parts.size()) < rowsToRender) rowsToRender = static_cast<int>(parts.size());
    if (parts.size() >= 2 && rowsToRender < 2) rowsToRender = 2;
    if (rowsToRender < 1) rowsToRender = 1;
    if (rowsToRender > 3) rowsToRender = 3;

    // First row gets first ETA
    if (!parts.empty()) safe_copy(model.rows[0].eta, parts[0]);

    // Clone row 0 into subsequent rows with remaining ETAs
    for (int ri = 1; ri < rowsToRender; ++ri) {
      memcpy(&model.rows[ri], &model.rows[0], sizeof(core::TransitRowModel));
      model.rows[ri].etaExtra[0] = '\0';
      if (ri < static_cast<int>(parts.size())) {
        safe_copy(model.rows[ri].eta, parts[ri]);
      } else {
        safe_copy(model.rows[ri].eta, "--");
      }
    }
    rowCount = rowsToRender;
  }

  model.activeRows = static_cast<uint8_t>(rowCount > 0 ? rowCount : 1);

  std::lock_guard<std::mutex> lock(live.mu);
  live.model = model;
  live.hasUpdate = true;
}

#ifdef HAS_MOSQUITTO
void mqtt_on_message(struct mosquitto *, void *userdata, const struct mosquitto_message *msg) {
  if (!msg || !msg->payload || msg->payloadlen <= 0) return;
  auto *live = static_cast<LiveState *>(userdata);
  std::string payload(static_cast<const char *>(msg->payload), static_cast<size_t>(msg->payloadlen));
  fprintf(stderr, "[MQTT] Received %d bytes on %s\n", msg->payloadlen, msg->topic ? msg->topic : "?");
  apply_mqtt_payload(payload, *live);
}

void mqtt_on_connect(struct mosquitto *mosq, void *userdata, int rc) {
  auto *live = static_cast<LiveState *>(userdata);
  if (rc == 0) {
    fprintf(stderr, "[MQTT] Connected\n");
    {
      std::lock_guard<std::mutex> lock(live->mu);
      live->connected = true;
    }
    // Subscribe to all device command topics (wildcard)
    mosquitto_subscribe(mosq, nullptr, "/device/+/commands", 0);
  } else {
    fprintf(stderr, "[MQTT] Connect failed rc=%d\n", rc);
  }
}

void mqtt_on_disconnect(struct mosquitto *, void *userdata, int rc) {
  auto *live = static_cast<LiveState *>(userdata);
  fprintf(stderr, "[MQTT] Disconnected rc=%d\n", rc);
  std::lock_guard<std::mutex> lock(live->mu);
  live->connected = false;
}

struct mosquitto *start_mqtt(const SimOptions &opts, LiveState &live) {
  mosquitto_lib_init();
  struct mosquitto *mosq = mosquitto_new("led-simulator", true, &live);
  if (!mosq) {
    fprintf(stderr, "[MQTT] Failed to create client\n");
    return nullptr;
  }

  if (!opts.mqttUser.empty()) {
    mosquitto_username_pw_set(mosq, opts.mqttUser.c_str(),
                              opts.mqttPass.empty() ? nullptr : opts.mqttPass.c_str());
  }

  mosquitto_connect_callback_set(mosq, mqtt_on_connect);
  mosquitto_disconnect_callback_set(mosq, mqtt_on_disconnect);
  mosquitto_message_callback_set(mosq, mqtt_on_message);

  int rc = mosquitto_connect(mosq, opts.mqttHost.c_str(), opts.mqttPort, 60);
  if (rc != MOSQ_ERR_SUCCESS) {
    fprintf(stderr, "[MQTT] mosquitto_connect failed: %s\n", mosquitto_strerror(rc));
    mosquitto_destroy(mosq);
    return nullptr;
  }

  // Start the network loop in a background thread
  mosquitto_loop_start(mosq);

  // If a specific device ID was given, also subscribe to its topic directly
  if (!opts.deviceId.empty()) {
    std::string topic = "/device/" + opts.deviceId + "/commands";
    fprintf(stderr, "[MQTT] Will subscribe to: %s\n", topic.c_str());
  }

  return mosq;
}

void stop_mqtt(struct mosquitto *mosq) {
  if (!mosq) return;
  mosquitto_loop_stop(mosq, true);
  mosquitto_disconnect(mosq);
  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();
}
#endif  // HAS_MOSQUITTO

// ── Scenario definitions ────────────────────────────────────────────────────

struct ScenarioDef {
  const char *name;
  core::UiState uiState;
  bool hasData;
  int activeRows;
  int displayType;  // 0 = use current
  const char *route[3];
  const char *destination[3];
  const char *eta[3];
  const char *etaExtra[3];
  const char *direction[3];
  const char *statusLine;
  const char *statusDetail;
  const char *apSsid;
  const char *apPin;
};

// Route list for cycling with Left/Right
static const char *kRouteList[] = {
    "A", "C", "E", "B", "D", "F", "M", "G", "J", "Z",
    "L", "N", "Q", "R", "W", "1", "2", "3", "4", "5",
    "6", "7", "S",
};
static constexpr size_t kRouteCount = sizeof(kRouteList) / sizeof(kRouteList[0]);

static const ScenarioDef kScenarios[] = {
    {
        "transit-2row",
        core::UiState::kTransit, true, 2, 0,
        {"A", "1", ""},
        {"Inwood-207 St", "South Ferry", ""},
        {"3", "8", ""},
        {"", "", ""},
        {"", "", ""},
        nullptr, nullptr, nullptr, nullptr,
    },
    {
        "transit-3row",
        core::UiState::kTransit, true, 3, 0,
        {"A", "1", "Q"},
        {"Inwood-207 St", "South Ferry", "96 St"},
        {"3", "8", "12"},
        {"", "", ""},
        {"", "", ""},
        nullptr, nullptr, nullptr, nullptr,
    },
    {
        "transit-1row",
        core::UiState::kTransit, true, 1, 0,
        {"A", "", ""},
        {"Inwood-207 St", "", ""},
        {"3", "", ""},
        {"", "", ""},
        {"", "", ""},
        nullptr, nullptr, nullptr, nullptr,
    },
    {
        "transit-compact",
        core::UiState::kTransit, true, 1, 4,
        {"A", "", ""},
        {"Inwood-207 St", "", ""},
        {"3", "", ""},
        {"6,12,18", "", ""},
        {"Uptown", "", ""},
        nullptr, nullptr, nullptr, nullptr,
    },
    {
        "setup",
        core::UiState::kSetupMode, false, 0, 0,
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        "SETUP MODE", "Connect to device Wi-Fi",
        "CommuteLive-AB12", "12345678",
    },
    {
        "waiting",
        core::UiState::kConnectedWaitingData, false, 0, 0,
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        "ADD A LINE", "Open the app to get started",
        nullptr, nullptr,
    },
    {
        "booting",
        core::UiState::kBooting, false, 0, 0,
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        {"", "", ""},
        "BOOTING", "Connecting...",
        nullptr, nullptr,
    },
};

static constexpr size_t kScenarioCount = sizeof(kScenarios) / sizeof(kScenarios[0]);

// ── Build RenderModel from scenario ─────────────────────────────────────────

core::RenderModel build_model(const ScenarioDef &scen, const SimOptions &opts, int routeOffset) {
  core::RenderModel model{};
  model.uiState = scen.uiState;
  model.hasData = scen.hasData;
  model.displayType = scen.displayType > 0 ? static_cast<uint8_t>(scen.displayType)
                                           : static_cast<uint8_t>(opts.displayType);
  model.activeRows = scen.activeRows > 0 ? static_cast<uint8_t>(scen.activeRows)
                                         : static_cast<uint8_t>(opts.rows);
  model.updatedAtMs = 0;

  if (scen.statusLine) copy_cstr(model.statusLine, scen.statusLine);
  if (scen.statusDetail) copy_cstr(model.statusDetail, scen.statusDetail);
  if (scen.apSsid) copy_cstr(model.apSsid, scen.apSsid);
  if (scen.apPin) copy_cstr(model.apPin, scen.apPin);

  for (int i = 0; i < 3; ++i) {
    memset(&model.rows[i], 0, sizeof(core::TransitRowModel));
    copy_cstr(model.rows[i].providerId, "mta-subway");

    // Apply route offset only to row 0
    if (i == 0 && scen.route[0] && scen.route[0][0] != '\0') {
      int idx = 0;
      for (size_t j = 0; j < kRouteCount; ++j) {
        if (strcmp(kRouteList[j], scen.route[0]) == 0) { idx = static_cast<int>(j); break; }
      }
      idx = (idx + routeOffset) % static_cast<int>(kRouteCount);
      if (idx < 0) idx += static_cast<int>(kRouteCount);
      copy_cstr(model.rows[i].routeId, kRouteList[idx]);
    } else {
      const char *r = scen.route[i];
      copy_cstr(model.rows[i].routeId, (r && r[0]) ? r : "--");
    }

    const char *d = scen.destination[i];
    copy_cstr(model.rows[i].destination, (d && d[0]) ? d : "--");
    const char *e = scen.eta[i];
    copy_cstr(model.rows[i].eta, (e && e[0]) ? e : "--");
    if (scen.etaExtra[i]) copy_cstr(model.rows[i].etaExtra, scen.etaExtra[i]);
    if (scen.direction[i]) copy_cstr(model.rows[i].direction, scen.direction[i]);
  }

  return model;
}

// ── RGB565 to SDL color with brightness ─────────────────────────────────────

void rgb565_to_rgb(uint16_t c, uint8_t brightness, uint8_t &r, uint8_t &g, uint8_t &b) {
  // Decode RGB565 to full 8-bit
  uint16_t r8 = static_cast<uint16_t>(((c >> 11) & 0x1F) * 255 / 31);
  uint16_t g8 = static_cast<uint16_t>(((c >> 5) & 0x3F) * 255 / 63);
  uint16_t b8 = static_cast<uint16_t>((c & 0x1F) * 255 / 31);

  // Apply hardware brightness (0-255 maps to 0.0-1.0 scale)
  // Real HUB75 panels use BCM (Binary Code Modulation), so brightness isn't
  // perfectly linear — we use a gamma-adjusted curve to better match.
  const float bright = static_cast<float>(brightness) / 255.0f;
  const float gamma = 2.2f;
  const float scale = powf(bright, 1.0f / gamma);

  r = static_cast<uint8_t>(std::min(255.0f, r8 * scale));
  g = static_cast<uint8_t>(std::min(255.0f, g8 * scale));
  b = static_cast<uint8_t>(std::min(255.0f, b8 * scale));
}

// ── Render pixel buffer to SDL ──────────────────────────────────────────────
// Simulates the physical appearance of a 1x2 grid of 64x32 HUB75 P3/P4 panels:
// - Dark green PCB substrate
// - Round LED dots with inter-pixel gap
// - Dim "off" LED glow
// - Panel seam line between the two 64-wide panels

void blit_to_sdl(SDL_Renderer *renderer, const SimDisplayEngine &display, int pixScale, bool ledDots) {
  const int w = display.width();
  const int h = display.height();
  const auto &pixels = display.pixels();

  // Dark green-black PCB substrate color (real HUB75 boards are dark green/black FR4)
  SDL_SetRenderDrawColor(renderer, 12, 14, 12, 255);
  SDL_RenderClear(renderer);

  // Optional: draw panel seam between the two 64x32 panels
  if (kPanelCols > 1) {
    SDL_SetRenderDrawColor(renderer, 6, 7, 6, 255);
    for (int col = 1; col < kPanelCols; ++col) {
      const int seamX = col * kPanelWidth * pixScale;
      SDL_Rect seam = {seamX - 1, 0, 2, h * pixScale};
      SDL_RenderFillRect(renderer, &seam);
    }
  }

  const int gap = ledDots ? kLedGapPx : 0;
  const int dotSize = pixScale - gap;
  // LED dot radius — real P3 panels have roughly 60-70% fill ratio
  const int dotRadius = ledDots ? static_cast<int>(dotSize * 0.42f) : 0;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const uint16_t c = pixels[static_cast<size_t>(y) * w + x];
      uint8_t r, g, b;
      rgb565_to_rgb(c, kHwBrightness, r, g, b);

      // "Off" LEDs still have a very faint dark glow on real panels
      if (c == kColorBlack) {
        r = 5; g = 6; b = 5;
      }

      SDL_SetRenderDrawColor(renderer, r, g, b, 255);

      if (ledDots && dotRadius > 1) {
        // Round LED dot (midpoint circle fill)
        const int cx = x * pixScale + pixScale / 2;
        const int cy = y * pixScale + pixScale / 2;
        const int r2 = dotRadius * dotRadius;
        for (int dy = -dotRadius; dy <= dotRadius; ++dy)
          for (int dx = -dotRadius; dx <= dotRadius; ++dx)
            if (dx * dx + dy * dy <= r2)
              SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
      } else {
        SDL_Rect rect = {x * pixScale + gap / 2, y * pixScale + gap / 2,
                         dotSize > 0 ? dotSize : pixScale,
                         dotSize > 0 ? dotSize : pixScale};
        SDL_RenderFillRect(renderer, &rect);
      }
    }
  }
}

// ── Render a frame ──────────────────────────────────────────────────────────

void render_frame(SimDisplayEngine &display, const ScenarioDef &scen, const SimOptions &opts, int routeOffset) {
  core::LayoutEngine layout;
  layout.set_viewport(kMatrixWidth, kMatrixHeight);

  core::RenderModel model = build_model(scen, opts, routeOffset);
  core::DrawList drawList{};
  layout.build_transit_layout(model, drawList);

  display.clear(kColorBlack);

  display::BadgeRenderer badgeRenderer;
  for (size_t i = 0; i < drawList.count; ++i) {
    const core::DrawCommand &cmd = drawList.commands[i];
    switch (cmd.type) {
      case core::DrawCommandType::kFillRect:
        display.fill_rect(cmd.x, cmd.y, cmd.w, cmd.h, cmd.color);
        break;
      case core::DrawCommandType::kText:
        display.draw_text(cmd.x, cmd.y, cmd.text, cmd.color, cmd.size, cmd.bg);
        break;
      case core::DrawCommandType::kBadge:
        badgeRenderer.draw_badge(display, cmd.x, cmd.y, cmd.w, cmd.text);
        break;
      default:
        break;
    }
  }
}

// ── Load .env.simulator file ────────────────────────────────────────────────

void load_env_file(SimOptions &opts) {
  // Try .env.simulator in the same directory as the binary, then cwd.
  const char *paths[] = {"tools/.env.simulator", ".env.simulator"};
  FILE *f = nullptr;
  for (auto p : paths) {
    f = fopen(p, "r");
    if (f) break;
  }
  if (!f) return;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    // Strip newline
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';

    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\0') continue;

    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    const char *key = line;
    const char *val = eq + 1;

    // Skip empty values
    if (val[0] == '\0') continue;

    if (strcmp(key, "MQTT_HOST") == 0) {
      opts.mqttHost = val;
    } else if (strcmp(key, "MQTT_PORT") == 0) {
      opts.mqttPort = atoi(val);
    } else if (strcmp(key, "MQTT_USERNAME") == 0) {
      opts.mqttUser = val;
    } else if (strcmp(key, "MQTT_PASSWORD") == 0) {
      opts.mqttPass = val;
    } else if (strcmp(key, "DEVICE_ID") == 0) {
      opts.deviceId = val;
      opts.mqttEnabled = true;
    }
  }
  fclose(f);
}

// ── CLI parsing ─────────────────────────────────────────────────────────────

bool parse_args(int argc, char **argv, SimOptions &opts) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
      opts.scale = atoi(argv[++i]);
      if (opts.scale < 1) opts.scale = 1;
    } else if (strcmp(argv[i], "--display-type") == 0 && i + 1 < argc) {
      opts.displayType = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
      opts.rows = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
      ++i;
      for (size_t j = 0; j < kScenarioCount; ++j) {
        if (strcmp(argv[i], kScenarios[j].name) == 0) {
          opts.scenarioIndex = static_cast<int>(j);
          break;
        }
      }
    } else if (strcmp(argv[i], "--flat") == 0) {
      opts.ledDots = false;
    } else if (strcmp(argv[i], "--mqtt") == 0) {
      opts.mqttEnabled = true;
    } else if (strcmp(argv[i], "--mqtt-host") == 0 && i + 1 < argc) {
      opts.mqttHost = argv[++i];
      opts.mqttEnabled = true;
    } else if (strcmp(argv[i], "--mqtt-port") == 0 && i + 1 < argc) {
      opts.mqttPort = atoi(argv[++i]);
      opts.mqttEnabled = true;
    } else if (strcmp(argv[i], "--mqtt-user") == 0 && i + 1 < argc) {
      opts.mqttUser = argv[++i];
    } else if (strcmp(argv[i], "--mqtt-pass") == 0 && i + 1 < argc) {
      opts.mqttPass = argv[++i];
    } else if (strcmp(argv[i], "--device-id") == 0 && i + 1 < argc) {
      opts.deviceId = argv[++i];
      opts.mqttEnabled = true;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr,
              "LED Matrix Simulator\n"
              "Usage: %s [options]\n"
              "\n"
              "Display options:\n"
              "  --scale <n>           Pixel scale (default: %d)\n"
              "  --display-type <1-5>  Layout preset\n"
              "  --rows <1-3>          Active transit rows\n"
              "  --scenario <name>     Initial scenario\n"
              "  --flat                Flat pixels instead of round LED dots\n"
              "\n"
              "MQTT live mode (receives real data from your server):\n"
              "  --mqtt                Enable MQTT connection\n"
              "  --mqtt-host <host>    MQTT broker host (default: localhost)\n"
              "  --mqtt-port <port>    MQTT broker port (default: 1883)\n"
              "  --mqtt-user <user>    MQTT username\n"
              "  --mqtt-pass <pass>    MQTT password\n"
              "  --device-id <id>      Device ID to simulate (enables MQTT)\n"
              "\n"
              "Keys:\n"
              "  1-5        Switch display type\n"
              "  S          Cycle scenario (mock mode only)\n"
              "  R          Cycle active rows (1/2/3)\n"
              "  Left/Right Cycle route on row 1 (mock mode only)\n"
              "  D          Toggle LED dots / flat pixels\n"
              "  +/-        Zoom in/out\n"
              "  Q / Esc    Quit\n"
              "\n"
              "Examples:\n"
              "  %s                                          # Mock mode\n"
              "  %s --device-id abc123                       # Live MQTT, localhost\n"
              "  %s --mqtt-host broker.example.com --mqtt-user admin --mqtt-pass secret\n",
              argv[0], kDefaultScale, argv[0], argv[0], argv[0]);
      return false;
    }
  }
  return true;
}

// ── Render a frame from a RenderModel directly ──────────────────────────────

void render_model_frame(SimDisplayEngine &display, const core::RenderModel &model) {
  core::LayoutEngine layout;
  layout.set_viewport(kMatrixWidth, kMatrixHeight);

  core::DrawList drawList{};
  // build_transit_layout takes a non-const ref in some builds, so copy.
  core::RenderModel m = model;
  layout.build_transit_layout(m, drawList);

  display.clear(kColorBlack);

  display::BadgeRenderer badgeRenderer;
  for (size_t i = 0; i < drawList.count; ++i) {
    const core::DrawCommand &cmd = drawList.commands[i];
    switch (cmd.type) {
      case core::DrawCommandType::kFillRect:
        display.fill_rect(cmd.x, cmd.y, cmd.w, cmd.h, cmd.color);
        break;
      case core::DrawCommandType::kText:
        display.draw_text(cmd.x, cmd.y, cmd.text, cmd.color, cmd.size, cmd.bg);
        break;
      case core::DrawCommandType::kBadge:
        badgeRenderer.draw_badge(display, cmd.x, cmd.y, cmd.w, cmd.text);
        break;
      default:
        break;
    }
  }
}

// ── Main ────────────────────────────────────────────────────────────────────

int run_simulator(SimOptions &opts) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  int windowW = kMatrixWidth * opts.scale;
  int windowH = kMatrixHeight * opts.scale;

  SDL_Window *window = SDL_CreateWindow(
      "LED Matrix Simulator",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      windowW, windowH,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SimDisplayEngine display(kMatrixWidth, kMatrixHeight);
  int routeOffset = 0;
  bool dirty = true;
  bool running = true;
  bool liveMode = opts.mqttEnabled;
  const char *modeLabel = liveMode ? "LIVE" : "MOCK";

  // MQTT setup
  LiveState liveState;
  gLiveState = &liveState;

#ifdef HAS_MOSQUITTO
  struct mosquitto *mosq = nullptr;
  if (liveMode) {
    // Show "connecting" state initially
    liveState.model.uiState = core::UiState::kBooting;
    liveState.model.hasData = false;
    safe_copy(liveState.model.statusLine, "CONNECTING");
    safe_copy(liveState.model.statusDetail, opts.mqttHost.c_str());
    liveState.hasUpdate = true;

    mosq = start_mqtt(opts, liveState);
    if (!mosq) {
      fprintf(stderr, "Failed to start MQTT — falling back to mock mode\n");
      liveMode = false;
      modeLabel = "MOCK";
    }
  }
#else
  if (liveMode) {
    fprintf(stderr, "MQTT support not compiled in. Build with: make simulator-mqtt\n"
                    "Falling back to mock mode.\n");
    liveMode = false;
    modeLabel = "MOCK";
  }
#endif

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          running = false;
          break;
        case SDL_KEYDOWN:
          switch (event.key.keysym.sym) {
            case SDLK_q:
            case SDLK_ESCAPE:
              running = false;
              break;
            case SDLK_1: opts.displayType = 1; dirty = true; break;
            case SDLK_2: opts.displayType = 2; dirty = true; break;
            case SDLK_3: opts.displayType = 3; dirty = true; break;
            case SDLK_4: opts.displayType = 4; dirty = true; break;
            case SDLK_5: opts.displayType = 5; dirty = true; break;
            case SDLK_s:
              if (!liveMode) {
                opts.scenarioIndex = (opts.scenarioIndex + 1) % static_cast<int>(kScenarioCount);
                routeOffset = 0;
                dirty = true;
              }
              break;
            case SDLK_r:
              opts.rows = (opts.rows % 3) + 1;
              dirty = true;
              break;
            case SDLK_LEFT:
              if (!liveMode) { --routeOffset; dirty = true; }
              break;
            case SDLK_RIGHT:
              if (!liveMode) { ++routeOffset; dirty = true; }
              break;
            case SDLK_d:
              opts.ledDots = !opts.ledDots;
              dirty = true;
              break;
            case SDLK_EQUALS:
            case SDLK_PLUS:
              opts.scale = std::min(opts.scale + 1, 20);
              windowW = kMatrixWidth * opts.scale;
              windowH = kMatrixHeight * opts.scale;
              SDL_SetWindowSize(window, windowW, windowH);
              dirty = true;
              break;
            case SDLK_MINUS:
              opts.scale = std::max(opts.scale - 1, 2);
              windowW = kMatrixWidth * opts.scale;
              windowH = kMatrixHeight * opts.scale;
              SDL_SetWindowSize(window, windowW, windowH);
              dirty = true;
              break;
            default:
              break;
          }
          break;
        default:
          break;
      }
    }

    // Check for MQTT updates
    if (liveMode) {
      std::lock_guard<std::mutex> lock(liveState.mu);
      if (liveState.hasUpdate) {
        dirty = true;
        liveState.hasUpdate = false;
      }
    }

    if (dirty) {
      if (liveMode) {
        core::RenderModel model;
        {
          std::lock_guard<std::mutex> lock(liveState.mu);
          model = liveState.model;
        }
        render_model_frame(display, model);
      } else {
        const ScenarioDef &scen = kScenarios[opts.scenarioIndex];
        render_frame(display, scen, opts, routeOffset);
      }

      blit_to_sdl(renderer, display, opts.scale, opts.ledDots);

      // Update window title with current state
      char title[256];
      if (liveMode) {
        bool conn;
        { std::lock_guard<std::mutex> lock(liveState.mu); conn = liveState.connected; }
        snprintf(title, sizeof(title),
                 "LED Simulator [%s]  |  %s:%d %s  |  [D]ots [+/-]zoom",
                 modeLabel,
                 opts.mqttHost.c_str(), opts.mqttPort,
                 conn ? "CONNECTED" : "CONNECTING...");
      } else {
        const ScenarioDef &scen = kScenarios[opts.scenarioIndex];
        snprintf(title, sizeof(title),
                 "LED Simulator [%s]  |  %s  |  display:%d  rows:%d  |  [S]cenario [1-5]type [R]ows [</>]route [D]ots [+/-]zoom",
                 modeLabel, scen.name, opts.displayType, opts.rows);
      }
      SDL_SetWindowTitle(window, title);

      SDL_RenderPresent(renderer);
      dirty = false;
    }

    SDL_Delay(16);  // ~60fps idle loop
  }

#ifdef HAS_MOSQUITTO
  stop_mqtt(mosq);
#endif

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  SimOptions opts;
  load_env_file(opts);       // .env.simulator values first
  if (!parse_args(argc, argv, opts)) return 0;  // CLI flags override
  return run_simulator(opts);
}
