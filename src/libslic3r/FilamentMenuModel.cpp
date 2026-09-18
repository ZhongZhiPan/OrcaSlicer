#include "FilamentMenuModel.hpp"

#include "FilamentSort.hpp"

#include <algorithm>
#include <deque>
#include <map>

namespace Slic3r {

namespace {

bool clean_vendor_type(const ConfigOptionStrings* opt, std::string& out)
{
    if (opt == nullptr || opt->values.empty())
        return false;
    std::string v = opt->values.front();
    if (v.empty() || v == "(Undefined)")
        return false;
    out = std::move(v);
    return true;
}

int vendor_rank(const std::string& v)
{
    static const std::vector<std::string> first_vendors = {"Snapmaker", "Generic"};
    if (v.empty())
        return (int) first_vendors.size() + 1;
    auto it = std::find(first_vendors.begin(), first_vendors.end(), v);
    return it == first_vendors.end() ? (int) first_vendors.size() : (int) (it - first_vendors.begin());
}

bool item_less(const FilamentMenuItem& l, const FilamentMenuItem& r)
{
    int lr = vendor_rank(l.vendor);
    int rr = vendor_rank(r.vendor);
    if (lr != rr)
        return lr < rr;
    int c = compare_filament_name(l.vendor, r.vendor);
    if (c != 0)
        return c < 0;
    bool lm = l.type.empty();
    bool rm = r.type.empty();
    if (lm != rm)
        return !lm;
    if (!lm) {
        c = compare_filament_name(l.type, r.type);
        if (c != 0)
            return c < 0;
    }
    c = compare_filament_name(l.display_name, r.display_name);
    if (c != 0)
        return c < 0;
    return l.stable_preset_name < r.stable_preset_name;
}

const char* origin_key(FilamentOrigin origin)
{
    switch (origin) {
    case FilamentOrigin::System: return "system";
    case FilamentOrigin::User: return "user";
    case FilamentOrigin::Project: return "project";
    }
    return "user";
}

} // namespace

FilamentMenuModel build_filament_menu_model(const PresetCollection&         filaments,
                                            const std::vector<std::string>& selections,
                                            size_t                          slot,
                                            const std::vector<std::string>& excluded)
{
    FilamentMenuModel         model;
    const std::deque<Preset>& presets = filaments.get_presets();
    if (presets.empty())
        return model;
    const std::string selected_name = slot < selections.size() ? selections[slot] : std::string();

    struct Candidate
    {
        const Preset* preset;
        int           prio;
    };
    std::map<std::string, Candidate> winners;
    for (size_t i = 0; i < presets.size(); ++i) {
        const Preset& preset = presets[i];
        // Base "Default Filament" must never become a menu entry or a fallback
        // candidate (implementation.md Step 3.6).
        if (preset.is_default)
            continue;
        if (std::find(excluded.begin(), excluded.end(), preset.name) != excluded.end())
            continue;
        const bool is_selected = preset.name == selected_name;
        if (!preset.is_visible || (!preset.is_compatible && !is_selected))
            continue;
        const int         prio  = is_selected ? 2 : 1;
        const std::string alias = preset.alias.empty() ? preset.name : preset.alias;
        auto              it    = winners.find(alias);
        if (it == winners.end())
            winners.emplace(alias, Candidate{&preset, prio});
        else if (prio > it->second.prio || (prio == it->second.prio && preset.name < it->second.preset->name))
            it->second = Candidate{&preset, prio};
    }

    std::vector<FilamentMenuItem> by_origin[3];
    for (const auto& winner : winners) {
        const Preset&    preset = *winner.second.preset;
        FilamentMenuItem item;
        item.stable_preset_name = preset.name;
        item.display_name       = preset.label(false);
        item.preset             = &preset;
        item.is_selected        = preset.name == selected_name;
        item.is_compatible      = preset.is_compatible;
        item.origin             = (preset.is_default || preset.is_system) ? FilamentOrigin::System :
                                  preset.is_project_embedded              ? FilamentOrigin::Project :
                                                                            FilamentOrigin::User;
        clean_vendor_type(preset.config.option<ConfigOptionStrings>("filament_vendor"), item.vendor);
        clean_vendor_type(preset.config.option<ConfigOptionStrings>("filament_type"), item.type);
        by_origin[int(item.origin)].push_back(std::move(item));
    }

    const bool user_flat   = by_origin[int(FilamentOrigin::User)].size() < 10;
    auto       add_section = [&](FilamentOrigin origin, bool flat) {
        std::vector<FilamentMenuItem>& vec = by_origin[int(origin)];
        if (vec.empty())
            return;
        std::sort(vec.begin(), vec.end(), item_less);
        FilamentMenuSection section;
        section.origin = origin;
        section.flat   = flat;
        for (const FilamentMenuItem& item : vec) {
            section.items.push_back((int) model.items.size());
            model.items.push_back(item);
        }
        model.sections.push_back(std::move(section));
    };
    add_section(FilamentOrigin::Project, false);
    add_section(FilamentOrigin::User, user_flat);
    add_section(FilamentOrigin::System, false);
    return model;
}

std::string FilamentMenuModel::group_key(const FilamentMenuItem& item) const
{
    std::string key = origin_key(item.origin);
    key += item.vendor.empty() ? "/uncategorized" : "/vendor/" + item.vendor;
    return key;
}

bool FilamentMenuModel::group_strip_prefix(const FilamentMenuItem& item) const
{
    return !item.vendor.empty();
}

int FilamentMenuModel::index_of_stable_name(const std::string& name) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].stable_preset_name == name)
            return (int) i;
    return -1;
}

int FilamentMenuModel::first_compatible_index() const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].is_compatible)
            return (int) i;
    return -1;
}

int FilamentMenuModel::fallback_index(const std::vector<std::string>& preferred) const
{
    for (const std::string& name : preferred) {
        if (name.empty())
            continue;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].is_compatible && items[i].stable_preset_name == name)
                return (int) i;
    }
    return first_compatible_index();
}

FilamentSelectionPlan plan_filament_selections(const PresetCollection&         filaments,
                                               const std::vector<std::string>& selections,
                                               const std::vector<std::string>& preferred,
                                               const std::vector<std::string>& excluded)
{
    FilamentSelectionPlan plan;
    plan.names = selections;
    for (size_t slot = 0; slot < selections.size(); ++slot) {
        const Preset* selected = filaments.find_preset(selections[slot], false);
        if (selected && selected->is_visible && std::find(excluded.begin(), excluded.end(), selections[slot]) == excluded.end())
            continue;
        const auto model = build_filament_menu_model(filaments, selections, slot, excluded);
        if (model.index_of_stable_name(selections[slot]) >= 0)
            continue;
        const int fallback = model.fallback_index(preferred);
        if (fallback < 0) {
            plan.valid = false;
            plan.names = selections;
            plan.changed_slots.clear();
            return plan;
        }
        plan.names[slot] = model.items[fallback].stable_preset_name;
        plan.changed_slots.push_back(slot);
    }
    return plan;
}

} // namespace Slic3r
