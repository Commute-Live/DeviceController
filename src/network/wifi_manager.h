#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace wifi_manager {

bool start_ap(const char *apSsid, const char *apPassword);

void save_credentials(const String &ssid, const String &password, const String &user = "");

bool load_credentials(String &ssid, String &password, String &user);

bool connect_station(const char *ssid, const char *password, const char *username = "");

bool handle_connect_request(WebServer &server, String &connectedSsid, String &connectedPassword, String &connectedUser);

}  // namespace wifi_manager
