#include <catch_main.hpp>

#include "libslic3r/FilamentSort.hpp"
#include "libslic3r/FilamentMenuModel.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "slic3r/GUI/Widgets/DropDownModel.hpp"
#include <boost/filesystem.hpp>
#include <chrono>
#include <iostream>

using Slic3r::compare_filament_name;
using Slic3r::filament_name_less;

namespace {

Slic3r::FilamentMenuItem make_item(const std::string& name, const std::string& vendor, bool compatible, Slic3r::FilamentOrigin origin)
{
    Slic3r::FilamentMenuItem item;
    item.stable_preset_name = name;
    item.display_name       = name;
    item.origin             = origin;
    item.is_compatible      = compatible;
    item.vendor             = vendor;
    return item;
}

Slic3r::FilamentMenuModel make_model()
{
    Slic3r::FilamentMenuModel model;
    model.items = {
        make_item("Snapmaker PLA", "Snapmaker", true, Slic3r::FilamentOrigin::System),
        make_item("Generic PLA", "Generic", true, Slic3r::FilamentOrigin::System),
        make_item("My PETG", "", true, Slic3r::FilamentOrigin::User),
        make_item("Old PLA", "Generic", false, Slic3r::FilamentOrigin::System),
    };
    return model;
}

} // namespace

namespace {
struct MenuFixture
{
    Slic3r::PresetBundle bundle;
    MenuFixture() { bundle.filaments.set_default_suppressed(true); }
    void add(const std::string&     name,
             const std::string&     alias,
             const std::string&     vendor     = "Generic",
             const std::string&     type       = "PLA",
             Slic3r::FilamentOrigin origin     = Slic3r::FilamentOrigin::User,
             bool                   compatible = true)
    {
        auto config = bundle.filaments.default_preset().config;
        config.set_key_value("filament_vendor", new Slic3r::ConfigOptionStrings({vendor}));
        config.set_key_value("filament_type", new Slic3r::ConfigOptionStrings({type}));
        auto& preset               = bundle.filaments.load_preset("", name, config, false);
        preset.alias               = alias;
        preset.is_visible          = true;
        preset.is_compatible       = compatible;
        preset.is_system           = origin == Slic3r::FilamentOrigin::System;
        preset.is_project_embedded = origin == Slic3r::FilamentOrigin::Project;
    }
    Slic3r::FilamentMenuModel menu(const std::string& selected = "")
    {
        return Slic3r::build_filament_menu_model(bundle.filaments, {selected}, 0);
    }
};
} // namespace

TEST_CASE("filament model aggregates aliases per selected physical slot", "[filament][fast]")
{
    MenuFixture f;
    f.add("PLA @ A", "PLA");
    f.add("PLA @ B", "PLA");
    f.add("PLA @ C", "PLA", "Generic", "PLA", Slic3r::FilamentOrigin::System, false);
    REQUIRE(f.menu().items.size() == 1);
    REQUIRE(f.menu().items.front().stable_preset_name == "PLA @ A");
    const std::vector<std::string> slots = {"PLA @ B", "PLA @ C"};
    for (size_t slot = 0; slot < slots.size(); ++slot) {
        const auto menu = Slic3r::build_filament_menu_model(f.bundle.filaments, slots, slot);
        REQUIRE(menu.items.size() == 1);
        REQUIRE(menu.items.front().stable_preset_name == slots[slot]);
        REQUIRE(menu.items.front().is_selected);
    }
    f.bundle.filaments.find_preset("PLA @ B", false)->is_dirty = true;
    REQUIRE(f.menu("PLA @ B").items.size() == 1);
}

TEST_CASE("filament model threshold counts unique user aliases only", "[filament][fast]")
{
    MenuFixture f;
    for (int i = 0; i < 9; ++i)
        f.add("User " + std::to_string(i), "Alias " + std::to_string(i));
    f.add("User duplicate", "Alias 0");
    f.add("Project", "Project", "Generic", "PLA", Slic3r::FilamentOrigin::Project);
    f.add("System", "System", "Generic", "PLA", Slic3r::FilamentOrigin::System);
    auto menu = f.menu();
    REQUIRE(menu.sections.size() == 3);
    REQUIRE_FALSE(menu.sections[0].flat);
    REQUIRE(menu.sections[1].flat);
    REQUIRE(menu.sections[1].items.size() == 9);
    REQUIRE_FALSE(menu.sections[2].flat);
    REQUIRE(menu.group_key(menu.items.front()) != menu.group_key(menu.items.back()));
    f.add("User 9", "Alias 9");
    REQUIRE_FALSE(f.menu().sections[1].flat);
    f.bundle.filaments.find_preset("User 9", false)->is_visible = false;
    REQUIRE(f.menu().sections[1].flat);
}

TEST_CASE("filament model cleans metadata and uses menu order for fallback", "[filament][fast]")
{
    MenuFixture f;
    f.add("Z first", "PLA 2", "Snapmaker");
    f.add("A second", "PLA 10", "Snapmaker");
    f.add("Generic", "PLA", "Generic");
    f.add("Missing type", "AAA", "Generic", "(Undefined)");
    f.add("Missing vendor", "AAA", "(Undefined)");
    // Give missing-vendor entry a distinct alias so both survive aggregation.
    f.bundle.filaments.find_preset("Missing vendor", false)->alias = "BBB";
    auto menu                                                      = f.menu();
    REQUIRE(menu.items.size() == 5);
    REQUIRE(menu.items[0].stable_preset_name == "Z first");
    REQUIRE(menu.items[1].stable_preset_name == "A second");
    REQUIRE(menu.items[2].stable_preset_name == "Generic");
    REQUIRE(menu.items[3].type.empty());
    REQUIRE(menu.items[4].vendor.empty());
    REQUIRE(menu.fallback_index({}) == 0);
    REQUIRE(menu.items[menu.fallback_index({"Gone", "Generic"})].stable_preset_name == "Generic");
    const auto plan = Slic3r::plan_filament_selections(f.bundle.filaments, {"Gone", "Generic"}, {});
    REQUIRE(plan.valid);
    REQUIRE(plan.names == std::vector<std::string>{"Z first", "Generic"});
    REQUIRE(plan.changed_slots == std::vector<size_t>{0});
    REQUIRE(Slic3r::plan_filament_selections(f.bundle.filaments, plan.names, {}).changed_slots.empty());
}

TEST_CASE("filament model rejects removal without changing any slot", "[filament][fast]")
{
    MenuFixture f;
    f.add("Only", "Only");
    const std::vector<std::string> before = {"Only", "Gone"};
    const auto                     plan   = Slic3r::plan_filament_selections(f.bundle.filaments, before, {}, {"Only"});
    REQUIRE_FALSE(plan.valid);
    REQUIRE(plan.names == before);
    REQUIRE(plan.changed_slots.empty());
    REQUIRE(f.bundle.filaments.find_preset("Only", false) != nullptr);
}

TEST_CASE("filament normalized display ties resolve by stable preset name", "[filament][fast]")
{
    MenuFixture f;
    f.add("Z", "ABS");
    f.add("A", "abs");
    REQUIRE(f.menu().items.front().stable_preset_name == "A");
    REQUIRE(compare_filament_name("PLA", "!PLA") < 0);
    REQUIRE(compare_filament_name("\xe8\x81\x9a", "!PLA") < 0);
}

TEST_CASE("dropdown mapping preserves full groups after fifty real items", "[filament][fast]")
{
    std::vector<std::string> groups(55, "system/vendor/First");
    groups.insert(groups.end(), {"", "user/vendor/Target", "user/vendor/Target", "project/vendor/Target", ""});
    const auto view = build_dropdown_view(groups);
    REQUIRE(view.rows == std::vector<int>{-2, 55, -58, -60, 59});
    REQUIRE(view.members.at("system/vendor/First").size() == 55);
    REQUIRE(view.members.at("user/vendor/Target") == std::vector<int>{56, 57});
    REQUIRE(view.real_to_view[54] == -1);
    REQUIRE(view.real_to_view[57] == -1);
    REQUIRE(view.real_to_view[0] == -1);
    REQUIRE(view.real_to_view[55] == 1);
    REQUIRE(view.real_to_view[59] == 4);
    const auto sub = build_dropdown_view(groups, std::string("user/vendor/Target"));
    REQUIRE(sub.rows == std::vector<int>{56, 57});
    REQUIRE(sub.real_to_view[56] == 0);
    REQUIRE(sub.real_to_view[57] == 1);
    REQUIRE(sub.real_to_view[58] == -1);
    REQUIRE(build_dropdown_view(groups, std::string("project/vendor/Target")).rows == std::vector<int>{58});
    groups.clear();
    REQUIRE(build_dropdown_view(groups).rows.empty());
}

TEST_CASE("dropdown selection gate and offset stay valid", "[filament][fast]")
{
    const std::vector<int> styles = {0, 1, 2, 4, 8, 2 | 4};
    REQUIRE(dropdown_item_selectable(0, styles));
    REQUIRE(dropdown_item_selectable(3, styles));
    for (int i : {-1, 1, 2, 4, 5, 6})
        REQUIRE_FALSE(dropdown_item_selectable(i, styles));
    REQUIRE(clamp_dropdown_offset(-900, 100, 30, 2) == 0);
    REQUIRE(clamp_dropdown_offset(-900, 100, 30, 10) == -200);
    REQUIRE(clamp_dropdown_offset(20, 100, 30, 10) == 0);
    REQUIRE(clamp_dropdown_offset(-10, 100, 30, 0) == 0);
}

TEST_CASE("filament menu rebuild does not persist group identities", "[filament][fast]")
{
    MenuFixture f;
    f.add("PLA @ A", "PLA");
    f.add("PLA @ B", "PLA");
    f.bundle.filament_presets = {"PLA @ B", "PLA @ A"};
    const auto before         = f.bundle.filament_presets;
    for (size_t i = 0; i < before.size(); ++i)
        REQUIRE(Slic3r::build_filament_menu_model(f.bundle.filaments, before, i).items.front().stable_preset_name == before[i]);
    Slic3r::AppConfig config;
    f.bundle.export_selections(config);
    const auto printer = f.bundle.printers.get_selected_preset_name();
    REQUIRE(config.get_printer_setting(printer, "filament") == before[0]);
    REQUIRE(config.get_printer_setting(printer, "filament_01") == before[1]);
    REQUIRE(f.bundle.filament_presets == before);
}

TEST_CASE("filament menu model retains legacy project metadata without vendor/type", "[filament][fast]")
{
    // Legacy/third-party 3MF project presets may lack vendor/type entirely; the
    // menu model must retain the entry (Uncategorized / missing type) rather
    // than dropping it or guessing metadata from the display name.
    MenuFixture f;
    f.add("PLA @ Printer A", "PLA", "", "", Slic3r::FilamentOrigin::Project);
    auto* preset = f.bundle.filaments.find_preset("PLA @ Printer A", false);
    preset->config.erase("filament_vendor");
    preset->config.erase("filament_type");
    const auto menu = f.menu("PLA @ Printer A");
    REQUIRE(menu.items.size() == 1);
    REQUIRE(menu.items.front().stable_preset_name == "PLA @ Printer A");
    REQUIRE(menu.items.front().vendor.empty());
    REQUIRE(menu.items.front().type.empty());
    REQUIRE_FALSE(menu.sections.front().flat);
}

TEST_CASE("filament single-slot recovery updates the slicing preset", "[filament][fast]")
{
    MenuFixture f;
    f.add("Replacement", "PLA");
    auto* preset = f.bundle.filaments.find_preset("Replacement", false);
    preset->config.set_key_value("filament_diameter", new Slic3r::ConfigOptionFloats({2.85}));
    f.bundle.filament_presets = {"Missing"};
    REQUIRE(f.bundle.resolve_filament_selections({"Missing"}));
    REQUIRE(f.bundle.filament_presets.front() == "Replacement");
    REQUIRE(f.bundle.filaments.get_edited_preset().name == "Replacement");
    REQUIRE(f.bundle.full_config().option<Slic3r::ConfigOptionFloats>("filament_diameter")->values.front() == 2.85);
}

TEST_CASE("filament menu model performance datasets", "[filament][!benchmark]")
{
    for (int count : {50, 100, 500}) {
        MenuFixture f;
        for (int i = 0; i < count; ++i)
            f.add("Preset " + std::to_string(i), "PLA " + std::to_string(i), "Vendor " + std::to_string(i % 10));
        std::vector<double> timings;
        for (int run = 0; run < 30; ++run) {
            const auto               start = std::chrono::steady_clock::now();
            const auto               model = f.menu();
            std::vector<std::string> groups;
            for (const auto& item : model.items)
                groups.push_back(model.group_key(item));
            const auto view = build_dropdown_view(groups);
            timings.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            REQUIRE(model.items.size() == size_t(count));
            REQUIRE(view.rows.size() == 10);
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "model+mapping aliases=" << count << " P50_ms=" << timings[15] << " P95_ms=" << timings[28] << '\n';
        // This measures data preparation only, not GUI popup latency.
    }
}

TEST_CASE("filament natural order: digit-led names", "[filament][fast]")
{
    SECTION("Natural number order inside digit runs")
    {
        REQUIRE(compare_filament_name("PLA 2", "PLA 10") < 0);
        REQUIRE(compare_filament_name("PLA 10", "PLA 11") < 0);
        REQUIRE(compare_filament_name("PLA 9", "PLA 10") < 0);
        REQUIRE(compare_filament_name("PLA 10", "PLA 9") > 0);
    }
    SECTION("Fewer leading zeros first on equal value")
    {
        REQUIRE(compare_filament_name("PLA 75", "PLA 075") < 0);
        REQUIRE(compare_filament_name("PLA 007", "PLA 07") > 0);
    }
    SECTION("Digit-led names sort before letter-led names") { REQUIRE(compare_filament_name("2 PLA", "ABS") < 0); }
}

TEST_CASE("filament natural order: ASCII letters", "[filament][fast]")
{
    SECTION("Case-insensitive primary, caller breaks ties by stable name")
    {
        REQUIRE(compare_filament_name("abs", "ABS") == 0);
        REQUIRE(compare_filament_name("PLA Matte", "pla silk") < 0);
    }
    SECTION("ASCII before CJK before other-led names")
    {
        REQUIRE(compare_filament_name("PETG", "\xe8\x81\x9a\xe4\xb9\xb3\xe9\x85\xb8") < 0);
        REQUIRE(compare_filament_name("\xe8\x81\x9a\xe4\xb9\xb3\xe9\x85\xb8", "\xc3\xa9PETG") < 0);
    }
}

TEST_CASE("filament natural order: CJK pinyin first batch", "[filament][fast]")
{
    REQUIRE(compare_filament_name("\xe8\x81\x9a\xe4\xb9\xb3\xe9\x85\xb8", "\xe6\xa0\x91\xe8\x84\x82") < 0);
    REQUIRE(compare_filament_name("\xe4\xbd\x8e\xe6\xb8\xa9", "\xe9\xab\x98\xe6\xb8\xa9") < 0);
    REQUIRE(compare_filament_name("\xe8\x81\x9a\xe4\xb9\xb3\xe9\x85\xb8", "\xe9\xbe\x99") < 0);
}

TEST_CASE("filament natural order: empty and stability", "[filament][fast]")
{
    SECTION("Empty name sorts after non-empty") { REQUIRE(compare_filament_name("PLA", "") < 0); }
    SECTION("NFC-equivalent strings tie")
    {
        const char* nfc    = "\xc3\xa9PLA";
        const char* decomp = "e\xcc\x81PLA";
        REQUIRE(compare_filament_name(nfc, decomp) == 0);
        REQUIRE(compare_filament_name(nfc, nfc) == 0);
    }
    SECTION("less is a strict weak ordering")
    {
        REQUIRE(filament_name_less("PLA", "PLA Matte"));
        REQUIRE(!filament_name_less("PLA", "PLA"));
    }
}

TEST_CASE("filament menu model: group identity", "[filament][fast]")
{
    const Slic3r::FilamentMenuModel model = make_model();
    SECTION("Origin-isolated vendor keys")
    {
        REQUIRE(model.group_key(model.items[0]) == "system/vendor/Snapmaker");
        REQUIRE(model.group_key(model.items[2]) == "user/uncategorized");
    }
    SECTION("Strip prefix only for named system vendors")
    {
        REQUIRE(model.group_strip_prefix(model.items[0]));
        REQUIRE(!model.group_strip_prefix(model.items[2]));
    }
}

TEST_CASE("filament menu model: lookup and fallback", "[filament][fast]")
{
    const Slic3r::FilamentMenuModel model = make_model();
    SECTION("Stable name lookup")
    {
        REQUIRE(model.index_of_stable_name("Generic PLA") == 1);
        REQUIRE(model.index_of_stable_name("Missing") == -1);
    }
    SECTION("First compatible skips incompatible entries") { REQUIRE(model.first_compatible_index() == 0); }
    SECTION("Preferred printer default wins when compatible")
    {
        std::vector<std::string> preferred = {"Old PLA", "Generic PLA"};
        REQUIRE(model.fallback_index(preferred) == 1);
    }
    SECTION("Incompatible preferred names are skipped")
    {
        std::vector<std::string> preferred = {"Old PLA"};
        REQUIRE(model.fallback_index(preferred) == 0);
    }
    SECTION("No preferred match falls to first compatible") { REQUIRE(model.fallback_index({}) == 0); }
    SECTION("No compatible candidate reports no fallback")
    {
        Slic3r::FilamentMenuModel empty;
        empty.items = {make_item("Only", "Snapmaker", false, Slic3r::FilamentOrigin::System)};
        REQUIRE(empty.fallback_index({"Only"}) == -1);
        REQUIRE(empty.first_compatible_index() == -1);
    }
}
