#include "ble/ble_provisioner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

namespace ble {

static const char *kServiceUuid   = "a1b2c3d4-0000-4a5b-8c7d-9e0f1a2b3c4d";
static const char *kProvisionUuid = "a1b2c3d4-0001-4a5b-8c7d-9e0f1a2b3c4d";
static const char *kStatusUuid    = "a1b2c3d4-0002-4a5b-8c7d-9e0f1a2b3c4d";

BleProvisioner *BleProvisioner::sInstance_ = nullptr;

// GATT write callback — forwards to static handler.
class ProvisionWriteCallback : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic *chr) override {
    const std::string val = chr->getValue();
    BleProvisioner::handle_write(reinterpret_cast<const uint8_t *>(val.data()), val.size());
  }
};

static ProvisionWriteCallback sWriteCallback;

void BleProvisioner::begin(const char *bleName, const char *deviceId) {
  sInstance_    = this;
  credCb_       = nullptr;
  credCbCtx_    = nullptr;
  credPending_  = false;
  statusChar_   = nullptr;
  memset(&pendingCreds_, 0, sizeof(pendingCreds_));

  NimBLEDevice::init(bleName);   // BLE advertised name — what the app scans for
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer  *server  = NimBLEDevice::createServer();
  NimBLEService *service = server->createService(kServiceUuid);

  // PROVISION characteristic: app writes WiFi credentials as JSON.
  service->createCharacteristic(kProvisionUuid, NIMBLE_PROPERTY::WRITE)
         ->setCallbacks(&sWriteCallback);

  // STATUS characteristic: device notifies connection outcome.
  NimBLECharacteristic *statusChr =
      service->createCharacteristic(kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  char readyJson[128];
  snprintf(readyJson, sizeof(readyJson), "{\"status\":\"ready\",\"deviceId\":\"%s\"}", deviceId);
  statusChr->setValue(readyJson);
  statusChar_ = statusChr;

  service->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kServiceUuid);
  adv->setScanResponse(true);
  adv->start();

  Serial.printf("[BLE] Advertising as '%s' (deviceId=%s)\n", bleName, deviceId);
}

bool BleProvisioner::restart_advertising() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (!adv || !statusChar_) {
    return false;
  }

  adv->stop();
  adv->start();
  Serial.println("[BLE] Advertising restarted");
  return true;
}

void BleProvisioner::stop() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (adv) {
    adv->stop();
  }
  Serial.println("[BLE] Advertising stopped");
}

void BleProvisioner::notify_status(const char *statusJson) {
  if (!statusChar_ || !statusJson) return;
  auto *chr = reinterpret_cast<NimBLECharacteristic *>(statusChar_);
  chr->setValue(statusJson);
  chr->notify();
}

void BleProvisioner::set_credentials_callback(OnCredentials cb, void *ctx) {
  credCb_    = cb;
  credCbCtx_ = ctx;
}

bool BleProvisioner::credentials_pending() {
  return credPending_;
}

BleCredentials BleProvisioner::take_credentials() {
  BleCredentials out = pendingCreds_;
  credPending_ = false;
  return out;
}

// static
void BleProvisioner::handle_write(const uint8_t *data, size_t len) {
  if (!sInstance_ || !data || len == 0 || len >= 512) return;

  char buf[512];
  memcpy(buf, data, len);
  buf[len] = '\0';
  const String msg(buf);

  // Minimal JSON field extraction — no heap allocation.
  auto extract = [&](const char *key) -> String {
    String search = String("\"") + key + "\":\"";
    int pos = msg.indexOf(search);
    if (pos < 0) return "";
    int start = pos + static_cast<int>(search.length());
    int end   = msg.indexOf('"', start);
    if (end < 0) return "";
    return msg.substring(start, end);
  };

  const String ssid     = extract("ssid");
  const String password = extract("password");
  const String username = extract("username");

  if (ssid.length() == 0) {
    Serial.println("[BLE] Write ignored: missing ssid");
    return;
  }

  BleCredentials &c = sInstance_->pendingCreds_;
  strncpy(c.ssid,     ssid.c_str(),     sizeof(c.ssid)     - 1);  c.ssid[sizeof(c.ssid) - 1]         = '\0';
  strncpy(c.password, password.c_str(), sizeof(c.password) - 1);  c.password[sizeof(c.password) - 1] = '\0';
  strncpy(c.username, username.c_str(), sizeof(c.username) - 1);  c.username[sizeof(c.username) - 1] = '\0';

  sInstance_->credPending_ = true;
  Serial.printf("[BLE] Credentials received: ssid='%s'\n", c.ssid);
}

}  // namespace ble
