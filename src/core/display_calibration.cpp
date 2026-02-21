#include "core/display_calibration.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <esp_system.h>

namespace core {
namespace calibration {

namespace {

constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kBlue = 0x001F;
constexpr uint16_t kYellow = 0xFFE0;
constexpr const char *kPrefsNs = "corecfg";
constexpr const char *kKeyChain = "chain";
constexpr const char *kKeyXOff = "xoff";
constexpr const char *kKeyYOff = "yoff";

void print_help() {
  Serial.println("[CAL] Commands: n/p map, l/r/t/b edge-fix, i/j/k/m fine XY, u/d up/down, z reset XY, s save, x cancel, h help");
}

void draw_test_pattern(DisplayEngine &display, const DisplayConfig &cfg) {
  const DisplayGeometry &geom = display.geometry();
  const int16_t w = static_cast<int16_t>(geom.totalWidth);
  const int16_t h = static_cast<int16_t>(geom.totalHeight);

  display.clear(0x0000);
  display.draw_rect(0, 0, w, h, kWhite);
  display.draw_hline(0, static_cast<int16_t>(h / 2), w, kWhite);
  for (int16_t y = 0; y < h; ++y) {
    display.draw_pixel(static_cast<int16_t>(w / 2), y, kWhite);
  }

  display.fill_rect(1, 1, 6, 6, kRed);
  display.fill_rect(static_cast<int16_t>(w - 7), 1, 6, 6, kGreen);
  display.fill_rect(1, static_cast<int16_t>(h - 7), 6, 6, kBlue);
  display.fill_rect(static_cast<int16_t>(w - 7), static_cast<int16_t>(h - 7), 6, 6, kYellow);

  const char *mode = "TL_DOWN";
  if (cfg.chainMode == 1) mode = "TR_DOWN";
  if (cfg.chainMode == 2) mode = "TL_ZZ";
  char top[48];
  snprintf(top, sizeof(top), "CAL %s", mode);
  display.draw_text_transparent(10, 2, top, kWhite, 1);
  display.draw_text_transparent(10, static_cast<int16_t>(h - 9), "n p lrtb s", kWhite, 1);

  Serial.printf("[CAL] mode=%s xOff=%d yOff=%d\n", mode, static_cast<int>(cfg.xOffset), static_cast<int>(cfg.yOffset));
}

void cycle_mode(DisplayConfig &cfg, int8_t step) {
  int next = static_cast<int>(cfg.chainMode) + step;
  if (next < 0) next = 2;
  if (next > 2) next = 0;
  cfg.chainMode = static_cast<uint8_t>(next);
}

}  // namespace

bool maybe_run(DisplayEngine &display,
               ConfigStore &configStore,
               DeviceRuntimeConfig &runtimeConfig,
               uint32_t enterWindowMs) {
  {
    Preferences prefs;
    if (prefs.begin(kPrefsNs, true)) {
      runtimeConfig.display.chainMode = prefs.getUChar(kKeyChain, runtimeConfig.display.chainMode);
      runtimeConfig.display.xOffset = prefs.getChar(kKeyXOff, runtimeConfig.display.xOffset);
      runtimeConfig.display.yOffset = prefs.getChar(kKeyYOff, runtimeConfig.display.yOffset);
      prefs.end();
    }
  }

  Serial.printf("[CAL] Send 'c' in %lu ms to calibrate display mapping\n",
                static_cast<unsigned long>(enterWindowMs));

  const uint32_t start = millis();
  bool startCalibration = false;
  while ((millis() - start) < enterWindowMs) {
    if (Serial.available() > 0) {
      const char ch = static_cast<char>(tolower(Serial.read()));
      if (ch == 'c') {
        startCalibration = true;
        break;
      }
    }
    delay(10);
  }

  if (!startCalibration) {
    return false;
  }

  DisplayConfig work = runtimeConfig.display;
  print_help();
  if (!display.begin(work)) {
    Serial.println("[CAL] Display init failed");
    return true;
  }
  display.set_offsets(work.xOffset, work.yOffset);
  draw_test_pattern(display, work);
  display.present();

  while (true) {
    if (Serial.available() == 0) {
      delay(20);
      continue;
    }

    const char ch = static_cast<char>(tolower(Serial.read()));
    bool redraw = false;
    bool remap = false;

    if (ch == 'n') {
      cycle_mode(work, +1);
      remap = true;
      redraw = true;
    } else if (ch == 'p') {
      cycle_mode(work, -1);
      remap = true;
      redraw = true;
    } else if (ch == 'l' || ch == 'm') {
      ++work.xOffset;
      redraw = true;
    } else if (ch == 'r' || ch == 'j') {
      --work.xOffset;
      redraw = true;
    } else if (ch == 't' || ch == 'k' || ch == 'd') {
      ++work.yOffset;
      redraw = true;
    } else if (ch == 'b' || ch == 'i' || ch == 'u') {
      --work.yOffset;
      redraw = true;
    } else if (ch == 'z') {
      work.xOffset = 0;
      work.yOffset = 0;
      redraw = true;
    } else if (ch == 'h') {
      print_help();
    } else if (ch == 'x') {
      Serial.println("[CAL] Exit without saving");
      display.end();
      return true;
    } else if (ch == 's') {
      runtimeConfig.display = work;
      if (!configStore.begin() || !configStore.save(runtimeConfig)) {
        Serial.println("[CAL] Save failed");
      } else {
        Serial.println("[CAL] Saved display calibration");
        Serial.println("[CAL] Restarting to apply mapping cleanly...");
        delay(200);
        ESP.restart();
      }
      display.end();
      return true;
    }

    if (work.xOffset < -32) work.xOffset = -32;
    if (work.xOffset > 32) work.xOffset = 32;
    if (work.yOffset < -16) work.yOffset = -16;
    if (work.yOffset > 16) work.yOffset = 16;

    if (remap) {
      display.end();
      if (!display.begin(work)) {
        Serial.println("[CAL] Display remap failed");
        return true;
      }
    }
    display.set_offsets(work.xOffset, work.yOffset);

    if (redraw) {
      draw_test_pattern(display, work);
      display.present();
    }
  }
}

}  // namespace calibration
}  // namespace core
