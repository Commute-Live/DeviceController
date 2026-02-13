#include "parsing/providers/mta_bus_payload_parser.h"

#include "parsing/payload_parser.h"

namespace parsing {

static String first_eta_from_message(const String &message) {
  String eta1, eta2, eta3;
  build_eta_lines(message, eta1, eta2, eta3);

  auto eta_value = [](const String &line) {
    int spacePos = line.indexOf(' ');
    if (spacePos >= 0 && spacePos + 1 < (int)line.length()) {
      return line.substring(spacePos + 1);
    }
    return line;
  };

  String e1 = eta_value(eta1);
  String e2 = eta_value(eta2);
  String e3 = eta_value(eta3);
  if (e1 != "--") return e1;
  if (e2 != "--") return e2;
  if (e3 != "--") return e3;
  return "--";
}

bool parse_mta_bus_payload(const String &message, ProviderPayload &out) {
  out = {};

  String provider = extract_json_string_field(message, "provider");
  if (provider.length() == 0) provider = "mta-bus";

  String line1;
  String provider1;
  String label1;
  String eta1;
  String line2;
  String provider2;
  String label2;
  String eta2;

  if (parse_lines_payload(message, line1, provider1, label1, eta1, line2, provider2, label2, eta2)) {
    out.hasRow1 = line1.length() > 0;
    out.row1.provider = provider1.length() ? provider1 : provider;
    out.row1.line = line1;
    out.row1.label = label1.length() ? label1 : line1;
    out.row1.eta = eta1.length() ? eta1 : "--";

    out.hasRow2 = line2.length() > 0;
    out.row2.provider = provider2.length() ? provider2 : provider;
    out.row2.line = line2;
    out.row2.label = label2.length() ? label2 : line2;
    out.row2.eta = eta2.length() ? eta2 : "--";
    return out.hasRow1;
  }

  String line = extract_json_string_field(message, "line");
  if (line.length() == 0) return false;

  String directionLabel = extract_json_string_field(message, "directionLabel");
  String stop = extract_json_string_field(message, "stop");
  String label = directionLabel.length() ? directionLabel : stop;
  if (label.length() == 0) label = line;

  out.hasRow1 = true;
  out.row1.provider = provider;
  out.row1.line = line;
  out.row1.label = label;
  out.row1.eta = first_eta_from_message(message);
  return true;
}

}  // namespace parsing
