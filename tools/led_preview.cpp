#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "core/layout_engine.h"
#include "display/badge_renderer.h"
#include "transit/mta_color_map.h"

namespace {

constexpr int kMatrixWidth = 128;
constexpr int kMatrixHeight = 32;
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint8_t kTextSizeTiny = 0;
constexpr uint8_t kTextSizeTinyPlus = 255;

struct PreviewOptions {
  std::string scenario = "transit";
  std::string outputPath = "/tmp/devicecontroller-led-preview.ppm";
  int scale = 8;
  int displayType = 1;
  int rows = 2;
  std::string bleName = "CommuteLive-AB12";
  std::string provider1;
  std::string route1 = "A";
  std::string destination1 = "Inwood-207 St";
  std::string eta1 = "3";
  std::string etaExtra1;
  std::string direction1;
  std::string provider2;
  std::string route2 = "1";
  std::string destination2 = "South Ferry";
  std::string eta2 = "8";
  std::string etaExtra2;
  std::string direction2;
  std::string provider3;
  std::string route3 = "Q";
  std::string destination3 = "96 St";
  std::string eta3 = "12";
  std::string etaExtra3;
  std::string direction3;
};

struct Glyph {
  uint8_t width;
  uint8_t height;
  const uint8_t *rows;
};

uint8_t clamp_u8(int value, uint8_t minimum, uint8_t maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return static_cast<uint8_t>(value);
}

template <size_t N>
void copy_cstr(char (&dst)[N], const std::string &src) {
  strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0';
}

template <size_t N>
void copy_cstr(char (&dst)[N], const char *src) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, N - 1);
  dst[N - 1] = '\0';
}

void print_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s [options]\n"
          "  --scenario transit|transit-compact|setup|waiting\n"
          "  --output <path>          Output PPM path (default: /tmp/devicecontroller-led-preview.ppm)\n"
          "  --scale <n>              Pixel upscale factor (default: 8)\n"
          "  --display-type <1-5>     Layout preset (default: 1)\n"
          "  --rows <1-2>             Active rows for transit scenarios\n"
          "  --ble-name <value>       Setup mode Bluetooth name\n"
          "  --provider<N> <value>    Provider id override, N=1..3\n"
          "  --route<N> <value>       Row route id, N=1..3\n"
          "  --destination<N> <value> Row destination, N=1..3\n"
          "  --eta<N> <value>         Row ETA, N=1..3\n"
          "  --eta-extra<N> <value>   Compact extra ETA line, N=1..3\n"
          "  --direction<N> <value>   Optional direction label, N=1..3\n",
          program);
}

bool parse_int_arg(const char *arg, int &out) {
  if (!arg || *arg == '\0') {
    return false;
  }
  char *end = nullptr;
  const long value = strtol(arg, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  out = static_cast<int>(value);
  return true;
}

bool parse_args(int argc, char **argv, PreviewOptions &options) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    auto require_value = [&](const char *flag) -> const char * {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", flag);
        return nullptr;
      }
      return argv[++i];
    };

    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      print_usage(argv[0]);
      return false;
    }
    if (strcmp(arg, "--scenario") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.scenario = value;
      continue;
    }
    if (strcmp(arg, "--output") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.outputPath = value;
      continue;
    }
    if (strcmp(arg, "--scale") == 0) {
      const char *value = require_value(arg);
      if (!value || !parse_int_arg(value, options.scale)) return false;
      continue;
    }
    if (strcmp(arg, "--display-type") == 0) {
      const char *value = require_value(arg);
      if (!value || !parse_int_arg(value, options.displayType)) return false;
      continue;
    }
    if (strcmp(arg, "--rows") == 0) {
      const char *value = require_value(arg);
      if (!value || !parse_int_arg(value, options.rows)) return false;
      continue;
    }
    if (strcmp(arg, "--ble-name") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.bleName = value;
      continue;
    }
    if (strcmp(arg, "--provider1") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.provider1 = value;
      continue;
    }
    if (strcmp(arg, "--provider2") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.provider2 = value;
      continue;
    }
    if (strcmp(arg, "--provider3") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.provider3 = value;
      continue;
    }
    if (strcmp(arg, "--route1") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.route1 = value;
      continue;
    }
    if (strcmp(arg, "--route2") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.route2 = value;
      continue;
    }
    if (strcmp(arg, "--route3") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.route3 = value;
      continue;
    }
    if (strcmp(arg, "--destination1") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.destination1 = value;
      continue;
    }
    if (strcmp(arg, "--destination2") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.destination2 = value;
      continue;
    }
    if (strcmp(arg, "--destination3") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.destination3 = value;
      continue;
    }
    if (strcmp(arg, "--eta1") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.eta1 = value;
      continue;
    }
    if (strcmp(arg, "--eta2") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.eta2 = value;
      continue;
    }
    if (strcmp(arg, "--eta3") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.eta3 = value;
      continue;
    }
    if (strcmp(arg, "--eta-extra1") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.etaExtra1 = value;
      continue;
    }
    if (strcmp(arg, "--eta-extra2") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.etaExtra2 = value;
      continue;
    }
    if (strcmp(arg, "--eta-extra3") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.etaExtra3 = value;
      continue;
    }
    if (strcmp(arg, "--direction1") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.direction1 = value;
      continue;
    }
    if (strcmp(arg, "--direction2") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.direction2 = value;
      continue;
    }
    if (strcmp(arg, "--direction3") == 0) {
      const char *value = require_value(arg);
      if (!value) return false;
      options.direction3 = value;
      continue;
    }

    fprintf(stderr, "Unknown argument: %s\n", arg);
    print_usage(argv[0]);
    return false;
  }

  if (options.scale < 1) {
    options.scale = 1;
  }
  options.displayType = clamp_u8(options.displayType, 1, 5);
  options.rows = clamp_u8(options.rows, 1, core::kMaxVisibleTransitRows);
  return true;
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

  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }

  switch (c) {
    case ' ': return {3, 7, kSpace};
    case '-': return {5, 7, kDash};
    case ':': return {3, 7, kColon};
    case ',': return {3, 7, kComma};
    case '.': return {2, 7, kPeriod};
    case '/': return {5, 7, kSlash};
    case '\'': return {2, 7, kApostrophe};
    case '0': return {5, 7, k0};
    case '1': return {5, 7, k1};
    case '2': return {5, 7, k2};
    case '3': return {5, 7, k3};
    case '4': return {5, 7, k4};
    case '5': return {5, 7, k5};
    case '6': return {5, 7, k6};
    case '7': return {5, 7, k7};
    case '8': return {5, 7, k8};
    case '9': return {5, 7, k9};
    case 'A': return {5, 7, kA};
    case 'B': return {5, 7, kB};
    case 'C': return {5, 7, kC};
    case 'D': return {5, 7, kD};
    case 'E': return {5, 7, kE};
    case 'F': return {5, 7, kF};
    case 'G': return {5, 7, kG};
    case 'H': return {5, 7, kH};
    case 'I': return {5, 7, kI};
    case 'J': return {5, 7, kJ};
    case 'K': return {5, 7, kK};
    case 'L': return {5, 7, kL};
    case 'M': return {5, 7, kM};
    case 'N': return {5, 7, kN};
    case 'O': return {5, 7, kO};
    case 'P': return {5, 7, kP};
    case 'Q': return {5, 7, kQ};
    case 'R': return {5, 7, kR};
    case 'S': return {5, 7, kS};
    case 'T': return {5, 7, kT};
    case 'U': return {5, 7, kU};
    case 'V': return {5, 7, kV};
    case 'W': return {5, 7, kW};
    case 'X': return {5, 7, kX};
    case 'Y': return {5, 7, kY};
    case 'Z': return {5, 7, kZ};
    default: return {5, 7, kSpace};
  }
}

Glyph tiny_glyph(char c) {
  static constexpr uint8_t kSpace[] = {0x0, 0x0, 0x0, 0x0, 0x0};
  static constexpr uint8_t kDash[] = {0x0, 0x0, 0x7, 0x0, 0x0};
  static constexpr uint8_t kColon[] = {0x0, 0x2, 0x0, 0x2, 0x0};
  static constexpr uint8_t kComma[] = {0x0, 0x0, 0x0, 0x6, 0x2};
  static constexpr uint8_t kPeriod[] = {0x0, 0x0, 0x0, 0x0, 0x2};
  static constexpr uint8_t kSlash[] = {0x1, 0x1, 0x2, 0x4, 0x4};
  static constexpr uint8_t k0[] = {0x7, 0x5, 0x5, 0x5, 0x7};
  static constexpr uint8_t k1[] = {0x2, 0x6, 0x2, 0x2, 0x7};
  static constexpr uint8_t k2[] = {0x7, 0x1, 0x7, 0x4, 0x7};
  static constexpr uint8_t k3[] = {0x7, 0x1, 0x3, 0x1, 0x7};
  static constexpr uint8_t k4[] = {0x5, 0x5, 0x7, 0x1, 0x1};
  static constexpr uint8_t k5[] = {0x7, 0x4, 0x7, 0x1, 0x7};
  static constexpr uint8_t k6[] = {0x3, 0x4, 0x7, 0x5, 0x7};
  static constexpr uint8_t k7[] = {0x7, 0x1, 0x2, 0x2, 0x2};
  static constexpr uint8_t k8[] = {0x7, 0x5, 0x7, 0x5, 0x7};
  static constexpr uint8_t k9[] = {0x7, 0x5, 0x7, 0x1, 0x6};
  static constexpr uint8_t kA[] = {0x2, 0x5, 0x7, 0x5, 0x5};
  static constexpr uint8_t kB[] = {0x6, 0x5, 0x6, 0x5, 0x6};
  static constexpr uint8_t kC[] = {0x3, 0x4, 0x4, 0x4, 0x3};
  static constexpr uint8_t kD[] = {0x6, 0x5, 0x5, 0x5, 0x6};
  static constexpr uint8_t kE[] = {0x7, 0x4, 0x6, 0x4, 0x7};
  static constexpr uint8_t kF[] = {0x7, 0x4, 0x6, 0x4, 0x4};
  static constexpr uint8_t kG[] = {0x3, 0x4, 0x5, 0x5, 0x3};
  static constexpr uint8_t kH[] = {0x5, 0x5, 0x7, 0x5, 0x5};
  static constexpr uint8_t kI[] = {0x7, 0x2, 0x2, 0x2, 0x7};
  static constexpr uint8_t kJ[] = {0x1, 0x1, 0x1, 0x5, 0x2};
  static constexpr uint8_t kK[] = {0x5, 0x5, 0x6, 0x5, 0x5};
  static constexpr uint8_t kL[] = {0x4, 0x4, 0x4, 0x4, 0x7};
  static constexpr uint8_t kM[] = {0x5, 0x7, 0x7, 0x5, 0x5};
  static constexpr uint8_t kN[] = {0x5, 0x7, 0x7, 0x7, 0x5};
  static constexpr uint8_t kO[] = {0x2, 0x5, 0x5, 0x5, 0x2};
  static constexpr uint8_t kP[] = {0x6, 0x5, 0x6, 0x4, 0x4};
  static constexpr uint8_t kQ[] = {0x2, 0x5, 0x5, 0x3, 0x1};
  static constexpr uint8_t kR[] = {0x6, 0x5, 0x6, 0x5, 0x5};
  static constexpr uint8_t kS[] = {0x3, 0x4, 0x2, 0x1, 0x6};
  static constexpr uint8_t kT[] = {0x7, 0x2, 0x2, 0x2, 0x2};
  static constexpr uint8_t kU[] = {0x5, 0x5, 0x5, 0x5, 0x7};
  static constexpr uint8_t kV[] = {0x5, 0x5, 0x5, 0x5, 0x2};
  static constexpr uint8_t kW[] = {0x5, 0x5, 0x7, 0x7, 0x5};
  static constexpr uint8_t kX[] = {0x5, 0x5, 0x2, 0x5, 0x5};
  static constexpr uint8_t kY[] = {0x5, 0x5, 0x2, 0x2, 0x2};
  static constexpr uint8_t kZ[] = {0x7, 0x1, 0x2, 0x4, 0x7};

  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }

  switch (c) {
    case ' ': return {3, 5, kSpace};
    case '-': return {3, 5, kDash};
    case ':': return {1, 5, kColon};
    case ',': return {2, 5, kComma};
    case '.': return {1, 5, kPeriod};
    case '/': return {3, 5, kSlash};
    case '0': return {3, 5, k0};
    case '1': return {3, 5, k1};
    case '2': return {3, 5, k2};
    case '3': return {3, 5, k3};
    case '4': return {3, 5, k4};
    case '5': return {3, 5, k5};
    case '6': return {3, 5, k6};
    case '7': return {3, 5, k7};
    case '8': return {3, 5, k8};
    case '9': return {3, 5, k9};
    case 'A': return {3, 5, kA};
    case 'B': return {3, 5, kB};
    case 'C': return {3, 5, kC};
    case 'D': return {3, 5, kD};
    case 'E': return {3, 5, kE};
    case 'F': return {3, 5, kF};
    case 'G': return {3, 5, kG};
    case 'H': return {3, 5, kH};
    case 'I': return {3, 5, kI};
    case 'J': return {3, 5, kJ};
    case 'K': return {3, 5, kK};
    case 'L': return {3, 5, kL};
    case 'M': return {3, 5, kM};
    case 'N': return {3, 5, kN};
    case 'O': return {3, 5, kO};
    case 'P': return {3, 5, kP};
    case 'Q': return {3, 5, kQ};
    case 'R': return {3, 5, kR};
    case 'S': return {3, 5, kS};
    case 'T': return {3, 5, kT};
    case 'U': return {3, 5, kU};
    case 'V': return {3, 5, kV};
    case 'W': return {3, 5, kW};
    case 'X': return {3, 5, kX};
    case 'Y': return {3, 5, kY};
    case 'Z': return {3, 5, kZ};
    default: return {3, 5, kSpace};
  }
}

class HostPreviewDisplayEngine final : public display::DisplayEngine {
 public:
  HostPreviewDisplayEngine(int width, int height)
      : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height, kColorBlack) {}

  void clear(uint16_t color) { std::fill(pixels_.begin(), pixels_.end(), color); }

  void draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size, uint16_t bg) override {
    draw_text_internal(x, y, text, color, size, false, bg);
  }

  void draw_text_transparent(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size) override {
    draw_text_internal(x, y, text, color, size, true, 0);
  }

  void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w <= 0 || h <= 0) {
      return;
    }
    for (int16_t yy = 0; yy < h; ++yy) {
      for (int16_t xx = 0; xx < w; ++xx) {
        draw_pixel(x + xx, y + yy, color);
      }
    }
  }

  void draw_pixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
      return;
    }
    pixels_[static_cast<size_t>(y) * width_ + x] = color;
  }

  void draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (w <= 0) {
      return;
    }
    for (int16_t xx = 0; xx < w; ++xx) {
      draw_pixel(x + xx, y, color);
    }
  }

  display::TextMetrics measure_text(const char *text, uint8_t size) override {
    display::TextMetrics metrics{};
    if (!text || text[0] == '\0') {
      return metrics;
    }

    const bool tiny = (size == kTextSizeTiny || size == kTextSizeTinyPlus);
    const bool embolden = (size == kTextSizeTinyPlus);
    int16_t width = 0;
    int16_t height = 0;
    const int16_t spacing = tiny ? 1 : static_cast<int16_t>(size);

    for (size_t i = 0; text[i] != '\0'; ++i) {
      const Glyph glyph = tiny ? tiny_glyph(text[i]) : regular_glyph(text[i]);
      if (tiny) {
        width = static_cast<int16_t>(width + glyph.width + spacing);
        height = glyph.height;
      } else {
        width = static_cast<int16_t>(width + glyph.width * size + spacing);
        height = static_cast<int16_t>(glyph.height * size);
      }
    }
    if (width > 0) {
      width = static_cast<int16_t>(width - spacing);
    }
    if (embolden) {
      ++width;
    }
    metrics.width = width;
    metrics.height = height;
    return metrics;
  }

  bool write_ppm(const std::string &path, int scale) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      return false;
    }

    const int outputWidth = width_ * scale;
    const int outputHeight = height_ * scale;
    out << "P6\n" << outputWidth << " " << outputHeight << "\n255\n";

    for (int y = 0; y < height_; ++y) {
      for (int ys = 0; ys < scale; ++ys) {
        for (int x = 0; x < width_; ++x) {
          const uint16_t color = pixels_[static_cast<size_t>(y) * width_ + x];
          const uint8_t r = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
          const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
          const uint8_t b = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
          for (int xs = 0; xs < scale; ++xs) {
            out.put(static_cast<char>(r));
            out.put(static_cast<char>(g));
            out.put(static_cast<char>(b));
          }
        }
      }
    }

    return out.good();
  }

 private:
  void draw_text_internal(int16_t x,
                          int16_t y,
                          const char *text,
                          uint16_t color,
                          uint8_t size,
                          bool transparent,
                          uint16_t bg) {
    if (!text) {
      return;
    }

    const bool tiny = (size == kTextSizeTiny || size == kTextSizeTinyPlus);
    const bool embolden = (size == kTextSizeTinyPlus);
    const int16_t spacing = tiny ? 1 : static_cast<int16_t>(size);
    int16_t cursorX = x;

    for (size_t i = 0; text[i] != '\0'; ++i) {
      if (tiny) {
        draw_tiny_char(cursorX, y, text[i], color, transparent, bg, embolden);
        const Glyph glyph = tiny_glyph(text[i]);
        cursorX = static_cast<int16_t>(cursorX + glyph.width + spacing);
      } else {
        draw_regular_char(cursorX, y, text[i], color, transparent, bg, size);
        const Glyph glyph = regular_glyph(text[i]);
        cursorX = static_cast<int16_t>(cursorX + glyph.width * size + spacing);
      }
    }
  }

  void draw_regular_char(int16_t x, int16_t y, char c, uint16_t color, bool transparent, uint16_t bg, uint8_t scale) {
    const Glyph glyph = regular_glyph(c);
    for (uint8_t row = 0; row < glyph.height; ++row) {
      for (uint8_t col = 0; col < glyph.width; ++col) {
        const bool on = (glyph.rows[row] & (1U << (glyph.width - 1 - col))) != 0;
        if (!on && transparent) {
          continue;
        }
        const uint16_t pixelColor = on ? color : bg;
        for (uint8_t yy = 0; yy < scale; ++yy) {
          for (uint8_t xx = 0; xx < scale; ++xx) {
            draw_pixel(static_cast<int16_t>(x + col * scale + xx),
                       static_cast<int16_t>(y + row * scale + yy),
                       pixelColor);
          }
        }
      }
    }
  }

  void draw_tiny_char(int16_t x,
                      int16_t y,
                      char c,
                      uint16_t color,
                      bool transparent,
                      uint16_t bg,
                      bool embolden) {
    const Glyph glyph = tiny_glyph(c);
    for (uint8_t row = 0; row < glyph.height; ++row) {
      for (uint8_t col = 0; col < glyph.width; ++col) {
        const bool on = (glyph.rows[row] & (1U << (glyph.width - 1 - col))) != 0;
        if (!on && transparent) {
          continue;
        }
        const uint16_t pixelColor = on ? color : bg;
        draw_pixel(static_cast<int16_t>(x + col), static_cast<int16_t>(y + row), pixelColor);
        if (on && embolden) {
          draw_pixel(static_cast<int16_t>(x + col + 1), static_cast<int16_t>(y + row), pixelColor);
        }
      }
    }
  }

  int width_;
  int height_;
  std::vector<uint16_t> pixels_;
};

bool is_cta_route(const std::string &route) {
  return route == "RED" || route == "BLUE" || route == "BRN" || route == "G" ||
         route == "ORG" || route == "P" || route == "PINK" || route == "PUR" ||
         route == "Y" || route == "YLW";
}

void apply_row(core::TransitRowModel &row,
               const std::string &provider,
               const std::string &route,
               const std::string &destination,
               const std::string &eta,
               const std::string &etaExtra,
               const std::string &direction) {
  (void)direction;
  memset(&row, 0, sizeof(row));
  row.displayType = 1;
  row.scrollEnabled = false;
  row.delayed = false;
  copy_cstr(row.destination, destination.empty() ? "--" : destination);
  copy_cstr(row.eta, eta.empty() ? "--" : eta);
  copy_cstr(row.etaExtra, etaExtra);
  row.badgeShape = route.size() <= 1 ? core::kBadgeShapeCircle : core::kBadgeShapePill;
  const char *resolvedProvider =
      provider.empty() ? (is_cta_route(route) ? "cta-subway" : "mta-subway") : provider.c_str();
  copy_cstr(row.providerId, resolvedProvider);
  row.badgeColor = transit::MtaColorMap::color_for_provider_route(row.providerId, route.c_str());
  copy_cstr(row.badgeText, route.empty() ? "--" : route);
}

core::RenderModel build_model(const PreviewOptions &options) {
  core::RenderModel model{};
  model.displayType = clamp_u8(options.displayType, 1, 5);
  model.activeRows = clamp_u8(options.rows, 1, core::kMaxVisibleTransitRows);
  model.updatedAtMs = 0;

  if (options.scenario == "setup") {
    model.uiState = core::UiState::kSetupMode;
    model.hasData = false;
    copy_cstr(model.statusLine, "SET UP");
    copy_cstr(model.statusDetail, "Scan QR and use the app");
    copy_cstr(model.bleName, options.bleName);
    return model;
  }

  if (options.scenario == "waiting") {
    model.uiState = core::UiState::kConnectedWaitingData;
    model.hasData = false;
    copy_cstr(model.statusLine, "ADD A LINE");
    copy_cstr(model.statusDetail, "Open the app to get started");
    return model;
  }

  model.uiState = core::UiState::kTransit;
  model.hasData = true;

  if (options.scenario == "transit-compact") {
    model.displayType = 4;
    model.activeRows = 1;
  }

  apply_row(model.rows[0],
            options.provider1,
            options.route1,
            options.destination1,
            options.eta1,
            options.etaExtra1,
            options.direction1);

  apply_row(model.rows[1],
            options.provider2,
            options.route2,
            options.destination2,
            options.eta2,
            options.etaExtra2,
            options.direction2);

  apply_row(model.rows[2],
            options.provider3,
            options.route3,
            options.destination3,
            options.eta3,
            options.etaExtra3,
            options.direction3);

  if (model.displayType == 4 || model.displayType == 5) {
    if (model.rows[0].etaExtra[0] == '\0') {
      copy_cstr(model.rows[0].etaExtra, "6,12,18");
    }
  }

  return model;
}

int run_preview(const PreviewOptions &options) {
  core::LayoutEngine layout;
  layout.set_viewport(kMatrixWidth, kMatrixHeight);

  core::RenderModel model = build_model(options);
  core::DrawList drawList{};
  layout.build_transit_layout(model, drawList);

  HostPreviewDisplayEngine display(kMatrixWidth, kMatrixHeight);
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
        badgeRenderer.draw_badge(display, cmd.x, cmd.y, cmd.w, cmd.text, cmd.color);
        break;
      case core::DrawCommandType::kRectBadge:
        badgeRenderer.draw_rect_badge(display,
                                      cmd.x,
                                      cmd.y,
                                      cmd.w,
                                      cmd.h,
                                      cmd.text,
                                      cmd.color,
                                      static_cast<display::RoundedBadgeStyle>(cmd.size));
        break;
      case core::DrawCommandType::kMonoBitmap:
        if (!cmd.bitmap || cmd.w <= 0 || cmd.h <= 0) {
          break;
        }
        for (int16_t y = 0; y < cmd.h; ++y) {
          for (int16_t x = 0; x < cmd.w; ++x) {
            const uint8_t pixel =
                cmd.bitmap[static_cast<size_t>(y) * static_cast<size_t>(cmd.w) + static_cast<size_t>(x)];
            display.draw_pixel(static_cast<int16_t>(cmd.x + x),
                               static_cast<int16_t>(cmd.y + y),
                               pixel ? cmd.color : cmd.bg);
          }
        }
        break;
      default:
        break;
    }
  }

  if (!display.write_ppm(options.outputPath, options.scale)) {
    fprintf(stderr, "Failed to write preview to %s\n", options.outputPath.c_str());
    return 1;
  }

  fprintf(stdout, "Wrote LED preview to %s\n", options.outputPath.c_str());
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  PreviewOptions options;
  if (!parse_args(argc, argv, options)) {
    return (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) ? 0 : 1;
  }
  return run_preview(options);
}
