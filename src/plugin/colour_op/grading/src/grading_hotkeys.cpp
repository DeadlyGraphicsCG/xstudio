// SPDX-License-Identifier: Apache-2.0

// Keyboard shortcuts for the grading tool.
//
// All grading shortcuts are listed under the 'Grading' category of the
// hotkeys dialog (Help > Hotkeys), where they can be remapped. User changes
// are stored by the keypress monitor in the '/ui/keyboard/hotkey_overrides'
// preference, so they are kept across restarts.
//
// Channel checks (show red / green / blue only, back to RGB) are provided by
// the viewer's colour pipeline ('Viewer' category: R, G, B, A, Ctrl+L and C by
// default). They are not duplicated here so there is only one channel mode.

#include <caf/actor_registry.hpp>

#include "xstudio/atoms.hpp"
#include "xstudio/ui/keyboard.hpp"
#include "xstudio/utility/logging.hpp"

#include "grading.h"
#include "grading_hotkeys_logic.h"

using namespace xstudio;
using namespace xstudio::colour_pipeline;

namespace gh = xstudio::colour_pipeline::grading_hotkeys;

utility::Uuid GradingTool::register_grading_hotkey(
    const std::string &sequence,
    const std::string &name,
    const std::string &description,
    const utility::Uuid &uuid) {

    // Same as Module::register_hotkey, but lets us pin the hotkey uuid. The
    // keypress monitor stores user overrides by hotkey uuid, and the uuid is
    // normally made from the hotkey name plus its category. The older grading
    // shortcuts used the category 'GradingTool'; pinning their original uuid
    // keeps any key a user already assigned to them after the move to the
    // 'Grading' category.
    try {

        int key      = 0;
        int modifier = 0;
        ui::Hotkey::sequence_to_key_and_modifier(sequence, key, modifier);

        auto keypress_monitor = system().registry().template get<caf::actor>(keyboard_events);
        if (!keypress_monitor || !self()) {
            spdlog::warn("{} cannot register hotkey '{}'", __PRETTY_FUNCTION__, name);
            return {};
        }

        ui::Hotkey hk(
            key,
            modifier,
            name,
            gh::hotkey_category(),
            description,
            "any", // window
            false, // auto repeat
            caf::actor_cast<caf::actor_addr>(self()),
            uuid);

        anon_mail(ui::keypress_monitor::register_hotkey_atom_v, hk).send(keypress_monitor);
        return hk.uuid();

    } catch (std::exception &e) {
        spdlog::warn("{} {} : {}", __PRETTY_FUNCTION__, name, e.what());
    }
    return {};
}

void GradingTool::register_grading_hotkeys() {

    // uuid that a hotkey had when it was registered under the old
    // 'GradingTool' category (see register_grading_hotkey)
    auto legacy_uuid = [](const std::string &hotkey_name) {
        return utility::Uuid::generate_from_name((hotkey_name + "GradingTool").c_str());
    };

    bypass_hotkey_ = register_grading_hotkey(
        "Ctrl+B",
        "Bypass all grades",
        "Bypass all grades applied on all media",
        legacy_uuid("Bypass all grades"));

    undo_hotkey_ = register_grading_hotkey(
        "Ctrl+Z",
        "Undo (Annotation edit)",
        "Undoes your last edits to an annotation",
        legacy_uuid("Undo (Annotation edit)"));

    redo_hotkey_ = register_grading_hotkey(
        "Ctrl+Shift+Z",
        "Redo (Annotation edit)",
        "Redoes your last undone edit on an annotation",
        legacy_uuid("Redo (Annotation edit)"));

    for (const auto &hk : gh::default_hotkeys()) {
        auto uuid = register_grading_hotkey(hk.sequence, hk.name, hk.description);
        if (!uuid.is_null()) {
            grading_hotkey_actions_[uuid] = static_cast<int>(hk.action);
        }
    }
}

bool GradingTool::grading_hotkey_pressed(const utility::Uuid &hotkey_uuid) {

    auto p = grading_hotkey_actions_.find(hotkey_uuid);
    if (p == grading_hotkey_actions_.end()) {
        return false;
    }

    // These shortcuts work on the grade being edited, so they only act while
    // a grading panel is open.
    if (!grading_tools_active()) {
        return true;
    }

    switch (static_cast<gh::Action>(p->second)) {
    case gh::Action::ToggleLayer:
        toggle_current_grade_layer();
        break;
    case gh::Action::ResetControl:
        reset_active_grade_control();
        break;
    case gh::Action::NextLayer:
        step_grade_layer(1);
        break;
    case gh::Action::PreviousLayer:
        step_grade_layer(-1);
        break;
    }

    redraw_viewport();
    return true;
}

void GradingTool::remember_edited_grade_control(const utility::Uuid &attribute_uuid) {

    auto attr = get_attribute(attribute_uuid);
    if (attr && attr->belongs_to_groups(gh::grade_control_groups()) &&
        attr->has_role_data(module::Attribute::DefaultValue)) {
        last_edited_grade_control_ = attribute_uuid;
    }
}

void GradingTool::toggle_current_grade_layer() {

    const auto bookmark_uuid = current_bookmark();
    if (bookmark_uuid.is_null()) {
        return;
    }

    auto bmd = get_bookmark_detail(bookmark_uuid);
    if (!bmd.user_data_) {
        bmd.user_data_ = utility::JsonStore();
    }

    auto &user_data           = *bmd.user_data_;
    const bool active         = user_data.get_or("grade_active", true);
    user_data["grade_active"] = !active;
    update_bookmark_detail(bookmark_uuid, bmd);
}

void GradingTool::reset_active_grade_control() {

    if (current_bookmark().is_null() || last_edited_grade_control_.is_null()) {
        return;
    }

    auto attr = get_attribute(last_edited_grade_control_);
    if (!attr || !attr->has_role_data(module::Attribute::DefaultValue)) {
        return;
    }

    // Setting the value with notification on runs attribute_changed, which
    // refreshes and saves the grade exactly as if the user had moved the
    // control back to its default.
    attr->update_role_data_from_json(
        module::Attribute::Value,
        attr->role_data_as_json(module::Attribute::DefaultValue),
        true);
}

void GradingTool::step_grade_layer(const int step) {

    if (!current_media_actor_) {
        return;
    }

    auto layers = media_actor_grading_bookmarks(current_media_actor_);
    std::vector<utility::Uuid> ordered(layers.begin(), layers.end());

    // same order as the layer list in the grading panel (oldest first)
    std::map<utility::Uuid, std::optional<utility::sys_time_point>> created;
    for (const auto &uuid : ordered) {
        created[uuid] = get_bookmark_detail(uuid).created_;
    }
    gh::sort_layers_by_created(
        ordered, [&created](const utility::Uuid &uuid) { return created[uuid]; });

    const int current = gh::index_of(ordered, current_bookmark());
    const int next    = gh::step_layer_index(current, static_cast<int>(ordered.size()), step);
    if (next >= 0) {
        select_bookmark(ordered[next]);
    }
}
