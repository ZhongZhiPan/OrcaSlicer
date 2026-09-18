# Filament menu validation

Baseline: `C:/Users/24105/Desktop/results/实施.md` (2026-09-18).
Cases: `C:/Users/24105/Documents/cases/耗材分类管理测试用例_测试用例.csv`.

## Reproduction

```powershell
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_TESTING=ON
cmake --build build --config Release --target Snapmaker_Orca filament_menu_tests --parallel 4
build/tests/libslic3r/Release/filament_menu_tests.exe "[filament]~[!benchmark]"
build/tests/libslic3r/Release/filament_menu_tests.exe "[!benchmark]"
```

The performance test measures menu-model construction and view mapping for
50/100/500 aliases and ten vendors. Its P50/P95 **do not measure GUI popup
latency**, and cannot substitute for cold-open/hot-group-switch acceptance.
The generated 3MF roundtrip exercises the generic 3MF serializer and stable
configuration identifiers, not the application's complete project-open workflow.

## CSV execution matrix

Automated coverage below refers to `tests/libslic3r/test_filament_menu.cpp`.
GUI steps require a desktop run; static inspection is not a passing test result.

| ID | Automated coverage / implementation | End-to-end CSV result |
|---|---|---|
| 1 | Origin sections and vendor mapping | GUI pending |
| 2 | System/project sections always grouped | GUI pending |
| 3 | Nine/ten unique user aliases | CRUD GUI pending; ordering conflict below |
| 4 | Flat user section | GUI pending; ordering conflict below |
| 5 | Ten/nine transition after hiding a preset | Deletion/restart GUI pending |
| 6 | Vendor rank, shared ordered fallback | Restart GUI pending |
| 7 | Type sort and missing-type handling | GUI pending; example conflict below |
| 8 | Explicitly out of scope | Blocked: product Top8 stable IDs/version policy |
| 9 | Vendor metadata sorting | Import/restart GUI pending |
| 10 | Empty/undefined vendor | GUI pending |
| 11 | Empty/undefined type | GUI pending |
| 12 | Natural/ASCII/NFC/CJK/special ordering | GUI pending |
| 13 | Stable row identity; single-slot slicing config recovery | Consumer GUI pending |
| 14 | Real/view mapping; selected group scrolling | GUI pending |
| 15 | Generic 3MF config roundtrip | Current application project roundtrip pending |
| 16 | AppConfig stores stable names | Full application restart pending |
| 17 | Rebuild consumes current visible presets | Wizard GUI pending |
| 18 | Invisible entries excluded and groups reconstructed | Wizard GUI pending |
| 19 | Atomic fallback plan; preflight removal checks; slicing guard | Wizard/slicing GUI pending |
| 20 | Model rebuild and alias threshold | Create GUI pending |
| 21 | Source-isolated groups and alias deduplication | Import GUI pending |
| 22 | Stable identity and ordered rebuild | Rename GUI pending |
| 23 | Excluded-preset rejection plan | Delete/restart GUI pending |
| 24 | Vendor/type read on each rebuild | Edit GUI pending |
| 25 | Project entry with absent vendor/type | Old-version project fixture pending |
| 26 | Unicode config roundtrip and unknown vendor fallback | Third-party project fixture pending |
| 27 | Compatibility filter, printer preflight | Nozzle GUI pending |
| 28 | Compatibility filter, printer preflight | Printer-switch GUI pending |
| 29 | 50/100/500 model+mapping benchmark | GUI P50/P95 pending |
| 30 | Ellipsis and hard-wrapped tooltip implemented | Blocked: CSV expects no truncation; baseline allows it |
| 31 | Full original name in controlled tooltip; work-area positioning | 512-character/DPI/dark/multi-monitor GUI pending |

## Specification conflicts

- IDs 3/4/5/20 describe flat user entries ordered only by name. The implementation
  baseline requires the shared vendor/type/display/stable-name key for menu and
  fallback; this implementation follows that baseline.
- ID 7's example `PLA < PETG` contradicts its alphabetical-order wording:
  alphabetical order is `PETG < PLA`. The implementation uses alphabetical order.
- ID 30 requires an untruncated 512-character row, whereas the implementation
  baseline explicitly adopts ellipsis plus the complete tooltip.
- Selected incompatible presets remain visible under baseline section 2.3;
  IDs 27/28 must account for this exception.

## Verification results

Build and automated-test results will be recorded after execution. No GUI,
platform, original-project-fixture or CSV end-to-end pass is claimed here.
