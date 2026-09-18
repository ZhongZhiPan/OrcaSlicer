#ifndef slic3r_FilamentMenuModel_hpp_
#define slic3r_FilamentMenuModel_hpp_

#include <string>
#include <vector>
#include "Preset.hpp"

namespace Slic3r {

enum class FilamentOrigin { System, User, Project };

struct FilamentMenuItem
{
    std::string    stable_preset_name;
    std::string    display_name;
    const Preset*  preset        = nullptr;
    FilamentOrigin origin        = FilamentOrigin::User;
    bool           is_selected   = false;
    bool           is_compatible = false;
    std::string    vendor; // cleaned; empty = uncategorized
    std::string    type;   // cleaned; empty = missing, sorts last
};

struct FilamentMenuSection
{
    FilamentOrigin   origin;
    bool             flat = false;
    std::vector<int> items; // indices into FilamentMenuModel::items, ordered
};

struct FilamentMenuModel
{
    std::vector<FilamentMenuItem>    items;
    std::vector<FilamentMenuSection> sections;

    std::string group_key(const FilamentMenuItem& item) const;
    bool        group_strip_prefix(const FilamentMenuItem& item) const;
    int         index_of_stable_name(const std::string& name) const;
    int         first_compatible_index() const;
    // Preferred printer defaults first (compatibly present), then the first
    // compatible item in menu order; -1 when nothing is selectable.
    int fallback_index(const std::vector<std::string>& preferred) const;
};

// Aggregates "@printer" alias variants (selected > compatible > stable name),
// cleans vendor/type, orders by the shared key, decides the User flat/two-level
// threshold and produces origin-isolated group keys. Only visible presets and
// selected-but-incompatible presets enter the model.
FilamentMenuModel build_filament_menu_model(const PresetCollection&         filaments,
                                            const std::vector<std::string>& filament_selection_names,
                                            size_t                          slot,
                                            const std::vector<std::string>& excluded = {});

// Resolve all slots without modifying the collection or the input. On failure
// names is the original selection, so callers can reject a mutation atomically.
struct FilamentSelectionPlan
{
    bool                     valid = true;
    std::vector<std::string> names;
    std::vector<size_t>      changed_slots;
};
FilamentSelectionPlan plan_filament_selections(const PresetCollection&         filaments,
                                               const std::vector<std::string>& selections,
                                               const std::vector<std::string>& preferred,
                                               const std::vector<std::string>& excluded = {});

} // namespace Slic3r

#endif // slic3r_FilamentMenuModel_hpp_
