#include <Arduino.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
#include <Preferences.h>
#include <ctype.h>
#include <esp_system.h>

#include "calibration.h"

namespace {

constexpr uint16_t panelRows = 1;
constexpr uint16_t panelCols = 2;
constexpr uint16_t panelW = 64;
constexpr uint16_t panelH = 32;
constexpr uint16_t totalW = panelW * panelCols;
constexpr uint16_t totalH = panelH * panelRows;

constexpr uint16_t topMargin = 2;
constexpr uint16_t betweenMargin = 2;
constexpr uint16_t bottomMargin = 2;
constexpr uint32_t blockSwitchMs = 5000;

constexpr uint8_t brightness = 32;
constexpr uint32_t calibrateWindowMs = 5000;

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

constexpr calibration::ChainOption chainOptions[] = {
    {CHAIN_TOP_LEFT_DOWN, "TL_DOWN"},
    {CHAIN_TOP_RIGHT_DOWN, "TR_DOWN"},
    {CHAIN_TOP_LEFT_DOWN_ZZ, "TL_ZZ"},
};

constexpr uint8_t chainOptionCount = sizeof(chainOptions) / sizeof(chainOptions[0]);
constexpr const char *prefsNs = "disp";
constexpr const char *prefsKeyTextSize2 = "b2sz";
constexpr const char *prefsKeyTextSize3 = "b3sz";
constexpr const char *prefsKeyTextOffX = "btx";
constexpr const char *prefsKeyTextOffY = "bty";

struct RouteEntry {
  const char *label;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Derived from src/nyc-subway-colors.json.
constexpr RouteEntry nycRoutes[] = {
    {"1", 238, 53, 46},   {"2", 238, 53, 46},   {"3", 238, 53, 46},   {"4", 0, 147, 60},    {"5", 0, 147, 60},
    {"6", 0, 147, 60},    {"6X", 0, 147, 60},   {"7", 185, 51, 173},  {"7X", 185, 51, 173},  {"A", 0, 57, 166},
    {"C", 0, 57, 166},    {"E", 0, 57, 166},    {"B", 255, 99, 25},   {"D", 255, 99, 25},    {"F", 255, 99, 25},
    {"FX", 255, 99, 25},  {"M", 255, 99, 25},   {"G", 108, 190, 69},  {"J", 153, 102, 51},   {"Z", 153, 102, 51},
    {"L", 167, 169, 172}, {"N", 252, 204, 10},  {"Q", 252, 204, 10},  {"R", 252, 204, 10},   {"W", 252, 204, 10},
    {"S", 128, 129, 131}, {"T", 0, 173, 208},
};

constexpr uint8_t nycRouteCount = sizeof(nycRoutes) / sizeof(nycRoutes[0]);

MatrixPanel_I2S_DMA *gMatrix = nullptr;
VirtualMatrixPanel *gVirtualMatrix = nullptr;
calibration::State gCalState{0, 0, 0, false};
bool gCalibrating = false;
bool gBadgeTuning = false;
uint8_t gActiveBlocks = 2;
uint32_t gLastBlockSwitchAt = 0;

struct BadgeTextConfig {
  uint8_t sizeFor2Blocks;
  uint8_t sizeFor3Blocks;
  int8_t offsetX;
  int8_t offsetY;
};

BadgeTextConfig gBadgeText{2, 1, 0, 0};  // block-2 text is +1 size by default

int16_t apply_x(int16_t x) { return static_cast<int16_t>(x + gCalState.xOffset); }
int16_t apply_y(int16_t y) { return static_cast<int16_t>(y + gCalState.yOffset); }

uint8_t random_route_index(const int8_t *used, uint8_t usedCount) {
  for (uint8_t attempts = 0; attempts < 16; ++attempts) {
    const uint8_t idx = static_cast<uint8_t>(random(nycRouteCount));
    bool duplicate = false;
    for (uint8_t i = 0; i < usedCount; ++i) {
      if (used[i] == static_cast<int8_t>(idx)) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) return idx;
  }
  return static_cast<uint8_t>(random(nycRouteCount));
}

uint16_t label_color_for(const RouteEntry &route) {
  const uint16_t luminance = static_cast<uint16_t>((route.r * 299U) + (route.g * 587U) + (route.b * 114U)) / 1000U;
  return (luminance > 160U) ? gMatrix->color565(0, 0, 0) : gMatrix->color565(255, 255, 255);
}

const RouteEntry *find_route(const char *label) {
  for (uint8_t i = 0; i < nycRouteCount; ++i) {
    if (strcmp(nycRoutes[i].label, label) == 0) return &nycRoutes[i];
  }
  return nullptr;
}

void load_badge_text_config() {
  Preferences prefs;
  if (!prefs.begin(prefsNs, true)) return;
  gBadgeText.sizeFor2Blocks = prefs.getUChar(prefsKeyTextSize2, gBadgeText.sizeFor2Blocks);
  gBadgeText.sizeFor3Blocks = prefs.getUChar(prefsKeyTextSize3, gBadgeText.sizeFor3Blocks);
  gBadgeText.offsetX = prefs.getChar(prefsKeyTextOffX, gBadgeText.offsetX);
  gBadgeText.offsetY = prefs.getChar(prefsKeyTextOffY, gBadgeText.offsetY);
  prefs.end();
  if (gBadgeText.sizeFor2Blocks < 1) gBadgeText.sizeFor2Blocks = 1;
  if (gBadgeText.sizeFor2Blocks > 3) gBadgeText.sizeFor2Blocks = 3;
  if (gBadgeText.sizeFor3Blocks < 1) gBadgeText.sizeFor3Blocks = 1;
  if (gBadgeText.sizeFor3Blocks > 3) gBadgeText.sizeFor3Blocks = 3;
}

void save_badge_text_config() {
  Preferences prefs;
  if (!prefs.begin(prefsNs, false)) return;
  prefs.putUChar(prefsKeyTextSize2, gBadgeText.sizeFor2Blocks);
  prefs.putUChar(prefsKeyTextSize3, gBadgeText.sizeFor3Blocks);
  prefs.putChar(prefsKeyTextOffX, gBadgeText.offsetX);
  prefs.putChar(prefsKeyTextOffY, gBadgeText.offsetY);
  prefs.end();
}

void draw_route_badge(int16_t cx, int16_t cy, int16_t radius, const RouteEntry &route, uint8_t textSize,
                      bool rectangle) {
  if (!gVirtualMatrix || !gMatrix) return;

  const uint16_t bg = gMatrix->color565(route.r, route.g, route.b);
  const uint16_t fg = label_color_for(route);

  if (rectangle) {
    const int16_t side = static_cast<int16_t>((radius * 2) + 1);
    const int16_t left = static_cast<int16_t>(cx - radius);
    const int16_t top = static_cast<int16_t>(cy - radius);
    gVirtualMatrix->fillRect(apply_x(left), apply_y(top), side, side, bg);
  } else {
    gVirtualMatrix->fillCircle(apply_x(cx), apply_y(cy), radius, bg);
  }

  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  gVirtualMatrix->setTextSize(textSize);
  gVirtualMatrix->setTextWrap(false);
  gVirtualMatrix->getTextBounds(route.label, 0, 0, &x1, &y1, &w, &h);
  const int16_t tx = static_cast<int16_t>(cx - (static_cast<int16_t>(w) / 2) - x1 + gBadgeText.offsetX);
  const int16_t ty = static_cast<int16_t>(cy - (static_cast<int16_t>(h) / 2) - y1 + gBadgeText.offsetY);
  gVirtualMatrix->setTextColor(fg, bg);
  gVirtualMatrix->setCursor(apply_x(tx), apply_y(ty));
  gVirtualMatrix->print(route.label);
}

calibration::Context make_cal_context() {
  return calibration::Context{
      gMatrix,
      &gVirtualMatrix,
      panelRows,
      panelCols,
      panelW,
      panelH,
      totalW,
      totalH,
      chainOptions,
      chainOptionCount,
  };
}

void draw_logo_scene(bool tuningPreview = false) {
  if (!gVirtualMatrix || !gMatrix) return;

  const uint8_t blocks = gActiveBlocks < 1 ? 1 : gActiveBlocks;
  const uint16_t modeTopMargin = (blocks == 3) ? 1 : topMargin;
  const uint16_t modeBetweenMargin = (blocks == 3) ? 1 : betweenMargin;
  const uint16_t modeBottomMargin = (blocks == 3) ? 1 : bottomMargin;
  const uint16_t totalGap = static_cast<uint16_t>(
      modeTopMargin + modeBottomMargin +
      (blocks > 1 ? static_cast<uint16_t>(blocks - 1) * modeBetweenMargin : 0));
  if (totalGap >= totalH) {
    return;
  }
  const uint16_t drawableH = static_cast<uint16_t>(totalH - totalGap);
  const uint16_t blockH = static_cast<uint16_t>(drawableH / blocks);
  if (blockH < 3) {
    return;
  }

  gVirtualMatrix->fillScreen(0);
  int16_t radius = static_cast<int16_t>((blockH - 1) / 2);
  if (blocks == 3) radius = 4;
  if (radius < 1) radius = 1;
  const int16_t cx = static_cast<int16_t>(1 + radius);
  const uint8_t textSize = (blocks == 2) ? gBadgeText.sizeFor2Blocks : gBadgeText.sizeFor3Blocks;
  int8_t usedRouteIndices[3] = {-1, -1, -1};
  const char *tuningRoutes[3] = {"A", "6X", "7"};
  for (uint8_t i = 0; i < blocks; ++i) {
    const RouteEntry *route = nullptr;
    if (tuningPreview) {
      route = find_route(tuningRoutes[i % 3]);
      if (!route) route = &nycRoutes[0];
    } else {
      const uint8_t routeIdx = random_route_index(usedRouteIndices, i);
      usedRouteIndices[i] = static_cast<int8_t>(routeIdx);
      route = &nycRoutes[routeIdx];
    }
    const int16_t blockTop = static_cast<int16_t>(modeTopMargin + i * (blockH + modeBetweenMargin));
    const int16_t cy = static_cast<int16_t>(blockTop + (blockH / 2));
    draw_route_badge(cx, cy, radius, *route, textSize, blocks == 3);
  }
}

void print_badge_tune_help() {
  Serial.println("[TUNE] Commands: u/d text size, j/l x offset, i/k y offset, g toggle 2/3 blocks, p save+exit, x exit");
}

void print_badge_tune_state() {
  Serial.print("[TUNE] blocks=");
  Serial.print(gActiveBlocks);
  Serial.print(" size2=");
  Serial.print(gBadgeText.sizeFor2Blocks);
  Serial.print(" size3=");
  Serial.print(gBadgeText.sizeFor3Blocks);
  Serial.print(" xOff=");
  Serial.print(gBadgeText.offsetX);
  Serial.print(" yOff=");
  Serial.println(gBadgeText.offsetY);
}

void enter_badge_tuning_mode() {
  gBadgeTuning = true;
  Serial.println("[TUNE] Entered badge tuning mode");
  print_badge_tune_help();
  print_badge_tune_state();
  draw_logo_scene(true);
}

void enter_calibration_mode() {
  gCalibrating = true;
  Serial.println("[CAL] Entered calibration mode");
  calibration::print_help();
  calibration::draw_test_pattern(make_cal_context(), gCalState);
}

void exit_calibration_mode(bool save) {
  if (save) {
    gCalState.calibrated = true;
    calibration::save_state(gCalState);
    Serial.print("[CAL] Saved mapping: ");
    Serial.print(chainOptions[gCalState.chainIndex].name);
    Serial.print(" | xOff=");
    Serial.print(gCalState.xOffset);
    Serial.print(" yOff=");
    Serial.println(gCalState.yOffset);
  } else {
    Serial.println("[CAL] Exit without saving");
  }
  gCalibrating = false;
  draw_logo_scene();
  gLastBlockSwitchAt = millis();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(esp_random());

  HUB75_I2S_CFG::i2s_pins pins = {
      kR1Pin, kG1Pin, kB1Pin, kR2Pin, kG2Pin, kB2Pin, kAPin,
      kBPin,  kCPin,  kDPin,  kEPin,  kLatPin, kOePin, kClkPin,
  };

  const uint16_t chainLen = static_cast<uint16_t>(panelRows * panelCols);
  HUB75_I2S_CFG mxConfig(panelW,
                         panelH,
                         chainLen,
                         pins,
                         HUB75_I2S_CFG::SHIFTREG,
                         HUB75_I2S_CFG::TYPE138,
                         false,
                         HUB75_I2S_CFG::HZ_8M,
                         1,
                         true,
                         60);

  gMatrix = new MatrixPanel_I2S_DMA(mxConfig);
  if (!gMatrix || !gMatrix->begin()) {
    Serial.println("[BOOT] Matrix init failed");
    return;
  }

  gMatrix->setBrightness8(brightness);
  load_badge_text_config();

  gCalState = calibration::load_state(chainOptionCount);
  if (!calibration::apply_mapping(make_cal_context(), gCalState)) {
    Serial.println("[BOOT] Virtual matrix init failed");
    return;
  }

  draw_logo_scene();

  if (!gCalState.calibrated) {
    Serial.println("[BOOT] First run detected. Starting calibration...");
    enter_calibration_mode();
    return;
  }

  Serial.println("[BOOT] Send 'c' in 5s to enter calibration mode");
  uint32_t start = millis();
  while ((millis() - start) < calibrateWindowMs) {
    if (Serial.available() > 0) {
      const char ch = static_cast<char>(tolower(Serial.read()));
      if (ch == 'c') {
        enter_calibration_mode();
        return;
      }
    }
    delay(10);
  }

  Serial.print("[BOOT] Using saved mapping: ");
  Serial.print(chainOptions[gCalState.chainIndex].name);
  Serial.print(" | xOff=");
  Serial.print(gCalState.xOffset);
  Serial.print(" yOff=");
  Serial.println(gCalState.yOffset);
}

void loop() {
  if (!gCalibrating && !gBadgeTuning) {
    const uint32_t now = millis();
    if ((now - gLastBlockSwitchAt) >= blockSwitchMs) {
      gActiveBlocks = (gActiveBlocks == 2) ? 3 : 2;
      draw_logo_scene();
      gLastBlockSwitchAt = now;
    }
  }

  if (Serial.available() == 0) {
    delay(20);
    return;
  }

  const char ch = static_cast<char>(tolower(Serial.read()));

  if (!gCalibrating) {
    if (ch == 'c') {
      enter_calibration_mode();
    } else if (ch == 'v') {
      enter_badge_tuning_mode();
    }
    if (!gBadgeTuning) return;
  }

  if (gBadgeTuning && !gCalibrating) {
    if (ch == 'u') {
      uint8_t &size = (gActiveBlocks == 2) ? gBadgeText.sizeFor2Blocks : gBadgeText.sizeFor3Blocks;
      if (size < 3) size++;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'd') {
      uint8_t &size = (gActiveBlocks == 2) ? gBadgeText.sizeFor2Blocks : gBadgeText.sizeFor3Blocks;
      if (size > 1) size--;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'j') {
      gBadgeText.offsetX--;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'l') {
      gBadgeText.offsetX++;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'i') {
      gBadgeText.offsetY--;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'k') {
      gBadgeText.offsetY++;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'g') {
      gActiveBlocks = (gActiveBlocks == 2) ? 3 : 2;
      draw_logo_scene(true);
      print_badge_tune_state();
    } else if (ch == 'p') {
      save_badge_text_config();
      gBadgeTuning = false;
      Serial.println("[TUNE] Saved badge text settings");
      draw_logo_scene();
      gLastBlockSwitchAt = millis();
    } else if (ch == 'x') {
      gBadgeTuning = false;
      Serial.println("[TUNE] Exit without saving");
      draw_logo_scene();
      gLastBlockSwitchAt = millis();
    } else if (ch == 'h') {
      print_badge_tune_help();
      print_badge_tune_state();
    }
    return;
  }

  const calibration::CommandResult result = calibration::handle_command(ch, gCalState, chainOptionCount);

  if (result == calibration::CommandResult::kNoChange) {
    return;
  }

  if (result == calibration::CommandResult::kExitSave) {
    exit_calibration_mode(true);
    return;
  }

  if (result == calibration::CommandResult::kExitNoSave) {
    exit_calibration_mode(false);
    return;
  }

  if (!calibration::apply_mapping(make_cal_context(), gCalState)) {
    Serial.println("[CAL] Failed to apply mapping");
    return;
  }

  calibration::draw_test_pattern(make_cal_context(), gCalState);
}
