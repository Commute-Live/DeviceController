#include "core/mqtt_client.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

namespace core {

MqttClient *MqttClient::activeInstance_ = nullptr;

namespace {
constexpr uint32_t kRetryBaseMs = 1000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kRetryJitterMs = 750;

uint32_t bounded_backoff(uint8_t attempt) {
  uint32_t waitMs = kRetryBaseMs;
  const uint8_t capped = attempt > 8 ? 8 : attempt;
  for (uint8_t i = 0; i < capped; ++i) {
    if (waitMs >= kRetryMaxMs / 2U) {
      waitMs = kRetryMaxMs;
      break;
    }
    waitMs *= 2U;
  }
  const uint32_t jitter = random(kRetryJitterMs + 1);
  if (waitMs > kRetryMaxMs - jitter) {
    return kRetryMaxMs;
  }
  return waitMs + jitter;
}

}  // namespace

MqttClient::MqttClient()
    : config_{},
      topics_{},
      wifiClient_(),
      mqtt_(wifiClient_),
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

  mqtt_.setServer(config_.host, config_.port);
  mqtt_.setBufferSize(kMaxPayloadLen);
  mqtt_.setKeepAlive(30);

  activeInstance_ = this;
  mqtt_.setCallback(&MqttClient::global_on_message);
  return true;
}

void MqttClient::tick(uint32_t nowMs) {
  ensure_connected(nowMs);
  if (connected_) {
    mqtt_.loop();
  }
}

bool MqttClient::connected() {
  return connected_ && mqtt_.connected();
}

bool MqttClient::ensure_connected(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    connected_ = false;
    return false;
  }

  if (mqtt_.connected()) {
    connected_ = true;
    retryCount_ = 0;
    return true;
  }

  connected_ = false;
  if (nowMs < nextRetryAtMs_) {
    return false;
  }

  const bool hasUser = config_.username[0] != '\0';
  const bool hasPass = config_.password[0] != '\0';

  bool ok = false;
  if (hasUser && hasPass) {
    ok = mqtt_.connect(config_.clientId, config_.username, config_.password, topics_.state, 1, true, "offline");
  } else {
    ok = mqtt_.connect(config_.clientId, topics_.state, 1, true, "offline");
  }

  if (ok) {
    connected_ = true;
    retryCount_ = 0;
    nextRetryAtMs_ = nowMs;

    if (!mqtt_.subscribe(topics_.command)) {
      Serial.printf("[MQTT] Failed to subscribe command topic %s\n", topics_.command);
    } else {
      Serial.printf("[MQTT] Subscribed %s\n", topics_.command);
    }

    publish_state("online", true);
    return true;
  }

  const uint32_t waitMs = bounded_backoff(retryCount_);
  retryCount_++;
  nextRetryAtMs_ = nowMs + waitMs;
  Serial.printf("[MQTT] Connect failed rc=%d, retry in %lu ms\n", mqtt_.state(), static_cast<unsigned long>(waitMs));
  return false;
}

void MqttClient::set_command_callback(CommandCallback callback, void *ctx) {
  commandCallback_ = callback;
  commandCtx_ = ctx;
}

bool MqttClient::publish_state(const char *payload, bool retained) {
  if (!connected()) {
    return false;
  }
  return mqtt_.publish(topics_.state, payload, retained);
}

bool MqttClient::publish_heartbeat(const char *payload) {
  if (!connected()) {
    return false;
  }
  return mqtt_.publish(topics_.heartbeat, payload, false);
}

bool MqttClient::publish_event(const char *payload) {
  if (!connected()) {
    return false;
  }
  return mqtt_.publish(topics_.event, payload, false);
}

bool MqttClient::publish_telemetry(const char *payload) {
  if (!connected()) {
    return false;
  }
  return mqtt_.publish(topics_.telemetry, payload, false);
}

bool MqttClient::build_default_topics(const char *deviceId, MqttTopics &outTopics) {
  if (!deviceId || deviceId[0] == '\0') {
    return false;
  }

  const int s = snprintf(outTopics.state, sizeof(outTopics.state), "device/%s/state", deviceId);
  const int c = snprintf(outTopics.command, sizeof(outTopics.command), "/device/%s/commands", deviceId);
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

void MqttClient::global_on_message(char *topic, uint8_t *payload, unsigned int len) {
  if (!activeInstance_) {
    return;
  }
  activeInstance_->on_message(topic, payload, len);
}

void MqttClient::on_message(char *topic, uint8_t *payload, unsigned int len) {
  if (!commandCallback_) {
    return;
  }
  commandCallback_(topic, payload, len, commandCtx_);
}

}  // namespace core
