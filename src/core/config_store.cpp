#include "core/config_store.h"

#include <string.h>

namespace core {

ConfigStore::ConfigStore() : bootstrapConfig_{}, hasBootstrapConfig_(false) {}

bool ConfigStore::begin() { return true; }

bool ConfigStore::load(DeviceRuntimeConfig &outConfig) {
  if (hasBootstrapConfig_) {
    outConfig = bootstrapConfig_;
    return true;
  }

  memset(&outConfig, 0, sizeof(outConfig));
  outConfig.schemaVersion = kCurrentSchemaVersion;
  strncpy(outConfig.deviceId, "esp32-unknown", sizeof(outConfig.deviceId) - 1);

  outConfig.display.panelRows = 1;
  outConfig.display.panelCols = 2;
  outConfig.display.panelWidth = 64;
  outConfig.display.panelHeight = 32;
  outConfig.display.brightness = 80;
  outConfig.display.serpentine = false;
  outConfig.display.doubleBuffered = true;
  return true;
}

bool ConfigStore::save(const DeviceRuntimeConfig &config) {
  (void)config;
  return true;
}

void ConfigStore::set_bootstrap_config(const DeviceRuntimeConfig &config) {
  bootstrapConfig_ = config;
  bootstrapConfig_.schemaVersion = kCurrentSchemaVersion;
  hasBootstrapConfig_ = true;
}

}  // namespace core
