#include "payload_parser.h"

#include <ctype.h>
#include <time.h>

String extract_json_string_field(const String &json, const char *field) {
  String key = "\"";
  key += field;
  key += "\"";

  int keyPos = json.indexOf(key);
  if (keyPos < 0) return "";

  int colonPos = json.indexOf(':', keyPos + key.length());
  if (colonPos < 0) return "";

  int i = colonPos + 1;
  while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\t')) i++;
  if (i >= (int)json.length()) return "";

  if (json[i] == '"') {
    int end = json.indexOf('"', i + 1);
    if (end < 0) return "";
    return json.substring(i + 1, end);
  }

  int end = i;
  while (end < (int)json.length()) {
    char c = json[end];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) break;
    end++;
  }

  if (end <= i) return "";
  return json.substring(i, end);
}

static bool parse_iso8601(const String &iso, time_t &out) {
  int y, mo, d, h, mi, s;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
    return false;
  }
  struct tm tmv = {};
  tmv.tm_year = y - 1900;
  tmv.tm_mon = mo - 1;
  tmv.tm_mday = d;
  tmv.tm_hour = h;
  tmv.tm_min = mi;
  tmv.tm_sec = s;
  tmv.tm_isdst = -1;
  out = mktime(&tmv);
  return out != (time_t)-1;
}

static int extract_next_arrival_list(const String &json, String outArrivals[], int maxCount) {
  int arrPos = json.indexOf("\"nextArrivals\"");
  if (arrPos < 0 || maxCount <= 0) return 0;

  int count = 0;
  int pos = arrPos;
  while (count < maxCount) {
    int keyPos = json.indexOf("\"arrivalTime\":\"", pos);
    if (keyPos < 0) break;
    int valueStart = keyPos + strlen("\"arrivalTime\":\"");
    int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) break;
    outArrivals[count++] = json.substring(valueStart, valueEnd);
    pos = valueEnd + 1;
  }
  return count;
}

static String eta_label_for_arrival(const String &arrivalIso, time_t fetchedTs) {
  time_t arrivalTs;
  if (parse_iso8601(arrivalIso, arrivalTs)) {
    long diffSec = (long)difftime(arrivalTs, fetchedTs);
    if (diffSec < 0) diffSec = 0;
    long mins = (diffSec + 59) / 60;
    if (mins == 0) return "NOW";
    return String(mins) + "min";
  }
  if (arrivalIso.length() >= 16) {
    return arrivalIso.substring(11, 16);
  }
  return "--";
}

void build_eta_lines(const String &message, String &line1, String &line2, String &line3) {
  line1 = "N1 --";
  line2 = "N2 --";
  line3 = "N3 --";

  String fetchedAt = extract_json_string_field(message, "fetchedAt");
  time_t fetchedTs = 0;
  bool hasFetchedTs = fetchedAt.length() > 0 && parse_iso8601(fetchedAt, fetchedTs);

  String arrivals[3];
  int n = extract_next_arrival_list(message, arrivals, 3);
  if (n <= 0) return;

  String labels[3];
  for (int i = 0; i < n; i++) {
    if (hasFetchedTs) {
      labels[i] = eta_label_for_arrival(arrivals[i], fetchedTs);
    } else if (arrivals[i].length() >= 16) {
      labels[i] = arrivals[i].substring(11, 16);
    } else {
      labels[i] = "--";
    }
  }

  if (n >= 1) line1 = "N1 " + labels[0];
  if (n >= 2) line2 = "N2 " + labels[1];
  if (n >= 3) line3 = "N3 " + labels[2];
}

static int find_matching_bracket(const String &s, int openPos, char openCh, char closeCh) {
  if (openPos < 0 || openPos >= (int)s.length() || s[openPos] != openCh) return -1;
  int depth = 0;
  bool inQuotes = false;
  bool escapeNext = false;
  for (int i = openPos; i < (int)s.length(); i++) {
    char c = s[i];
    if (escapeNext) {
      escapeNext = false;
      continue;
    }
    if (c == '\\') {
      escapeNext = true;
      continue;
    }
    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }
    if (inQuotes) continue;
    if (c == openCh) depth++;
    else if (c == closeCh) {
      depth--;
      if (depth == 0) return i;
    }
  }
  return -1;
}

static String format_arrivals_compact(const String &json, const String &fallbackFetchedAt) {
  String fetchedAt = extract_json_string_field(json, "fetchedAt");
  if (fetchedAt.length() == 0) fetchedAt = fallbackFetchedAt;

  time_t fetchedTs = 0;
  bool hasFetchedTs = fetchedAt.length() > 0 && parse_iso8601(fetchedAt, fetchedTs);

  String arrivals[3];
  int n = extract_next_arrival_list(json, arrivals, 3);
  if (n <= 0) return "--";

  // Device display uses only the next upcoming ETA (one train min).
  for (int i = 0; i < n; i++) {
    String label = "--";
    if (hasFetchedTs) {
      label = eta_label_for_arrival(arrivals[i], fetchedTs);
    } else if (arrivals[i].length() >= 16) {
      label = arrivals[i].substring(11, 16);
    }
    if (label != "--") return label;
  }

  return "--";
}

bool parse_lines_payload(const String &message,
                         String &primaryLine,
                         String &row1Provider,
                         String &row1Label,
                         String &row1Eta,
                         String &row2Line,
                         String &row2Provider,
                         String &row2Label,
                         String &row2Eta) {
  int linesKeyPos = message.indexOf("\"lines\"");
  if (linesKeyPos < 0) return false;

  int arrayOpen = message.indexOf('[', linesKeyPos);
  if (arrayOpen < 0) return false;
  int arrayClose = find_matching_bracket(message, arrayOpen, '[', ']');
  if (arrayClose < 0) return false;

  String fallbackFetchedAt = extract_json_string_field(message, "fetchedAt");
  String linesJson = message.substring(arrayOpen + 1, arrayClose);

  int cursor = 0;
  int rowCount = 0;
  while (rowCount < 2) {
    int objStart = linesJson.indexOf('{', cursor);
    if (objStart < 0) break;
    int objEnd = find_matching_bracket(linesJson, objStart, '{', '}');
    if (objEnd < 0) break;

    String item = linesJson.substring(objStart, objEnd + 1);
    cursor = objEnd + 1;

    String line = extract_json_string_field(item, "line");
    if (line.length() == 0) continue;
    String provider = extract_json_string_field(item, "provider");
    String directionLabel = extract_json_string_field(item, "directionLabel");
    if (directionLabel.length() == 0) {
      directionLabel = extract_json_string_field(item, "stop");
    }

    String etaText = format_arrivals_compact(item, fallbackFetchedAt);
    if (rowCount == 0) {
      primaryLine = line;
      row1Provider = provider;
      row1Label = directionLabel;
      row1Eta = etaText;
    } else if (rowCount == 1) {
      row2Line = line;
      row2Provider = provider;
      row2Label = directionLabel;
      row2Eta = etaText;
    }
    rowCount++;
  }

  return rowCount > 0;
}
