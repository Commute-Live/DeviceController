#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace wifi_manager {

bool start_ap(const char *apSsid, const char *apPassword);

void save_credentials(const String &ssid, const String &password, const String &user = "");
bool load_credentials(String &ssid, String &password, String &user);
void clear_credentials();

// Blocking: attempts connection and waits up to ~7.5s for result.
// Use only from the one-time startup connect in NetworkManager::begin().
bool connect_station(const char *ssid, const char *password, const char *username = "");

// Non-blocking: configures WPA/WPA2-Enterprise if needed and calls WiFi.begin(), then returns.
// Caller must poll WiFi.status() (or use NetworkManager::tick()) to detect the result.
void begin_station(const char *ssid, const char *password, const char *username = "");

// Builds a per-device AP SSID of the form "CommuteLive-XXXX" using the chip MAC.
void build_ap_ssid(char *out, size_t outLen);

// Generates an 8-char random alphanumeric AP password on first call and persists it to NVS.
// Returns the same password on all subsequent calls (survives reboots).
String generate_or_load_ap_password();

bool handle_connect_request(WebServer &server, String &connectedSsid, String &connectedPassword, String &connectedUser);

}  // namespace wifi_manager
