#include "parsing/generic_payload_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "parsing/payload_parser.h"

namespace parsing {

namespace {

template <size_t N>
void copy_cstr(char (&dst)[N], const String &src) {
  strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0';
}

// Parse "#RRGGBB" hex string to RGB565. Returns gray on failure.
uint16_t hex_color_to_rgb565(const char *hex) {
  if (!hex || hex[0] == '\0') return 0x8410;
  const char *s = (hex[0] == '#') ? hex + 1 : hex;
  if (strlen(s) < 6) return 0x8410;

  char rStr[3] = {s[0], s[1], '\0'};
  char gStr[3] = {s[2], s[3], '\0'};
  char bStr[3] = {s[4], s[5], '\0'};

  const uint8_t r = static_cast<uint8_t>(strtoul(rStr, nullptr, 16));
  const uint8_t g = static_cast<uint8_t>(strtoul(gStr, nullptr, 16));
  const uint8_t b = static_cast<uint8_t>(strtoul(bStr, nullptr, 16));

  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Extract a nested JSON object by key. Returns the raw "{...}" substring or "".
String extract_json_object_field(const String &json, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\"";
  const int keyPos = json.indexOf(needle);
  if (keyPos < 0) return "";

  const int colonPos = json.indexOf(':', keyPos + needle.length());
  if (colonPos < 0) return "";

  int objStart = colonPos + 1;
  while (objStart < (int)json.length() && json[objStart] == ' ') objStart++;
  if (objStart >= (int)json.length() || json[objStart] != '{') return "";

  int depth = 0;
  for (int i = objStart; i < (int)json.length(); ++i) {
    const char c = json[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) return json.substring(objStart, i + 1);
    }
  }
  return "";
}

// Get the JSON object at lines[index]. Returns true on success.
bool extract_line_at(const String &json, int index, String &out) {
  const String needle = "\"lines\"";
  const int arrKeyPos = json.indexOf(needle);
  if (arrKeyPos < 0) return false;

  const int colonPos = json.indexOf(':', arrKeyPos + needle.length());
  if (colonPos < 0) return false;

  const int arrStart = json.indexOf('[', colonPos);
  if (arrStart < 0) return false;

  int depth = 0;
  int itemStart = -1;
  int itemIndex = -1;

  for (int i = arrStart; i < (int)json.length(); ++i) {
    const char c = json[i];
    if (c == '[' || c == '{') {
      if (c == '{' && depth == 1) {
        itemIndex++;
        itemStart = i;
      }
      depth++;
    } else if (c == ']' || c == '}') {
      depth--;
      if (c == '}' && depth == 1 && itemIndex == index) {
        out = json.substring(itemStart, i + 1);
        return true;
      }
    }
  }
  return false;
}

void parse_line_into_row(const String &lineJson, ProviderRow &row) {
  row.label = extract_json_string_field(lineJson, "label");
  copy_cstr(row.providerId, extract_json_string_field(lineJson, "provider"));

  // badge
  const String badgeJson = extract_json_object_field(lineJson, "badge");
  if (badgeJson.length() > 0) {
    const String shape = extract_json_string_field(badgeJson, "shape");
    const String color = extract_json_string_field(badgeJson, "color");
    const String text  = extract_json_string_field(badgeJson, "text");

    row.badgeShape = (shape == "circle") ? 0 : 1;
    row.badgeColor = hex_color_to_rgb565(color.c_str());
    const size_t textLen = min((size_t)text.length(), (size_t)4);
    memcpy(row.badgeText, text.c_str(), textLen);
    row.badgeText[textLen] = '\0';
  }

  // status
  String status = extract_json_string_field(lineJson, "status");
  status.toLowerCase();
  row.delayed = (status == "delayed");

  // scrolling
  row.scrollEnabled = extract_json_bool_field(lineJson, "scrolling", false);

  // primary ETA = etas[0]
  String etas[1];
  const int etaCount = extract_json_string_array_field(lineJson, "etas", etas, 1);
  row.eta = (etaCount > 0) ? etas[0] : "--";
}

// Build etaExtra string from etas[1..n], comma-separated.
String build_eta_extra(const String &lineJson) {
  String etas[8];
  const int count = extract_json_string_array_field(lineJson, "etas", etas, 8);
  if (count <= 1) return "";
  String extra = "";
  for (int i = 1; i < count; ++i) {
    if (i > 1) extra += " ";
    etas[i].replace("m", "M");
    etas[i].replace("M", "M");
    extra += etas[i];
  }
  return extra;
}


}  // namespace

bool parse_generic_v2_payload(const String &message, ProviderPayload &out) {
  out = {};

  String line0Json, line1Json;
  const bool hasLine0 = extract_line_at(message, 0, line0Json);
  const bool hasLine1 = extract_line_at(message, 1, line1Json);

  if (!hasLine0) return false;

  out.hasRow1 = true;
  parse_line_into_row(line0Json, out.row1);
  out.row1EtaExtra = build_eta_extra(line0Json);

  if (hasLine1) {
    out.hasRow2 = true;
    parse_line_into_row(line1Json, out.row2);
    out.row2EtaExtra = build_eta_extra(line1Json);
  }

  return true;
}

}  // namespace parsing
