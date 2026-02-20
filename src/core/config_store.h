#pragma once

#include <stdint.h>
#include "core/models.h"
#include "core/mqtt_client.h"
#include "core/network_manager.h"

namespace core {

struct DeviceRuntimeConfig {
  uint16_t schemaVersion;
  char deviceId[kMaxDeviceIdLen];
  DisplayConfig display;
  NetworkConfig network;
  MqttConfig mqtt;
};

class ConfigStore final {
 public:
  ConfigStore();

  bool begin();
  bool load(DeviceRuntimeConfig &outConfig);
  bool save(const DeviceRuntimeConfig &config);

 private:
  static constexpr uint16_t kCurrentSchemaVersion = 1;
};

}  // namespace core
