#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace core::logging {

inline const char *bool_str(bool value) {
  return value ? "true" : "false";
}

inline const char *safe_str(const char *value) {
  return value ? value : "(null)";
}

inline const char *wifi_status_name(int status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
#ifdef WL_NO_SHIELD
    case WL_NO_SHIELD:
      return "WL_NO_SHIELD";
#endif
    default:
      return "WL_UNKNOWN";
  }
}

}  // namespace core::logging

#define DCTRL_LOG_PREFIX "[DCTRL]"
#define DCTRL_LOG(level, tag, fmt, ...)                                                               \
  Serial.printf(DCTRL_LOG_PREFIX "[%010lu][%s][%s] " fmt "\n",                                       \
                static_cast<unsigned long>(millis()),                                                 \
                level,                                                                                \
                tag,                                                                                  \
                ##__VA_ARGS__)

#define DCTRL_LOGD(tag, fmt, ...) DCTRL_LOG("DBG", tag, fmt, ##__VA_ARGS__)
#define DCTRL_LOGI(tag, fmt, ...) DCTRL_LOG("INF", tag, fmt, ##__VA_ARGS__)
#define DCTRL_LOGW(tag, fmt, ...) DCTRL_LOG("WRN", tag, fmt, ##__VA_ARGS__)
#define DCTRL_LOGE(tag, fmt, ...) DCTRL_LOG("ERR", tag, fmt, ##__VA_ARGS__)
