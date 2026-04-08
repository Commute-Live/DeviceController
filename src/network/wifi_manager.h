#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace wifi_manager {

using ProvisioningProgressCallback = void (*)(const char *phase,
                                              int wifiStatus,
                                              uint8_t attempt,
                                              uint8_t totalAttempts,
                                              void *ctx);

void save_credentials(const String &ssid, const String &password, const String &user = "");
bool load_credentials(String &ssid, String &password, String &user);
void clear_credentials();
void reset_station_state(bool erasePersistentConfig);

// Blocking: attempts connection and waits up to ~7.5s for result.
// Use only from the one-time startup connect in NetworkManager::begin().
bool connect_station(const char *ssid, const char *password, const char *username = "");

// Blocking: tries the same station credentials a few times in fast succession.
// Use for interactive provisioning flows that need a prompt success/failure result.
bool connect_station_for_provisioning(const char *ssid, const char *password, const char *username = "",
                                     int *finalStatusOut = nullptr,
                                     ProvisioningProgressCallback progressCb = nullptr,
                                     void *progressCtx = nullptr);

// Non-blocking: configures WPA/WPA2-Enterprise if needed and calls WiFi.begin(), then returns.
// Caller must poll WiFi.status() (or use NetworkManager::tick()) to detect the result.
void begin_station(const char *ssid, const char *password, const char *username = "");

// Builds a per-device AP SSID of the form "CommuteLive-XXXX" using the chip MAC.
void build_ap_ssid(char *out, size_t outLen);

// Generates an 8-char random alphanumeric AP password on first call and persists it to NVS.
// Returns the same password on all subsequent calls (survives reboots).
String generate_or_load_ap_password();

bool handle_connect_request(WebServer &server, String &connectedSsid, String &connectedPassword, String &connectedUser);

// Performs a WiFi scan and calls emitChunk(jsonChunk, ctx) for each chunk of results.
// Each chunk is a self-contained JSON object: {"c":0,"t":2,"n":[{"s":"SSID","r":-45,"e":3},...]}
// Returns the total number of networks found.
int scan_and_emit(void (*emitChunk)(const char *json, void *ctx), void *ctx);

int last_disconnect_reason();
const char *disconnect_reason_name(int reason);

}  // namespace wifi_manager
