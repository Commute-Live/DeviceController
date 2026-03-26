// WebAssembly bridge for the LED Matrix Simulator.
// Compiles the same C++ rendering pipeline (LayoutEngine, BadgeRenderer, etc.)
// to WASM and exposes simple C functions for JavaScript to call.
//
// Build: make -C tools web

#include <emscripten/emscripten.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/layout_engine.h"
#include "display/badge_renderer.h"

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr int kMatrixWidth = 128;
static constexpr int kMatrixHeight = 32;
static constexpr uint8_t kHwBrightness = 32;
static constexpr uint16_t kColorBlack = 0x0000;
static constexpr uint8_t kTextSizeTiny = 0;
static constexpr uint8_t kTextSizeTinyPlus = 255;

// ── Font glyph tables ────────────────────────────────────────────────────────

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

template <size_t N>
void safe_copy(char (&dst)[N], const std::string &src) {
  strncpy(dst, src.c_str(), N - 1);
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

// ── Display Engine (pixel buffer, no SDL) ────────────────────────────────────

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
    const int16_t spacing = tiny ? 1 : static_cast<int16_t>(size);
    int16_t cursorX = 0;
    int16_t globalMinX = 32767, globalMaxX = -1, globalMinY = 32767, globalMaxY = -1;
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
      if (tiny) cursorX = static_cast<int16_t>(cursorX + glyph.width + spacing);
      else cursorX = static_cast<int16_t>(cursorX + glyph.width * size + spacing);
    }
    if (globalMaxX < 0) return metrics;
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

// ── JSON helpers ─────────────────────────────────────────────────────────────

std::string json_string(const std::string &json, const char *field) {
  std::string key = "\""; key += field; key += "\"";
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
  while (end < json.size() && (isalnum(static_cast<unsigned char>(json[end])) || json[end] == '_' || json[end] == '-')) ++end;
  return json.substr(pos, end - pos);
}

int json_int(const std::string &json, const char *field, int fallback) {
  std::string key = "\""; key += field; key += "\"";
  auto pos = json.find(key);
  if (pos == std::string::npos) return fallback;
  pos = json.find(':', pos + key.size());
  if (pos == std::string::npos) return fallback;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
  if (pos >= json.size()) return fallback;
  bool neg = false;
  if (json[pos] == '-') { neg = true; ++pos; }
  int val = 0; bool found = false;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    found = true; val = val * 10 + (json[pos] - '0'); ++pos;
  }
  if (!found) return fallback;
  return neg ? -val : val;
}

size_t find_bracket(const std::string &s, size_t openPos, char openCh, char closeCh) {
  if (openPos >= s.size() || s[openPos] != openCh) return std::string::npos;
  int depth = 0; bool inQ = false, esc = false;
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

bool extract_line_object(const std::string &json, int index, std::string &out) {
  auto lk = json.find("\"lines\"");
  if (lk == std::string::npos) return false;
  auto arrOpen = json.find('[', lk);
  if (arrOpen == std::string::npos) return false;
  auto arrClose = find_bracket(json, arrOpen, '[', ']');
  if (arrClose == std::string::npos) return false;
  std::string arr = json.substr(arrOpen + 1, arrClose - arrOpen - 1);
  size_t cursor = 0; int count = 0;
  while (count <= index) {
    auto objStart = arr.find('{', cursor);
    if (objStart == std::string::npos) return false;
    auto objEnd = find_bracket(arr, objStart, '{', '}');
    if (objEnd == std::string::npos) return false;
    if (count == index) { out = arr.substr(objStart, objEnd - objStart + 1); return true; }
    cursor = objEnd + 1; ++count;
  }
  return false;
}

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
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  for (auto &c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  if (s == "NOW" || s == "DUE") return "DUE";
  if (s.empty()) return "--";
  return s;
}

// ── Global state ─────────────────────────────────────────────────────────────

static SimDisplayEngine gDisplay(kMatrixWidth, kMatrixHeight);
static core::RenderModel gModel{};
static uint8_t gRgbaBuffer[kMatrixWidth * kMatrixHeight * 4];
static uint8_t gHwBrightness = kHwBrightness;

static uint8_t clamp_payload_brightness(int value) {
  if (value < 1) return 1;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

static uint8_t brightness_percent_to_panel(uint8_t percent) {
  const uint16_t scaled = static_cast<uint16_t>((static_cast<uint32_t>(percent) * 255U + 50U) / 100U);
  if (scaled < 1U) return 1;
  if (scaled > 255U) return 255;
  return static_cast<uint8_t>(scaled);
}

static void rgb565_to_rgb(uint16_t c, uint8_t brightness, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint16_t r8 = static_cast<uint16_t>(((c >> 11) & 0x1F) * 255 / 31);
  uint16_t g8 = static_cast<uint16_t>(((c >> 5) & 0x3F) * 255 / 63);
  uint16_t b8 = static_cast<uint16_t>((c & 0x1F) * 255 / 31);
  const float bright = static_cast<float>(brightness) / 255.0f;
  const float gamma = 2.2f;
  const float scale = powf(bright, 1.0f / gamma);
  r = static_cast<uint8_t>(std::min(255.0f, r8 * scale));
  g = static_cast<uint8_t>(std::min(255.0f, g8 * scale));
  b = static_cast<uint8_t>(std::min(255.0f, b8 * scale));
}

static void render_and_export() {
  core::LayoutEngine layout;
  layout.set_viewport(kMatrixWidth, kMatrixHeight);
  core::DrawList drawList{};
  core::RenderModel m = gModel;
  layout.build_transit_layout(m, drawList);
  gDisplay.clear(kColorBlack);
  display::BadgeRenderer badgeRenderer;
  for (size_t i = 0; i < drawList.count; ++i) {
    const core::DrawCommand &cmd = drawList.commands[i];
    switch (cmd.type) {
      case core::DrawCommandType::kFillRect:
        gDisplay.fill_rect(cmd.x, cmd.y, cmd.w, cmd.h, cmd.color); break;
      case core::DrawCommandType::kText:
        gDisplay.draw_text(cmd.x, cmd.y, cmd.text, cmd.color, cmd.size, cmd.bg); break;
      case core::DrawCommandType::kBadge:
        badgeRenderer.draw_badge(gDisplay, cmd.x, cmd.y, cmd.w, cmd.text); break;
      case core::DrawCommandType::kMonoBitmap:
        if (!cmd.bitmap || cmd.w <= 0 || cmd.h <= 0) {
          break;
        }
        for (int16_t y = 0; y < cmd.h; ++y) {
          for (int16_t x = 0; x < cmd.w; ++x) {
            const uint8_t pixel =
                cmd.bitmap[static_cast<size_t>(y) * static_cast<size_t>(cmd.w) + static_cast<size_t>(x)];
            gDisplay.draw_pixel(static_cast<int16_t>(cmd.x + x),
                                static_cast<int16_t>(cmd.y + y),
                                pixel ? cmd.color : cmd.bg);
          }
        }
        break;
      default: break;
      }
  }
  // Convert RGB565 pixel buffer to RGBA for JS Canvas
  const auto &pixels = gDisplay.pixels();
  for (int i = 0; i < kMatrixWidth * kMatrixHeight; ++i) {
    uint8_t r, g, b;
    if (pixels[i] == kColorBlack) {
      r = 5; g = 6; b = 5;  // dim off-LED glow
    } else {
      rgb565_to_rgb(pixels[i], gHwBrightness, r, g, b);
    }
    gRgbaBuffer[i * 4 + 0] = r;
    gRgbaBuffer[i * 4 + 1] = g;
    gRgbaBuffer[i * 4 + 2] = b;
    gRgbaBuffer[i * 4 + 3] = 255;
  }
}

static void apply_payload(const char *json, int len) {
  std::string payload(json, static_cast<size_t>(len));
  int displayType = json_int(payload, "displayType", 1);
  if (displayType < 1) displayType = 1;
  if (displayType > 5) displayType = 5;
  gHwBrightness = brightness_percent_to_panel(clamp_payload_brightness(json_int(payload, "brightness", 60)));
  int arrivalsToDisplay = json_int(payload, "arrivalsToDisplay", 1);
  if (arrivalsToDisplay < 1) arrivalsToDisplay = 1;
  if (arrivalsToDisplay > 3) arrivalsToDisplay = 3;

  std::string topProvider = json_string(payload, "provider");
  core::RenderModel model{};
  model.uiState = core::UiState::kTransit;
  model.hasData = true;
  model.displayType = static_cast<uint8_t>(displayType);

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
    std::string eta = json_string(lineObj, "eta");
    if (eta.empty()) {
      auto etaVals = extract_eta_values(lineObj);
      if (!etaVals.empty()) {
        std::string merged; int count = 0;
        for (auto &ev : etaVals) {
          if (count >= arrivalsToDisplay) break;
          std::string norm = normalize_eta_token(ev);
          if (norm == "--") continue;
          if (!merged.empty()) merged += "/";
          merged += norm; ++count;
        }
        eta = merged.empty() ? "--" : merged;
      } else { eta = "--"; }
    }
    safe_copy(model.rows[rowCount].providerId, provider);
    safe_copy(model.rows[rowCount].routeId, line);
    safe_copy(model.rows[rowCount].destination, dest.empty() ? "--" : dest);
    safe_copy(model.rows[rowCount].direction, dirLabel);
    safe_copy(model.rows[rowCount].eta, eta.empty() ? "--" : eta);
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
    model.uiState = core::UiState::kConnectedWaitingData;
    model.hasData = false;
    safe_copy(model.statusLine, "NO DATA");
    safe_copy(model.statusDetail, "Waiting for server");
  }

  if (rowCount == 1 && (displayType != 4 && displayType != 5)) {
    std::string etaStr = model.rows[0].eta;
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= etaStr.size()) {
      auto sep = etaStr.find('/', start);
      std::string token = (sep == std::string::npos) ? etaStr.substr(start) : etaStr.substr(start, sep - start);
      if (!token.empty()) parts.push_back(token);
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
    int rowsToRender = arrivalsToDisplay;
    if (static_cast<int>(parts.size()) < rowsToRender) rowsToRender = static_cast<int>(parts.size());
    if (parts.size() >= 2 && rowsToRender < 2) rowsToRender = 2;
    if (rowsToRender < 1) rowsToRender = 1;
    if (rowsToRender > 3) rowsToRender = 3;
    if (!parts.empty()) safe_copy(model.rows[0].eta, parts[0]);
    for (int ri = 1; ri < rowsToRender; ++ri) {
      memcpy(&model.rows[ri], &model.rows[0], sizeof(core::TransitRowModel));
      model.rows[ri].etaExtra[0] = '\0';
      if (ri < static_cast<int>(parts.size())) safe_copy(model.rows[ri].eta, parts[ri]);
      else safe_copy(model.rows[ri].eta, "--");
    }
    rowCount = rowsToRender;
  }

  model.activeRows = static_cast<uint8_t>(rowCount > 0 ? rowCount : 1);
  gModel = model;
}

// ── Exported WASM functions ──────────────────────────────────────────────────

extern "C" {

EMSCRIPTEN_KEEPALIVE
int wasm_get_width() { return kMatrixWidth; }

EMSCRIPTEN_KEEPALIVE
int wasm_get_height() { return kMatrixHeight; }

EMSCRIPTEN_KEEPALIVE
uint8_t *wasm_get_pixels() {
  render_and_export();
  return gRgbaBuffer;
}

EMSCRIPTEN_KEEPALIVE
void wasm_apply_payload(const char *json, int len) {
  apply_payload(json, len);
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_display_type(int type) {
  if (type < 1) type = 1;
  if (type > 5) type = 5;
  gModel.displayType = static_cast<uint8_t>(type);
}

EMSCRIPTEN_KEEPALIVE
void wasm_init() {
  memset(&gModel, 0, sizeof(gModel));
  gModel.uiState = core::UiState::kBooting;
  gModel.hasData = false;
  safe_copy(gModel.statusLine, "COMMUTE LIVE");
  safe_copy(gModel.statusDetail, "Web Simulator");
}

}  // extern "C"
