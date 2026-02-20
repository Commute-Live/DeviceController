#include "core/mqtt_client.h"

#include <stdio.h>
#include <string.h>

namespace core {

namespace {
constexpr uint32_t kRetryBaseMs = 1000;
constexpr uint32_t kRetryMaxMs = 60000;
}  // namespace

MqttClient::MqttClient()
    : config_{},
      topics_{},
      connected_(false),
      nextRetryAtMs_(0),
      retryCount_(0),
      commandCallback_(nullptr),
      commandCtx_(nullptr) {}

bool MqttClient::begin(const MqttConfig &config, const MqttTopics &topics) {
  config_ = config;
  topics_ = topics;
  connected_ = false;
  nextRetryAtMs_ = 0;
  retryCount_ = 0;
  return true;
}

void MqttClient::tick(uint32_t nowMs) {
  (void)ensure_connected(nowMs);
}

bool MqttClient::connected() const { return connected_; }

bool MqttClient::ensure_connected(uint32_t nowMs) {
  if (connected_) {
    return true;
  }
  if (nowMs < nextRetryAtMs_) {
    return false;
  }

  retryCount_++;
  const uint8_t shift = retryCount_ > 5 ? 5 : retryCount_;
  const uint32_t backoff = kRetryBaseMs << shift;
  nextRetryAtMs_ = nowMs + (backoff > kRetryMaxMs ? kRetryMaxMs : backoff);

  // Stub behavior: real implementation should attempt broker connect and subscribe topics_.command.
  connected_ = false;
  return connected_;
}

void MqttClient::set_command_callback(CommandCallback callback, void *ctx) {
  commandCallback_ = callback;
  commandCtx_ = ctx;
}

bool MqttClient::publish_state(const char *payload, bool retained) {
  (void)payload;
  (void)retained;
  return connected_;
}

bool MqttClient::publish_heartbeat(const char *payload) {
  (void)payload;
  return connected_;
}

bool MqttClient::publish_event(const char *payload) {
  (void)payload;
  return connected_;
}

bool MqttClient::publish_telemetry(const char *payload) {
  (void)payload;
  return connected_;
}

bool MqttClient::build_default_topics(const char *deviceId, MqttTopics &outTopics) {
  if (!deviceId || deviceId[0] == '\0') {
    return false;
  }

  const int s = snprintf(outTopics.state, sizeof(outTopics.state), "device/%s/state", deviceId);
  const int c = snprintf(outTopics.command, sizeof(outTopics.command), "device/%s/command", deviceId);
  const int h = snprintf(outTopics.heartbeat, sizeof(outTopics.heartbeat), "device/%s/heartbeat", deviceId);
  const int e = snprintf(outTopics.event, sizeof(outTopics.event), "device/%s/event", deviceId);
  const int t = snprintf(outTopics.telemetry, sizeof(outTopics.telemetry), "device/%s/telemetry", deviceId);

  return s > 0 && c > 0 && h > 0 && e > 0 && t > 0 &&
         s < static_cast<int>(sizeof(outTopics.state)) &&
         c < static_cast<int>(sizeof(outTopics.command)) &&
         h < static_cast<int>(sizeof(outTopics.heartbeat)) &&
         e < static_cast<int>(sizeof(outTopics.event)) &&
         t < static_cast<int>(sizeof(outTopics.telemetry));
}

}  // namespace core
