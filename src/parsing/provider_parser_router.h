#pragma once

#include <Arduino.h>
#include "parsing/provider_payload.h"

namespace parsing {

bool parse_provider_payload(const String &provider, const String &message, ProviderPayload &out);

}  // namespace parsing
