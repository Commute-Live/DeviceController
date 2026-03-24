#pragma once

#include <stdint.h>

namespace core {

enum class NetworkState : uint8_t;
enum class UiState : uint8_t;

namespace debug {

void logf(const char *tag, const char *fmt, ...);

const char *network_state_name(NetworkState state);
const char *ui_state_name(UiState state);
const char *mqtt_state_name(int state);

uint8_t normalize_wifi_disconnect_reason(uint8_t reason);
const char *wifi_disconnect_reason_name(uint8_t reason);

}  // namespace debug
}  // namespace core
