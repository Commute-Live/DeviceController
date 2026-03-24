#include "core/mqtt_client.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "core/logging.h"

namespace core {

MqttClient *MqttClient::activeInstance_ = nullptr;

namespace {
constexpr uint32_t kRetryBaseMs = 1000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kRetryJitterMs = 750;
constexpr int kUnknownWifiStatus = 999;
constexpr int kUnknownMqttState = 999;

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

const char *mqtt_state_name(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT:
      return "MQTT_CONNECTION_TIMEOUT";
    case MQTT_CONNECTION_LOST:
      return "MQTT_CONNECTION_LOST";
    case MQTT_CONNECT_FAILED:
      return "MQTT_CONNECT_FAILED";
    case MQTT_DISCONNECTED:
      return "MQTT_DISCONNECTED";
    case MQTT_CONNECTED:
      return "MQTT_CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL:
      return "MQTT_CONNECT_BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID:
      return "MQTT_CONNECT_BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE:
      return "MQTT_CONNECT_UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS:
      return "MQTT_CONNECT_BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED:
      return "MQTT_CONNECT_UNAUTHORIZED";
    default:
      return "MQTT_UNKNOWN";
  }
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
      lastWifiStatus_(kUnknownWifiStatus),
      lastMqttState_(kUnknownMqttState),
      lastTransportConnected_(false),
      commandCallback_(nullptr),
      commandCtx_(nullptr) {}

bool MqttClient::begin(const MqttConfig &config, const MqttTopics &topics) {
  config_ = config;
  topics_ = topics;
  connected_ = false;
  nextRetryAtMs_ = 0;
  retryCount_ = 0;
  lastWifiStatus_ = kUnknownWifiStatus;
  lastMqttState_ = kUnknownMqttState;
  lastTransportConnected_ = false;

  mqtt_.setServer(config_.host, config_.port);
  mqtt_.setBufferSize(kMaxPayloadLen);
  mqtt_.setKeepAlive(30);

  activeInstance_ = this;
  mqtt_.setCallback(&MqttClient::global_on_message);
  DCTRL_LOGI("MQTT",
             "Configured broker host=%s port=%u clientId=%s auth=%s stateTopic=%s commandTopic=%s",
             core::logging::safe_str(config_.host),
             static_cast<unsigned>(config_.port),
             core::logging::safe_str(config_.clientId),
             core::logging::bool_str(config_.username[0] != '\0'),
             topics_.state,
             topics_.command);
  return true;
}

void MqttClient::tick(uint32_t nowMs) {
  ensure_connected(nowMs);
  if (connected_) {
    const bool loopOk = mqtt_.loop();
    const bool transportConnected = mqtt_.connected();
    const int mqttState = mqtt_.state();
    if (mqttState != lastMqttState_) {
      DCTRL_LOGI("MQTT", "Loop state update transport=%s rc=%d (%s)",
                 core::logging::bool_str(transportConnected),
                 mqttState,
                 mqtt_state_name(mqttState));
      lastMqttState_ = mqttState;
    }
    if (lastTransportConnected_ && !transportConnected) {
      DCTRL_LOGW("MQTT",
                 "Connection dropped during loop wifi=%s rc=%d (%s) ip=%s rssi=%d",
                 core::logging::wifi_status_name(WiFi.status()),
                 mqttState,
                 mqtt_state_name(mqttState),
                 WiFi.localIP().toString().c_str(),
                 WiFi.RSSI());
      connected_ = false;
    }
    lastTransportConnected_ = transportConnected;
    if (!loopOk && !transportConnected) {
      connected_ = false;
    }
  }
}

bool MqttClient::connected() {
  return connected_ && mqtt_.connected();
}

bool MqttClient::ensure_connected(uint32_t nowMs) {
  const int wifiStatus = WiFi.status();
  if (wifiStatus != lastWifiStatus_) {
    DCTRL_LOGI("MQTT", "Observed WiFi status=%s (%d) while managing broker connectivity",
               core::logging::wifi_status_name(wifiStatus),
               wifiStatus);
    lastWifiStatus_ = wifiStatus;
  }

  if (wifiStatus != WL_CONNECTED) {
    if (connected_ || lastTransportConnected_) {
      DCTRL_LOGW("MQTT", "Skipping broker connect because WiFi is %s (%d)",
                 core::logging::wifi_status_name(wifiStatus),
                 wifiStatus);
    }
    connected_ = false;
    lastTransportConnected_ = false;
    return false;
  }

  const bool transportConnected = mqtt_.connected();
  const int mqttState = mqtt_.state();
  if (mqttState != lastMqttState_) {
    DCTRL_LOGI("MQTT", "Broker rc=%d (%s) transport=%s",
               mqttState,
               mqtt_state_name(mqttState),
               core::logging::bool_str(transportConnected));
    lastMqttState_ = mqttState;
  }

  if (transportConnected) {
    if (!connected_ || !lastTransportConnected_) {
      DCTRL_LOGI("MQTT", "Broker session active host=%s port=%u clientId=%s ip=%s rssi=%d",
                 core::logging::safe_str(config_.host),
                 static_cast<unsigned>(config_.port),
                 core::logging::safe_str(config_.clientId),
                 WiFi.localIP().toString().c_str(),
                 WiFi.RSSI());
    }
    connected_ = true;
    lastTransportConnected_ = true;
    retryCount_ = 0;
    return true;
  }

  if (connected_ || lastTransportConnected_) {
    DCTRL_LOGW("MQTT", "Transport disconnected while WiFi still up rc=%d (%s) ip=%s rssi=%d",
               mqttState,
               mqtt_state_name(mqttState),
               WiFi.localIP().toString().c_str(),
               WiFi.RSSI());
  }
  connected_ = false;
  lastTransportConnected_ = false;
  if (nowMs < nextRetryAtMs_) {
    return false;
  }

  const bool hasUser = config_.username[0] != '\0';
  const bool hasPass = config_.password[0] != '\0';
  DCTRL_LOGI("MQTT",
             "Attempting broker connect host=%s port=%u clientId=%s authUser=%s authPass=%s attempt=%u ip=%s rssi=%d nextRetryAt=%lu",
             core::logging::safe_str(config_.host),
             static_cast<unsigned>(config_.port),
             core::logging::safe_str(config_.clientId),
             core::logging::bool_str(hasUser),
             core::logging::bool_str(hasPass),
             static_cast<unsigned>(retryCount_ + 1),
             WiFi.localIP().toString().c_str(),
             WiFi.RSSI(),
             static_cast<unsigned long>(nextRetryAtMs_));

  bool ok = false;
  if (hasUser && hasPass) {
    ok = mqtt_.connect(config_.clientId, config_.username, config_.password, topics_.presence, 0, true, "offline");
  } else {
    ok = mqtt_.connect(config_.clientId, topics_.presence, 0, true, "offline");
  }

  if (ok) {
    connected_ = true;
    retryCount_ = 0;
    nextRetryAtMs_ = nowMs;
    lastTransportConnected_ = true;
    lastMqttState_ = mqtt_.state();
    DCTRL_LOGI("MQTT", "Broker connect succeeded rc=%d (%s) commandTopic=%s presenceTopic=%s",
               lastMqttState_,
               mqtt_state_name(lastMqttState_),
               topics_.command,
               topics_.presence);

    if (!mqtt_.subscribe(topics_.command)) {
      DCTRL_LOGE("MQTT", "Subscribe failed topic=%s", topics_.command);
    } else {
      DCTRL_LOGI("MQTT", "Subscribed topic=%s", topics_.command);
    }

    if (!publish_presence("online", true)) {
      DCTRL_LOGE("MQTT", "Failed to publish initial online presence topic=%s", topics_.presence);
    }
    return true;
  }

  const uint32_t waitMs = bounded_backoff(retryCount_);
  retryCount_++;
  nextRetryAtMs_ = nowMs + waitMs;
  lastMqttState_ = mqtt_.state();
  DCTRL_LOGW("MQTT", "Broker connect failed rc=%d (%s) retryInMs=%lu wifi=%s ip=%s rssi=%d",
             lastMqttState_,
             mqtt_state_name(lastMqttState_),
             static_cast<unsigned long>(waitMs),
             core::logging::wifi_status_name(WiFi.status()),
             WiFi.localIP().toString().c_str(),
             WiFi.RSSI());
  return false;
}

void MqttClient::disconnect(bool publishOffline) {
  DCTRL_LOGI("MQTT", "Disconnect requested publishOffline=%s connected=%s rc=%d (%s)",
             core::logging::bool_str(publishOffline),
             core::logging::bool_str(mqtt_.connected()),
             mqtt_.state(),
             mqtt_state_name(mqtt_.state()));
  if (mqtt_.connected()) {
    if (publishOffline) {
      publish_with_trace(topics_.presence, "offline", true, "presence");
    }
    mqtt_.disconnect();
    DCTRL_LOGI("MQTT", "Broker session closed");
  }
  connected_ = false;
  lastTransportConnected_ = false;
  lastMqttState_ = mqtt_.state();
}

void MqttClient::set_command_callback(CommandCallback callback, void *ctx) {
  commandCallback_ = callback;
  commandCtx_ = ctx;
}

bool MqttClient::publish_state(const char *payload, bool retained) {
  return publish_with_trace(topics_.state, payload, retained, "state");
}

bool MqttClient::publish_presence(const char *payload, bool retained) {
  return publish_with_trace(topics_.presence, payload, retained, "presence");
}

bool MqttClient::publish_heartbeat(const char *payload) {
  return publish_with_trace(topics_.heartbeat, payload, false, "heartbeat");
}

bool MqttClient::publish_event(const char *payload) {
  return publish_with_trace(topics_.event, payload, false, "event");
}

bool MqttClient::publish_telemetry(const char *payload) {
  return publish_with_trace(topics_.telemetry, payload, false, "telemetry");
}

bool MqttClient::build_default_topics(const char *deviceId, MqttTopics &outTopics) {
  if (!deviceId || deviceId[0] == '\0') {
    return false;
  }

  const int s = snprintf(outTopics.state, sizeof(outTopics.state), "device/%s/state", deviceId);
  const int p = snprintf(outTopics.presence, sizeof(outTopics.presence), "device/%s/presence", deviceId);
  const int c = snprintf(outTopics.command, sizeof(outTopics.command), "/device/%s/commands", deviceId);
  const int h = snprintf(outTopics.heartbeat, sizeof(outTopics.heartbeat), "device/%s/heartbeat", deviceId);
  const int e = snprintf(outTopics.event, sizeof(outTopics.event), "device/%s/event", deviceId);
  const int t = snprintf(outTopics.telemetry, sizeof(outTopics.telemetry), "device/%s/telemetry", deviceId);

  return s > 0 && p > 0 && c > 0 && h > 0 && e > 0 && t > 0 &&
         s < static_cast<int>(sizeof(outTopics.state)) &&
         p < static_cast<int>(sizeof(outTopics.presence)) &&
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
    DCTRL_LOGW("MQTT", "Dropping incoming topic=%s len=%u because no command callback is registered",
               core::logging::safe_str(topic),
               len);
    return;
  }
  DCTRL_LOGI("MQTT", "Dispatching incoming topic=%s len=%u", core::logging::safe_str(topic), len);
  commandCallback_(topic, payload, len, commandCtx_);
}

bool MqttClient::publish_with_trace(const char *topic, const char *payload, bool retained, const char *label) {
  if (!connected()) {
    DCTRL_LOGW("MQTT", "Skipping %s publish because client is not connected topic=%s",
               core::logging::safe_str(label),
               core::logging::safe_str(topic));
    return false;
  }

  const bool ok = mqtt_.publish(topic, payload ? payload : "", retained);
  if (ok) {
    DCTRL_LOGI("MQTT", "Published %s topic=%s retained=%s payload=%s",
               core::logging::safe_str(label),
               core::logging::safe_str(topic),
               core::logging::bool_str(retained),
               core::logging::safe_str(payload));
  } else {
    DCTRL_LOGE("MQTT", "Publish failed %s topic=%s retained=%s payload=%s rc=%d (%s)",
               core::logging::safe_str(label),
               core::logging::safe_str(topic),
               core::logging::bool_str(retained),
               core::logging::safe_str(payload),
               mqtt_.state(),
               mqtt_state_name(mqtt_.state()));
  }
  return ok;
}

}  // namespace core
