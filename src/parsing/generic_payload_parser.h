#pragma once

#include <Arduino.h>
#include "parsing/provider_parser_router.h"

namespace parsing {

/**
 * Parses a v2 server payload (contains "v":2 field).
 * Extracts pre-computed badge info and ETAs directly — no city-specific logic.
 * Returns true if at least one row was successfully parsed.
 */
bool parse_generic_v2_payload(const String &message, ProviderPayload &out);

}  // namespace parsing
