#pragma once

#include <stdint.h>

#include "core/config_store.h"
#include "core/display_engine.h"

namespace core {
namespace calibration {

// Runs interactive serial-driven display calibration when requested.
// Returns true if calibration mode was entered (saved or canceled), false otherwise.
bool maybe_run(DisplayEngine &display,
               ConfigStore &configStore,
               DeviceRuntimeConfig &runtimeConfig,
               uint32_t enterWindowMs = 5000);

}  // namespace calibration
}  // namespace core

