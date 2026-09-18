#ifndef slic3r_GUI_DropDownModel_hpp_
#define slic3r_GUI_DropDownModel_hpp_

#include <algorithm>
#include <cstddef>
#include <map>
#include <vector>

// Window-independent mapping shared by layout, painting and hit testing.
// A negative row encodes a group header as -first_real_index-2.
template<class Key> struct DropDownView
{
    std::vector<int>                rows;
    std::vector<int>                real_to_view;
    std::map<Key, std::vector<int>> members;
};

template<class Key> DropDownView<Key> build_dropdown_view(const std::vector<Key>& groups, const Key& expanded = Key())
{
    DropDownView<Key> view;
    view.real_to_view.assign(groups.size(), -1);
    for (size_t i = 0; i < groups.size(); ++i)
        if (!groups[i].empty())
            view.members[groups[i]].push_back(int(i));
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!expanded.empty() && groups[i] != expanded)
            continue;
        const bool header = expanded.empty() && !groups[i].empty();
        if (header && view.members.at(groups[i]).front() != int(i))
            continue;
        if (header) {
            view.rows.push_back(-int(i) - 2);
        } else {
            view.real_to_view[i] = int(view.rows.size());
            view.rows.push_back(int(i));
        }
    }
    return view;
}

inline int clamp_dropdown_offset(int offset, int viewport_height, int row_height, int rows)
{
    return std::clamp(offset, std::min(0, viewport_height - row_height * std::max(0, rows)), 0);
}

inline bool dropdown_item_selectable(int index, const std::vector<int>& styles)
{
    // SPLIT=1, DISABLED=2, DIMMED=4, NON_SELECTABLE=8.
    return index >= 0 && index < int(styles.size()) && (styles[index] & (1 | 2 | 8)) == 0;
}

#endif
