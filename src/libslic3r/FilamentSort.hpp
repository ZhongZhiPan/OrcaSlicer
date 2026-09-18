#ifndef slic3r_FilamentSort_hpp_
#define slic3r_FilamentSort_hpp_

#include <string>

namespace Slic3r {

// Shared display-name ordering for filament menus: digit-led names first with
// natural number order, then ASCII letters (case-insensitive), then CJK by a
// versioned pinyin key table with Unicode code point fallback, then anything
// else. Input is NFC normalized; callers break equivalent-name ties by stable identity.
int compare_filament_name(const std::string& a, const std::string& b);

bool filament_name_less(const std::string& a, const std::string& b);

} // namespace Slic3r

#endif // slic3r_FilamentSort_hpp_
