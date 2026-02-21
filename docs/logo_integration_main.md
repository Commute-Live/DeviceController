# Logo Integration: logos -> main

## Scope
Controlled integration for NYC subway badge/logo rendering only.

## Classification
- A (Production-ready):
  - Existing `src/display/BadgeRenderer.cpp` circle badge renderer in `main`.
  - Existing spacing/layout in `src/display/LayoutEngine.cpp` + `src/core/layout_engine.cpp`.
  - Existing NYC-only route color map in `src/transit/MtaColorMap.cpp`.
- B (Experimental, excluded):
  - `logos` 5-second runtime block rotation.
  - Dynamic random badge generation in `main.cpp` prototype.
  - Calibration/tuning command runtime for display mapping in app code path.
- C (Debug/test only, moved):
  - NYC color JSON moved to `test/fixtures/nyc-subway-colors.json`.
  - NYC color mapping checks added in `test/test_mta_color_map/test_main.cpp`.

## Files integrated
- `test/fixtures/nyc-subway-colors.json`
- `test/test_mta_color_map/test_main.cpp`

## Files intentionally not merged from logos
- `src/main.cpp` prototype-only runtime logic
- `src/calibration.cpp`
- `src/calibration.h`
- `src/nyc-subway-colors.json` (kept out of production source tree)
- Any 5-second rotation / random rendering behavior

## Provider isolation
- NYC path remains in `src/display/providers/nyc/row_chrome.cpp` and `src/transit/MtaColorMap.cpp`.
- Boston/Chicago/Philadelphia providers unchanged.

## Risk assessment
- Runtime risk: low (no production firmware behavior changed).
- Integration risk: low (test-only additions).
- Remaining risk: visual regressions are still hardware-dependent and require on-device validation.

## Regression checklist
- [ ] Boot firmware and confirm normal transit rendering unchanged.
- [ ] NYC subway route badges still render as circles.
- [ ] NYC route colors match expected families (1/2/3, 4/5/6, 7, ACE, etc.).
- [ ] Non-NYC providers render unchanged.
- [ ] No MQTT/layout runtime behavior changes.

