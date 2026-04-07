#include "parsing/provider_parser_router.h"

#include "parsing/providers/boston/mbta_payload_parser.h"
#include "parsing/providers/chicago/cta_subway_payload_parser.h"
#include "parsing/providers/nyc/mta_bus_payload_parser.h"
#include "parsing/providers/nyc/mta_subway_payload_parser.h"
#include "parsing/providers/philadelphia/septa_bus_payload_parser.h"
#include "parsing/providers/philadelphia/septa_rail_payload_parser.h"

namespace parsing {

bool is_supported_provider_id(const String &provider) {
  String p = provider;
  p.trim();
  p.toLowerCase();
  return p == "mta-subway" ||
         p == "mta-lirr" ||
         p == "mta-mnr" ||
         p == "mta-bus" ||
         p == "mbta-subway" ||
         p == "mbta-bus" ||
         p == "mbta-rail" ||
         p == "njt-rail" ||
         p == "njt-bus" ||
         p == "cta-bus" ||
         p == "mbta" ||
         p == "cta-subway" ||
         p == "septa-rail" ||
         p == "septa-bus" ||
         p == "septa-trolley";
}

bool parse_provider_payload(const String &provider, const String &message, ProviderPayload &out) {
  String p = provider;
  p.trim();
  p.toLowerCase();

  if (p == "mta-subway" || p == "mta-lirr" || p == "mta-mnr") {
    return parse_mta_subway_payload(message, out);
  }
  if (p == "njt-rail") {
    return parse_septa_rail_payload(message, out);
  }
  if (p == "njt-bus") {
    return parse_septa_bus_payload(message, out);
  }
  if (p == "cta-subway") {
    return parse_cta_subway_payload(message, out);
  }
  if (p == "mta-bus" || p == "cta-bus") {
    return parse_mta_bus_payload(message, out);
  }
  if (p == "mbta" || p == "mbta-subway" || p == "mbta-bus" || p == "mbta-rail") {
    return parse_mbta_payload(message, out);
  }
  if (p == "septa-rail") {
    return parse_septa_rail_payload(message, out);
  }
  if (p == "septa-bus") {
    return parse_septa_bus_payload(message, out);
  }
  if (p == "septa-trolley") {
    return parse_septa_bus_payload(message, out);
  }

  return false;
}

}  // namespace parsing
