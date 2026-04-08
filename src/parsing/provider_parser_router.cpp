#include "parsing/provider_parser_router.h"

#include "parsing/generic_payload_parser.h"

namespace parsing {

bool parse_transit_payload(const String &message, ProviderPayload &out) {
  return parse_generic_v2_payload(message, out);
}

}  // namespace parsing
