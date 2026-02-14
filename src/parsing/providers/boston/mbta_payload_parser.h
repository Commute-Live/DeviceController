#pragma once

#include "parsing/provider_payload.h"

namespace parsing {

bool parse_mbta_payload(const String &message, ProviderPayload &out);

}  // namespace parsing
