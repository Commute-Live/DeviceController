#pragma once

#include <stddef.h>
#include <stdint.h>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

namespace core {

constexpr size_t kMaxDeviceIdLen = 40;
constexpr size_t kMaxDestinationLen = 64;
constexpr size_t kMaxEtaLen = 12;
constexpr size_t kMaxErrorLen = 96;
constexpr size_t kMaxStatusLen = 32;
constexpr uint8_t kMaxTransitRows = 3;
constexpr uint8_t kMaxVisibleTransitRows = 2;

struct DisplayConfig {
  uint8_t panelRows;
  uint8_t panelCols;
  uint16_t panelWidth;
  uint16_t panelHeight;
  uint8_t brightness;
  bool serpentine;
  bool doubleBuffered;
  uint8_t chainMode;  // 0=TL_DOWN, 1=TR_DOWN, 2=TL_ZZ
  int8_t xOffset;
  int8_t yOffset;
  uint8_t shiftDriver;   // HUB75_I2S_CFG::shift_driver
  uint8_t lineDriver;    // HUB75_I2S_CFG::line_driver
  uint8_t clockSpeed;    // 0=8MHz, 1=16MHz, 2=20MHz
  uint8_t latchBlanking; // 0..4 clock pulses
  bool clkPhase;
};

struct DisplayGeometry {
  uint16_t totalWidth;
  uint16_t totalHeight;
};

inline bool compute_geometry(const DisplayConfig &cfg, DisplayGeometry &out) {
  if (cfg.panelRows == 0 || cfg.panelCols == 0 || cfg.panelWidth == 0 || cfg.panelHeight == 0) {
    return false;
  }

  const uint32_t totalWidth = static_cast<uint32_t>(cfg.panelCols) * cfg.panelWidth;
  const uint32_t totalHeight = static_cast<uint32_t>(cfg.panelRows) * cfg.panelHeight;

  if (totalWidth > UINT16_MAX || totalHeight > UINT16_MAX) {
    return false;
  }

  out.totalWidth = static_cast<uint16_t>(totalWidth);
  out.totalHeight = static_cast<uint16_t>(totalHeight);
  return true;
}

enum class UiState : uint8_t {
  kBooting,
  kStaleTransit,
  kSetupMode,
  kNoWifi,
  kWifiOkNoMqtt,
  kConnectedWaitingData,
  kBlank,
  kTransit,
};

// Badge shape constants
constexpr uint8_t kBadgeShapeCircle = 0;
constexpr uint8_t kBadgeShapePill   = 1;

struct TransitRowModel {
  uint8_t displayType;   // 1=normal, 4=stacked-eta (set by device_controller)
  bool scrollEnabled;
  bool delayed;
  char destination[kMaxDestinationLen];  // pre-computed label from server
  char eta[kMaxEtaLen];                  // primary ETA string
  char etaExtra[kMaxDestinationLen];     // secondary ETAs comma-separated (triggers stacked layout)
  uint8_t badgeShape;    // kBadgeShapeCircle or kBadgeShapePill
  uint16_t badgeColor;   // RGB565
  char badgeText[5];     // 1 char (circle) or 1-3 chars (pill) + null terminator
};

struct RenderModel {
  UiState uiState;
  bool hasData;
  uint8_t displayType;
  uint8_t activeRows;
  uint32_t updatedAtMs;
  char statusLine[kMaxStatusLen];
  char statusDetail[kMaxDestinationLen];
  char apSsid[64];
  char apPin[12];
  char bleName[64];
  TransitRowModel rows[kMaxTransitRows];
};

}  // namespace core
