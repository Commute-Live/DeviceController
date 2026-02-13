#pragma once

#include <Arduino.h>
#include "parsing/provider_payload.h"

namespace parsing {

bool parse_mta_bus_payload(const String &message, ProviderPayload &out);

}  // namespace parsing
