#include "core/config_store.h"

#include <Preferences.h>
#include <string.h>

namespace core {

namespace {
constexpr const char *kPrefsNs = "corecfg";
constexpr const char *kKeySchema = "schema";
constexpr const char *kKeyDid = "did";
constexpr const char *kKeyRows = "rows";
constexpr const char *kKeyCols = "cols";
constexpr const char *kKeyPW = "pw";
constexpr const char *kKeyPH = "ph";
constexpr const char *kKeyBr = "br";
constexpr const char *kKeySerp = "serp";
constexpr const char *kKeyDb = "db";

void copy_str(char *dst, size_t dstLen, const char *src) {
  if (dstLen == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

void apply_defaults(DeviceRuntimeConfig &cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.schemaVersion = 1;
  copy_str(cfg.deviceId, sizeof(cfg.deviceId), "esp32-unknown");

  cfg.display.panelRows = 1;
  cfg.display.panelCols = 2;
  cfg.display.panelWidth = 64;
  cfg.display.panelHeight = 32;
  cfg.display.brightness = 80;
  cfg.display.serpentine = false;
  cfg.display.doubleBuffered = false;
}

void sanitize_display(DisplayConfig &cfg) {
  if (cfg.panelRows < 1) cfg.panelRows = 1;
  if (cfg.panelRows > 4) cfg.panelRows = 4;
  if (cfg.panelCols < 1) cfg.panelCols = 1;
  if (cfg.panelCols > 8) cfg.panelCols = 8;
  if (cfg.panelWidth < 32) cfg.panelWidth = 32;
  if (cfg.panelWidth > 128) cfg.panelWidth = 128;
  if (cfg.panelHeight < 16) cfg.panelHeight = 16;
  if (cfg.panelHeight > 64) cfg.panelHeight = 64;
  if (cfg.brightness < 1) cfg.brightness = 1;
  if (cfg.brightness > 255) cfg.brightness = 255;
}

}  // namespace

ConfigStore::ConfigStore() : bootstrapConfig_{}, hasBootstrapConfig_(false) {}

bool ConfigStore::begin() { return true; }

bool ConfigStore::load(DeviceRuntimeConfig &outConfig) {
  if (hasBootstrapConfig_) {
    outConfig = bootstrapConfig_;
    return true;
  }

  apply_defaults(outConfig);

  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return true;
  }

  outConfig.schemaVersion = prefs.getUShort(kKeySchema, kCurrentSchemaVersion);
  String did = prefs.getString(kKeyDid, outConfig.deviceId);
  copy_str(outConfig.deviceId, sizeof(outConfig.deviceId), did.c_str());

  outConfig.display.panelRows = prefs.getUChar(kKeyRows, outConfig.display.panelRows);
  outConfig.display.panelCols = prefs.getUChar(kKeyCols, outConfig.display.panelCols);
  outConfig.display.panelWidth = prefs.getUShort(kKeyPW, outConfig.display.panelWidth);
  outConfig.display.panelHeight = prefs.getUShort(kKeyPH, outConfig.display.panelHeight);
  outConfig.display.brightness = prefs.getUChar(kKeyBr, outConfig.display.brightness);
  outConfig.display.serpentine = prefs.getBool(kKeySerp, outConfig.display.serpentine);
  outConfig.display.doubleBuffered = prefs.getBool(kKeyDb, outConfig.display.doubleBuffered);
  prefs.end();

  sanitize_display(outConfig.display);
  return true;
}

bool ConfigStore::save(const DeviceRuntimeConfig &config) {
  DeviceRuntimeConfig next = config;
  next.schemaVersion = kCurrentSchemaVersion;
  sanitize_display(next.display);

  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    return false;
  }

  prefs.putUShort(kKeySchema, next.schemaVersion);
  prefs.putString(kKeyDid, next.deviceId);
  prefs.putUChar(kKeyRows, next.display.panelRows);
  prefs.putUChar(kKeyCols, next.display.panelCols);
  prefs.putUShort(kKeyPW, next.display.panelWidth);
  prefs.putUShort(kKeyPH, next.display.panelHeight);
  prefs.putUChar(kKeyBr, next.display.brightness);
  prefs.putBool(kKeySerp, next.display.serpentine);
  prefs.putBool(kKeyDb, next.display.doubleBuffered);
  prefs.end();
  return true;
}

void ConfigStore::set_bootstrap_config(const DeviceRuntimeConfig &config) {
  bootstrapConfig_ = config;
  bootstrapConfig_.schemaVersion = kCurrentSchemaVersion;
  hasBootstrapConfig_ = true;
}

}  // namespace core
