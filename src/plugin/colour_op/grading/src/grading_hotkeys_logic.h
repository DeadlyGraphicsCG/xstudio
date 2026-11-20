// SPDX-License-Identifier: Apache-2.0

#pragma once

// Small, self-contained helpers used by the grading tool keyboard shortcuts.
// They have no dependency on the actor system so they can be unit tested on
// their own (see ../test/grading_hotkeys_test.cpp).

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::colour_pipeline::grading_hotkeys {

// Attribute UI groups that hold the grade controls the user edits (sliders
// and colour wheels). The 'Reset Active Grade Control' shortcut only tracks
// attributes exposed in one of these groups.
inline const std::vector<std::string> &grade_control_groups() {
    static const std::vector<std::string> groups = {"grading_sliders", "grading_wheels"};
    return groups;
}

// Work out which grade layer to select when the user steps to the next
// (step > 0) or previous (step < 0) layer.
//   current : index of the selected layer, or -1 when no layer is selected
//   count   : number of layers on the media
// Returns the index of the layer to select, or -1 when there are no layers.
// Stepping past either end wraps around. With nothing selected, 'next' picks
// the first layer and 'previous' picks the last one.
inline int step_layer_index(const int current, const int count, const int step) {

    if (count <= 0)
        return -1;

    if (current < 0 || current >= count) {
        return step >= 0 ? 0 : count - 1;
    }

    if (step == 0)
        return current;

    const int wrapped = (current + step) % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

// Sort grade layers into the order shown in the grading panel (oldest first).
// 'created' returns an optional time stamp for a layer; layers without a time
// stamp keep their relative order and go after the ones that have one.
template <typename Item, typename CreatedFn>
void sort_layers_by_created(std::vector<Item> &layers, CreatedFn created) {
    std::stable_sort(layers.begin(), layers.end(), [&created](const Item &a, const Item &b) {
        const auto ca = created(a);
        const auto cb = created(b);
        if (ca && cb)
            return *ca < *cb;
        // items with a time stamp come before items without one
        return static_cast<bool>(ca) && !static_cast<bool>(cb);
    });
}

// Index of 'item' in 'items', or -1 if not present.
template <typename Item> int index_of(const std::vector<Item> &items, const Item &item) {
    const auto p = std::find(items.begin(), items.end(), item);
    return p == items.end() ? -1 : static_cast<int>(std::distance(items.begin(), p));
}

// The keyboard shortcuts added by the grading tool, with their default key
// sequences. Listed here so the defaults are in one place (and documented in
// the hotkeys dialog via their descriptions).
enum class Action { ToggleLayer, ResetControl, NextLayer, PreviousLayer };

struct DefaultHotkey {
    Action action;
    const char *name;
    const char *sequence;
    const char *description;
};

// Category (hotkey 'component') that groups all grading shortcuts in the
// hotkeys dialog.
inline const char *hotkey_category() { return "Grading"; }

inline const std::vector<DefaultHotkey> &default_hotkeys() {
    static const std::vector<DefaultHotkey> hotkeys = {
        {Action::ToggleLayer,
         "Toggle Current Grade Layer",
         "Alt+D",
         "Turns the selected grade layer on or off (Grading panel must be open)."},
        {Action::ResetControl,
         "Reset Active Grade Control",
         "Shift+Home",
         "Puts the grade control you changed last (for example Exposure or Slope) back to "
         "its default value (Grading panel must be open)."},
        {Action::NextLayer,
         "Next Grade Layer",
         "Alt+]",
         "Selects the next grade layer on the current media (Grading panel must be open)."},
        {Action::PreviousLayer,
         "Previous Grade Layer",
         "Alt+[",
         "Selects the previous grade layer on the current media (Grading panel must be "
         "open)."}};
    return hotkeys;
}

} // namespace xstudio::colour_pipeline::grading_hotkeys
