#include "ble/ble_provisioner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "core/logging.h"

namespace ble {

static const char *kServiceUuid   = "a1b2c3d4-0000-4a5b-8c7d-9e0f1a2b3c4d";
static const char *kProvisionUuid = "a1b2c3d4-0001-4a5b-8c7d-9e0f1a2b3c4d";
static const char *kStatusUuid    = "a1b2c3d4-0002-4a5b-8c7d-9e0f1a2b3c4d";

BleProvisioner *BleProvisioner::sInstance_ = nullptr;

namespace {

void set_ready_status(void *statusChar, const char *deviceId) {
  if (!statusChar || !deviceId) {
    return;
  }

  auto *chr = reinterpret_cast<NimBLECharacteristic *>(statusChar);
  char readyJson[128];
  snprintf(readyJson, sizeof(readyJson), "{\"status\":\"ready\",\"deviceId\":\"%s\"}", deviceId);
  chr->setValue(reinterpret_cast<const uint8_t *>(readyJson), strlen(readyJson));
}

}  // namespace

// GATT write callback — forwards to static handler.
class ProvisionWriteCallback : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic *chr) override {
    const std::string val = chr->getValue();
    BleProvisioner::handle_write(reinterpret_cast<const uint8_t *>(val.data()), val.size());
  }
};

// Server disconnect callback — NimBLE 1.4.x auto-restarts advertising after every
// disconnect. If stop() was already called (provisioning done), cancel that restart.
class ProvisionServerCallbacks : public NimBLEServerCallbacks {
 public:
  void onDisconnect(NimBLEServer *) override {
    if (BleProvisioner::sInstance_ && !BleProvisioner::sInstance_->advertising_) {
      NimBLEDevice::getAdvertising()->stop();
    }
  }
};

static ProvisionWriteCallback sWriteCallback;
static ProvisionServerCallbacks sServerCallbacks;

void BleProvisioner::begin(const char *bleName, const char *deviceId) {
  sInstance_ = this;
  credPending_ = false;
  memset(&pendingCreds_, 0, sizeof(pendingCreds_));

  if (!initialized_) {
    credCb_ = nullptr;
    credCbCtx_ = nullptr;
    statusChar_ = nullptr;

    NimBLEDevice::init(bleName);   // BLE advertised name — what the app scans for
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEServer  *server  = NimBLEDevice::createServer();
    server->setCallbacks(&sServerCallbacks);
    NimBLEService *service = server->createService(kServiceUuid);

    // PROVISION characteristic: app writes WiFi credentials as JSON.
    service->createCharacteristic(kProvisionUuid, NIMBLE_PROPERTY::WRITE)
           ->setCallbacks(&sWriteCallback);

    // STATUS characteristic: device notifies connection outcome.
    NimBLECharacteristic *statusChr =
        service->createCharacteristic(kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    statusChar_ = statusChr;
    set_ready_status(statusChar_, deviceId);

    service->start();
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv) {
      adv->addServiceUUID(kServiceUuid);
      adv->setScanResponse(true);
      adv->start();
      advertising_ = true;
    }
    initialized_ = true;
    DCTRL_LOGI("BLE", "Advertising started bleName=%s deviceId=%s serviceUuid=%s",
               core::logging::safe_str(bleName),
               core::logging::safe_str(deviceId),
               kServiceUuid);
    return;
  }

  set_ready_status(statusChar_, deviceId);
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (adv) {
    adv->start();
    advertising_ = true;
  }
  DCTRL_LOGI("BLE", "Advertising resumed bleName=%s deviceId=%s serviceUuid=%s",
             core::logging::safe_str(bleName),
             core::logging::safe_str(deviceId),
             kServiceUuid);
}

void BleProvisioner::stop() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (adv) {
    adv->stop();
  }
  advertising_ = false;
  DCTRL_LOGI("BLE", "Advertising stopped");
}

void BleProvisioner::notify_status(const char *statusJson) {
  if (!statusChar_ || !statusJson) return;
  auto *chr = reinterpret_cast<NimBLECharacteristic *>(statusChar_);
  chr->setValue(reinterpret_cast<const uint8_t *>(statusJson), strlen(statusJson));
  chr->notify();
  DCTRL_LOGI("BLE", "Status notification sent payload=%s", statusJson);
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

  const String ssid      = extract("ssid");
  const String password  = extract("password");
  const String username  = extract("username");
  const String token     = extract("token");
  const String serverUrl = extract("server_url");

  if (ssid.length() == 0) {
    DCTRL_LOGW("BLE", "Provision write ignored because ssid was missing payload=%s", msg.c_str());
    return;
  }

  BleCredentials &c = sInstance_->pendingCreds_;
  strncpy(c.ssid,      ssid.c_str(),      sizeof(c.ssid)      - 1);  c.ssid[sizeof(c.ssid) - 1]           = '\0';
  strncpy(c.password,  password.c_str(),  sizeof(c.password)  - 1);  c.password[sizeof(c.password) - 1]   = '\0';
  strncpy(c.username,  username.c_str(),  sizeof(c.username)  - 1);  c.username[sizeof(c.username) - 1]   = '\0';
  strncpy(c.token,     token.c_str(),     sizeof(c.token)     - 1);  c.token[sizeof(c.token) - 1]         = '\0';
  strncpy(c.serverUrl, serverUrl.c_str(), sizeof(c.serverUrl) - 1);  c.serverUrl[sizeof(c.serverUrl) - 1] = '\0';

  sInstance_->credPending_ = true;
  DCTRL_LOGI("BLE", "Credentials received ssid=%s passwordLen=%u enterprise=%s token=%s",
             c.ssid,
             static_cast<unsigned>(strlen(c.password)),
             core::logging::bool_str(c.username[0] != '\0'),
             token.length() > 0 ? "yes" : "no");
}

}  // namespace ble
