#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

void wifiManagerInit(AsyncWebServer &server);
void wifiManagerLoop();
bool wifiManagerIsConnected();
