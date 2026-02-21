#include <unity.h>

#include "transit/MtaColorMap.h"

void test_known_routes_map_to_expected_colors() {
  TEST_ASSERT_EQUAL_HEX16(0xE986, transit::MtaColorMap::color_for_route("1"));
  TEST_ASSERT_EQUAL_HEX16(0x04A7, transit::MtaColorMap::color_for_route("6"));
  TEST_ASSERT_EQUAL_HEX16(0x01B4, transit::MtaColorMap::color_for_route("A"));
  TEST_ASSERT_EQUAL_HEX16(0xFB23, transit::MtaColorMap::color_for_route("F"));
  TEST_ASSERT_EQUAL_HEX16(0xFE61, transit::MtaColorMap::color_for_route("Q"));
}

void test_extended_and_formatted_routes_map_by_family() {
  TEST_ASSERT_EQUAL_HEX16(0x04A7, transit::MtaColorMap::color_for_route("6X"));
  TEST_ASSERT_EQUAL_HEX16(0xB2B5, transit::MtaColorMap::color_for_route("7X"));
  TEST_ASSERT_EQUAL_HEX16(0x01B4, transit::MtaColorMap::color_for_route(" c "));
  TEST_ASSERT_EQUAL_HEX16(0x9B26, transit::MtaColorMap::color_for_route("z-local"));
}

void test_unknown_or_empty_route_uses_fallback() {
  TEST_ASSERT_EQUAL_HEX16(transit::MtaColorMap::kFallbackColor, transit::MtaColorMap::color_for_route(""));
  TEST_ASSERT_EQUAL_HEX16(transit::MtaColorMap::kFallbackColor, transit::MtaColorMap::color_for_route("X"));
  TEST_ASSERT_EQUAL_HEX16(transit::MtaColorMap::kFallbackColor, transit::MtaColorMap::color_for_route(nullptr));
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_known_routes_map_to_expected_colors);
  RUN_TEST(test_extended_and_formatted_routes_map_by_family);
  RUN_TEST(test_unknown_or_empty_route_uses_fallback);
  UNITY_END();
}

void loop() {}
