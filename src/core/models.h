#pragma once

#include <stddef.h>
#include <stdint.h>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

namespace core {

constexpr size_t kMaxDeviceIdLen = 40;
constexpr size_t kMaxProviderIdLen = 24;
constexpr size_t kMaxRouteIdLen = 24;
constexpr size_t kMaxDestinationLen = 64;
constexpr size_t kMaxEtaLen = 12;
constexpr size_t kMaxErrorLen = 96;
constexpr size_t kMaxStatusLen = 32;
constexpr uint8_t kMaxTransitRows = 3;

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
  kSetupMode,
  kNoWifi,
  kWifiOkNoMqtt,
  kConnectedWaitingData,
  kTransit,
};

struct TransitRowModel {
  char providerId[kMaxProviderIdLen];
  char routeId[kMaxRouteIdLen];
  char direction[kMaxDestinationLen];
  char destination[kMaxDestinationLen];
  char eta[kMaxEtaLen];
  char etaExtra[kMaxDestinationLen];
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
