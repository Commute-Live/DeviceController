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

static int extract_next_eta_list(const String &json, String outEtas[], int maxCount) {
  int arrPos = json.indexOf("\"nextArrivals\"");
  if (arrPos < 0 || maxCount <= 0) return 0;

  int count = 0;
  int pos = arrPos;
  while (count < maxCount) {
    int keyPos = json.indexOf("\"eta\":\"", pos);
    if (keyPos < 0) break;
    int valueStart = keyPos + strlen("\"eta\":\"");
    int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) break;
    outEtas[count++] = json.substring(valueStart, valueEnd);
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
    if (mins <= 1) return "DUE";
    return String(mins) + "m";
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
  String etaValues[3];
  int etaCount = extract_next_eta_list(json, etaValues, 3);
  if (etaCount > 0) {
    bool sawDue = false;
    for (int i = 0; i < etaCount; i++) {
      String eta = etaValues[i];
      eta.trim();
      eta.toUpperCase();
      if (eta.length() == 0 || eta == "--") continue;
      if (eta == "DUE" || eta == "NOW") {
        sawDue = true;
        continue;
      }
      return etaValues[i];
    }
    if (sawDue) return "DUE";
  }

  String fetchedAt = extract_json_string_field(json, "fetchedAt");
  if (fetchedAt.length() == 0) fetchedAt = fallbackFetchedAt;

  time_t fetchedTs = 0;
  bool hasFetchedTs = fetchedAt.length() > 0 && parse_iso8601(fetchedAt, fetchedTs);

  String arrivals[3];
  int n = extract_next_arrival_list(json, arrivals, 3);
  if (n <= 0) return "--";

  // Prefer the first non-DUE ETA so riders can still see a minute value when
  // the nearest prediction is only a few seconds away.
  bool sawDue = false;
  for (int i = 0; i < n; i++) {
    String label = "--";
    if (hasFetchedTs) {
      label = eta_label_for_arrival(arrivals[i], fetchedTs);
    } else if (arrivals[i].length() >= 16) {
      label = arrivals[i].substring(11, 16);
    }
    if (label == "--") continue;
    if (label == "DUE") {
      sawDue = true;
      continue;
    }
    return label;
  }

  if (sawDue) return "DUE";
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
  String firstItemJson = "";
  String firstLine = "";
  String firstProvider = "";
  String firstLabel = "";
  String firstEta = "";

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
    String directionLabel = extract_json_string_field(item, "destination");
    if (directionLabel.length() == 0) {
      directionLabel = extract_json_string_field(item, "directionLabel");
    }
    if (directionLabel.length() == 0) {
      directionLabel = extract_json_string_field(item, "stop");
    }

    String etaText = extract_json_string_field(item, "eta");
    if (etaText.length() == 0) {
      etaText = format_arrivals_compact(item, fallbackFetchedAt);
    }
    if (rowCount == 0) {
      primaryLine = line;
      row1Provider = provider;
      row1Label = directionLabel;
      row1Eta = etaText;
      firstItemJson = item;
      firstLine = line;
      firstProvider = provider;
      firstLabel = directionLabel;
      firstEta = etaText;
    } else if (rowCount == 1) {
      row2Line = line;
      row2Provider = provider;
      row2Label = directionLabel;
      row2Eta = etaText;
    }
    rowCount++;
  }

  // If only one configured line is present, render second row with the next ETA
  // from the same line instead of leaving row2 empty.
  if (rowCount == 1 && firstItemJson.length() > 0) {
    String etaValues[3];
    int etaCount = extract_next_eta_list(firstItemJson, etaValues, 3);
    if (etaCount > 1) {
      String nextEta = "--";
      for (int i = 1; i < etaCount; i++) {
        String candidate = etaValues[i];
        candidate.trim();
        candidate.toUpperCase();
        if (candidate.length() == 0 || candidate == "--") continue;
        nextEta = etaValues[i];
        break;
      }
      if (nextEta == "--" && etaCount > 1) {
        nextEta = etaValues[1];
      }

      row2Line = firstLine;
      row2Provider = firstProvider;
      row2Label = firstLabel;
      row2Eta = nextEta;
      rowCount = 2;
    }
  }

  return rowCount > 0;
}
