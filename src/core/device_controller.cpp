#include "core/device_controller.h"

#include <string.h>

namespace core {

DeviceController::DeviceController(const Dependencies &deps)
    : deps_(deps), runtimeConfig_{}, renderModel_{}, drawList_{} {
  memset(&renderModel_, 0, sizeof(renderModel_));
}

bool DeviceController::begin() {
  if (!deps_.configStore || !deps_.networkManager || !deps_.mqttClient || !deps_.displayEngine ||
      !deps_.layoutEngine || !deps_.providerRegistry) {
    return false;
  }

  if (!deps_.configStore->begin() || !deps_.configStore->load(runtimeConfig_)) {
    return false;
  }
  if (!deps_.displayEngine->begin(runtimeConfig_.display)) {
    return false;
  }

  deps_.layoutEngine->set_viewport(deps_.displayEngine->geometry().totalWidth,
                                   deps_.displayEngine->geometry().totalHeight);
  deps_.networkManager->set_state_callback(&DeviceController::on_network_state_change, this);
  deps_.mqttClient->set_command_callback(&DeviceController::on_mqtt_command, this);
  return deps_.networkManager->begin(runtimeConfig_.network);
}

void DeviceController::tick(uint32_t nowMs) {
  deps_.networkManager->tick(nowMs);
  deps_.mqttClient->tick(nowMs);
  render_frame();
}

void DeviceController::on_network_state_change(NetworkState state, void *ctx) {
  if (!ctx) {
    return;
  }
  static_cast<DeviceController *>(ctx)->handle_network_state(state);
}

void DeviceController::on_mqtt_command(const char *topic, const uint8_t *payload, size_t len, void *ctx) {
  if (!ctx) {
    return;
  }
  static_cast<DeviceController *>(ctx)->handle_command(topic, payload, len);
}

void DeviceController::handle_network_state(NetworkState state) {
  (void)state;
}

void DeviceController::handle_command(const char *topic, const uint8_t *payload, size_t len) {
  (void)topic;
  (void)payload;
  (void)len;
}

void DeviceController::render_frame() {
  if (!deps_.displayEngine->begin_frame()) {
    return;
  }

  deps_.layoutEngine->build_transit_layout(renderModel_, drawList_);
  for (size_t i = 0; i < drawList_.count; ++i) {
    const DrawCommand &cmd = drawList_.commands[i];
    if (cmd.type == DrawCommandType::kFillRect) {
      deps_.displayEngine->fill_rect(cmd.x, cmd.y, cmd.w, cmd.h, cmd.color);
    } else if (cmd.type == DrawCommandType::kText) {
      deps_.displayEngine->draw_text(cmd.x, cmd.y, cmd.text, cmd.color, cmd.size);
    }
  }
  deps_.displayEngine->present();
}

}  // namespace core
