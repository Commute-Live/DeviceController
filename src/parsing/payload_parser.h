#pragma once

#include <Arduino.h>

String extract_json_string_field(const String &json, const char *field);

void build_eta_lines(const String &message, String &line1, String &line2, String &line3);

bool parse_lines_payload(const String &message,
                         String &primaryLine,
                         String &row1Eta,
                         String &row2Line,
                         String &row2Eta);
