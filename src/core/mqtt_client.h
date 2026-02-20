#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

constexpr size_t kMaxTopicLen = 96;
constexpr size_t kMaxPayloadLen = 512;

struct MqttConfig {
  char host[64];
  uint16_t port;
  char username[64];
  char password[64];
  char clientId[48];
};

struct MqttTopics {
  char state[kMaxTopicLen];
  char command[kMaxTopicLen];
  char heartbeat[kMaxTopicLen];
  char event[kMaxTopicLen];
  char telemetry[kMaxTopicLen];
};

class MqttClient final {
 public:
  using CommandCallback = void (*)(const char *topic, const uint8_t *payload, size_t len, void *ctx);

  MqttClient();

  bool begin(const MqttConfig &config, const MqttTopics &topics);
  void tick(uint32_t nowMs);
  bool connected() const;
  bool ensure_connected(uint32_t nowMs);
  void set_command_callback(CommandCallback callback, void *ctx);

  bool publish_state(const char *payload, bool retained);
  bool publish_heartbeat(const char *payload);
  bool publish_event(const char *payload);
  bool publish_telemetry(const char *payload);

  static bool build_default_topics(const char *deviceId, MqttTopics &outTopics);

 private:
  MqttConfig config_;
  MqttTopics topics_;
  bool connected_;
  uint32_t nextRetryAtMs_;
  uint8_t retryCount_;
  CommandCallback commandCallback_;
  void *commandCtx_;
};

}  // namespace core
