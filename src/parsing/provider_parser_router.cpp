#include "parsing/provider_parser_router.h"

#include "parsing/providers/cta_subway_payload_parser.h"
#include "parsing/providers/mta_bus_payload_parser.h"
#include "parsing/providers/mta_payload_parser.h"

namespace parsing {

bool parse_provider_payload(const String &provider, const String &message, ProviderPayload &out) {
  String p = provider;
  p.trim();
  p.toLowerCase();

  if (p == "mta-subway" || p == "mta") {
    return parse_mta_payload(message, out);
  }
  if (p == "cta-subway" || p == "cta") {
    return parse_cta_subway_payload(message, out);
  }
  if (p == "mta-bus") {
    return parse_mta_bus_payload(message, out);
  }

  // Unknown provider: prefer generic bus-like fallback instead of dropping.
  return parse_mta_bus_payload(message, out);
}

}  // namespace parsing
