// SPDX-License-Identifier: Apache-2.0

// GradingTool side of colour profiles (saved grade stacks): the UI
// attributes, and saving / applying / removing profiles on media. The file
// format and the apply / sync / ordering rules live in grade_stack.cpp.

#include <algorithm>
#include <filesystem>
#include <set>

#include <caf/actor_registry.hpp>

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/logging.hpp"
#include "xstudio/utility/string_helpers.hpp"

#include "grading.h"
#include "grading_data_serialiser.hpp"

using namespace xstudio;
using namespace xstudio::colour_pipeline;
using namespace xstudio::ui::viewport;

namespace fs = std::filesystem;
namespace gs = xstudio::ui::viewport::grade_stack;

namespace {

// The shader applies at most this many grade layers per image.
constexpr size_t MAX_LAYERS_PER_SHOT = 8;

std::string local_path(std::string path) {
    path = utility::replace_once(path, "file://", "");
    return path;
}

std::string arg_string(const utility::JsonStore &args, const char *key) {
    if (args.is_object() && args.contains(key) && args.at(key).is_string())
        return args.at(key).get<std::string>();
    return "";
}

bookmark::BookmarkDetail new_layer_detail() {
    bookmark::BookmarkDetail bmd;
    // Hides bookmark from timeline, same as layers made in the grading panel
    bmd.colour_    = "transparent";
    bmd.visible_   = false;
    bmd.user_type_ = "Grading";
    return bmd;
}

utility::JsonStore grading_data_json(const bookmark::AnnotationBasePtr &annotation) {
    auto gd = dynamic_cast<const GradingData *>(annotation.get());
    if (!gd)
        return utility::JsonStore();
    return GradingDataSerialiser::serialise(gd);
}

} // namespace

void GradingTool::grade_stack_init() {

    // QML writes a small JSON command here, e.g.
    // {"action": "apply", "path": "...", "scope": "selected", "position": "below"}
    grade_stack_action_ = add_string_attribute("grade_stack_action", "grade_stack_action", "");
    grade_stack_action_->expose_in_ui_attrs_group("grade_stack_settings");

    // JSON list of the profiles found in the profile folder
    grade_stack_profiles_ =
        add_string_attribute("grade_stack_profiles", "grade_stack_profiles", "[]");
    grade_stack_profiles_->expose_in_ui_attrs_group("grade_stack_settings");

    // JSON {"ok": bool, "message": "...", "count": n} after each command
    grade_stack_status_ = add_string_attribute("grade_stack_status", "grade_stack_status", "");
    grade_stack_status_->expose_in_ui_attrs_group("grade_stack_settings");

    // Optional folder override (e.g. a shared show folder). Empty = default.
    grade_stack_folder_ = add_string_attribute("grade_stack_folder", "grade_stack_folder", "");
    grade_stack_folder_->set_preference_path("/plugin/grading/grade_stack_folder");
    grade_stack_folder_->expose_in_ui_attrs_group("grade_stack_settings");

    grade_stack_default_folder_ =
        add_string_attribute("grade_stack_default_folder", "grade_stack_default_folder", "");
    grade_stack_default_folder_->expose_in_ui_attrs_group("grade_stack_settings");

    grade_stack_position_ = add_string_choice_attribute(
        "grade_stack_position", "grade_stack_position", "Below", {"Below", "Above"});
    grade_stack_position_->set_preference_path("/plugin/grading/grade_stack_position");
    grade_stack_position_->expose_in_ui_attrs_group("grade_stack_settings");

    grade_stack_can_undo_ =
        add_boolean_attribute("grade_stack_can_undo", "grade_stack_can_undo", false);
    grade_stack_can_undo_->expose_in_ui_attrs_group("grade_stack_settings");

    grade_stack_refresh_profiles();
}

bool GradingTool::grade_stack_attribute_changed(const utility::Uuid &attribute_uuid) {

    if (!grade_stack_action_)
        return false;

    if (attribute_uuid == grade_stack_action_->uuid()) {
        const std::string action = grade_stack_action_->value();
        if (!action.empty()) {
            grade_stack_handle_action(action);
            grade_stack_action_->set_value("");
        }
        return true;
    }

    if (attribute_uuid == grade_stack_folder_->uuid()) {
        grade_stack_refresh_profiles();
        return true;
    }

    return false;
}

std::string GradingTool::grade_stack_folder() const {

    std::string folder = grade_stack_folder_ ? local_path(grade_stack_folder_->value()) : "";
    if (folder.empty()) {
        // next to xSTUDIO's preference folder, e.g.
        // ~/.config/DNEG/xstudio/grade_profiles
        folder =
            (fs::path(utility::preference_path()).parent_path() / "grade_profiles").string();
    }
    return folder;
}

void GradingTool::grade_stack_set_status(bool ok, const std::string &message) {

    if (ok)
        spdlog::info("Colour profiles: {}", message);
    else
        spdlog::warn("Colour profiles: {}", message);

    nlohmann::json status;
    status["ok"]      = ok;
    status["message"] = message;
    status["count"]   = ++grade_stack_status_count_;
    grade_stack_status_->set_value(status.dump());
}

void GradingTool::grade_stack_handle_action(const std::string &action_json) {

    try {
        const utility::JsonStore args(nlohmann::json::parse(action_json));
        const std::string action = arg_string(args, "action");

        if (action == "refresh") {
            grade_stack_refresh_profiles();
        } else if (action == "save") {
            grade_stack_save(args);
        } else if (action == "apply") {
            grade_stack_apply(args);
        } else if (action == "remove") {
            grade_stack_remove(args);
        } else if (action == "undo") {
            grade_stack_undo();
        } else if (action == "import") {
            grade_stack_import(args);
        } else if (action == "export" || action == "export_cdl") {
            grade_stack_export(args);
        } else if (action == "delete") {
            grade_stack_delete(args);
        } else {
            grade_stack_set_status(false, fmt::format("Unknown command '{}'.", action));
        }

    } catch (const std::exception &e) {
        grade_stack_set_status(false, e.what());
    }
}

void GradingTool::grade_stack_refresh_profiles() {

    nlohmann::json list      = nlohmann::json::array();
    const std::string folder = grade_stack_folder();
    grade_stack_default_folder_->set_value(folder, false);

    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
        for (const auto &entry : fs::directory_iterator(folder, ec)) {
            const std::string path = entry.path().string();
            if (!entry.is_regular_file() || !utility::ends_with(path, gs::PROFILE_SUFFIX))
                continue;
            try {
                const auto p = gs::load_profile_file(path);
                nlohmann::json item;
                item["path"]         = path;
                item["name"]         = p.name;
                item["id"]           = p.id;
                item["description"]  = p.description;
                item["position"]     = gs::to_string(p.position);
                item["layers"]       = p.layers.size();
                nlohmann::json names = nlohmann::json::array();
                for (const auto &l : p.layers)
                    names.push_back(l.name);
                item["layer_names"] = names;
                list.push_back(item);
            } catch (const std::exception &e) {
                spdlog::warn("Skipping colour profile {}: {}", path, e.what());
            }
        }
    }

    std::sort(list.begin(), list.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
        return utility::to_lower(a["name"].get<std::string>()) <
               utility::to_lower(b["name"].get<std::string>());
    });

    grade_stack_profiles_->set_value(list.dump());
}

std::vector<gs::ExistingLayer>
GradingTool::grade_stack_existing_layers(const caf::actor &media_actor) {

    std::vector<gs::ExistingLayer> result;
    for (const auto &uuid : media_actor_grading_bookmarks(media_actor)) {
        const auto detail = get_bookmark_detail(uuid);
        result.push_back({uuid, detail.user_data_, detail.created_});
    }
    return result;
}

std::vector<caf::actor> GradingTool::grade_stack_target_media(const std::string &scope) const {

    if (scope != "all")
        return selected_media_actors();

    std::vector<caf::actor> result;
    scoped_actor sys{system()};
    try {
        auto session = utility::request_receive<caf::actor>(
            *sys,
            system().registry().template get<caf::actor>(studio_registry),
            session::session_atom_v);

        // the playlist, subset or sequence shown in the media list
        auto container = utility::request_receive<utility::UuidActor>(
            *sys, session, session::active_media_container_atom_v);

        auto media = utility::request_receive<utility::UuidActorVector>(
            *sys, container.actor(), playlist::get_media_atom_v);

        for (const auto &m : media)
            result.push_back(m.actor());

    } catch (const std::exception &e) {
        spdlog::warn("{} {}", __PRETTY_FUNCTION__, e.what());
    }
    return result;
}

void GradingTool::grade_stack_reselect_current_layer() {

    // The panel keeps its own copy of the selected layer's grade. Reload it
    // so a later edit doesn't write the old values back over the profile.
    const auto current = current_bookmark();
    if (!current)
        return;
    select_bookmark(utility::Uuid());
    if (get_bookmark_annotation(current))
        select_bookmark(current);
}

void GradingTool::grade_stack_save(const utility::JsonStore &args) {

    std::string name        = arg_string(args, "name");
    const std::string path  = local_path(arg_string(args, "path"));
    std::string description = arg_string(args, "description");

    if (!current_media_actor_) {
        throw std::runtime_error("There is no shot on screen to take the grade layers from.");
    }

    gs::Profile profile;
    std::string target;

    if (!path.empty()) {
        // update an existing profile from this shot, keeping its id so that
        // shots it was applied to can be synced
        profile = gs::load_profile_file(path);
        target  = path;
        if (!name.empty())
            profile.name = name;
        if (args.contains("description"))
            profile.description = description;
    } else {
        if (name.empty())
            throw std::runtime_error("Please give the profile a name.");

        const fs::path folder(grade_stack_folder());
        const auto file_name = gs::profile_file_name(name);
        const std::string stem =
            file_name.substr(0, file_name.size() - gs::PROFILE_SUFFIX.size());

        // Saving again under the same name updates that profile (same id).
        // A different name that happens to give the same file name gets a
        // numbered file instead of overwriting.
        target = (folder / file_name).string();
        for (int n = 2; fs::exists(target); ++n) {
            try {
                const auto existing = gs::load_profile_file(target);
                if (existing.name == name) {
                    profile.id = existing.id;
                    break;
                }
            } catch (const std::exception &) {
            }
            target = (folder / (fmt::format("{}_{}", stem, n) + gs::PROFILE_SUFFIX)).string();
        }

        profile.name        = name;
        profile.description = description;
        if (profile.id.empty())
            profile.id = utility::to_string(utility::Uuid::generate());
    }

    if (args.contains("position"))
        profile.position = gs::position_from_string(arg_string(args, "position"));
    else if (path.empty())
        profile.position = gs::position_from_string(grade_stack_position_->value());

    // Layers of the shot on screen, in the order they are applied
    std::vector<std::pair<bookmark::BookmarkDetail, utility::JsonStore>> layers;
    for (const auto &uuid : media_actor_grading_bookmarks(current_media_actor_)) {
        auto detail = get_bookmark_detail(uuid);
        auto json   = grading_data_json(get_bookmark_annotation(uuid));
        if (!json.is_null())
            layers.emplace_back(detail, json);
    }
    std::stable_sort(layers.begin(), layers.end(), [](const auto &a, const auto &b) {
        return gs::layer_less(a.first, b.first);
    });

    if (layers.empty()) {
        throw std::runtime_error("The shot on screen has no grade layers to save.");
    }

    profile.layers.clear();
    std::set<std::string> used_ids;
    for (size_t i = 0; i < layers.size(); ++i) {
        auto layer = gs::layer_from_bookmark(
            layers[i].second, layers[i].first.user_data_, profile.id, static_cast<int>(i));
        if (used_ids.count(layer.layer_id))
            layer.layer_id = utility::to_string(utility::Uuid::generate());
        used_ids.insert(layer.layer_id);
        profile.layers.push_back(layer);
    }

    gs::save_profile_file(profile, target);
    grade_stack_refresh_profiles();
    grade_stack_set_status(
        true,
        fmt::format(
            "Saved colour profile '{}' with {} layer{}.",
            profile.name,
            profile.layers.size(),
            profile.layers.size() == 1 ? "" : "s"));
}

void GradingTool::grade_stack_apply(const utility::JsonStore &args) {

    const auto profile = gs::load_profile_file(local_path(arg_string(args, "path")));
    const auto scope   = arg_string(args, "scope");
    const auto position =
        gs::position_from_string(arg_string(args, "position"), profile.position);

    const auto targets = grade_stack_target_media(scope);
    if (targets.empty()) {
        throw std::runtime_error(
            scope == "all" ? "There is no media in the current playlist or sequence."
                           : "Please select one or more media items first.");
    }

    gs::UndoStep undo;
    undo.description = fmt::format("apply '{}'", profile.name);

    size_t shots = 0, added = 0, updated = 0, removed = 0;
    std::vector<std::string> too_many;

    for (const auto &media : targets) {

        const auto existing = grade_stack_existing_layers(media);
        const auto plan     = gs::plan_apply(profile, existing);

        std::vector<gs::ExistingLayer> others;
        for (const auto &l : existing) {
            const auto tag = gs::stack_tag(l.user_data);
            if (!tag || tag->profile_id != profile.id)
                others.push_back(l);
        }
        const auto times = gs::stack_created_times(
            position, others, profile.layers.size(), utility::sysclock::now());

        // 1. layers no longer in the profile
        for (const auto &uuid : plan.remove) {
            undo.removed.push_back(
                {media,
                 uuid,
                 get_bookmark_detail(uuid),
                 grading_data_json(get_bookmark_annotation(uuid))});
            StandardPlugin::remove_bookmark(uuid);
            removed++;
        }

        // 2. layers already on the shot: refresh them in place
        for (const auto &[uuid, index] : plan.update) {
            const auto &layer = profile.layers[index];
            auto old_detail   = get_bookmark_detail(uuid);
            undo.updated.push_back(
                {media, uuid, old_detail, grading_data_json(get_bookmark_annotation(uuid))});

            bookmark::BookmarkDetail bmd;
            bmd.user_data_ =
                gs::make_stack_user_data(profile, layer, position, old_detail.user_data_);
            bmd.created_  = times[index];
            bmd.start_    = timebase::k_flicks_low;
            bmd.duration_ = timebase::k_flicks_max;
            update_bookmark_detail(uuid, bmd);

            GradingData gd(layer.grading_data);
            gd.bookmark_uuid_ = uuid;
            StandardPlugin::update_bookmark_annotation(
                uuid, std::make_shared<const GradingData>(gd), false);
            updated++;
        }

        // 3. new layers
        if (!plan.create.empty()) {
            auto frame_id = media_actor_frame_id(media, 0);
            for (const auto index : plan.create) {
                const auto &layer = profile.layers[index];

                auto bmd       = new_layer_detail();
                bmd.user_data_ = gs::make_stack_user_data(profile, layer, position);
                bmd.created_   = times[index];

                // Profile layers always cover the whole shot
                auto uuid = StandardPlugin::create_bookmark_on_frame(
                    frame_id, "Grading Note", bmd, true);
                if (!uuid) {
                    spdlog::warn("Colour profiles: could not add a layer to a media item");
                    continue;
                }

                GradingData gd(layer.grading_data);
                gd.bookmark_uuid_ = uuid;
                StandardPlugin::update_bookmark_annotation(
                    uuid, std::make_shared<const GradingData>(gd), false);
                undo.created.push_back(uuid);
                added++;
            }
        }

        const size_t total = others.size() + profile.layers.size();
        if (total > MAX_LAYERS_PER_SHOT) {
            too_many.push_back(std::to_string(total));
        }
        shots++;
    }

    if (!undo.empty()) {
        grade_stack_undo_ = undo;
        grade_stack_can_undo_->set_value(true);
    }
    grade_stack_reselect_current_layer();

    std::string message = fmt::format(
        "Applied '{}' {} the shot layers on {} media item{}: {} added, {} updated, {} "
        "removed.",
        profile.name,
        position == gs::StackPosition::Below ? "below" : "above",
        shots,
        shots == 1 ? "" : "s",
        added,
        updated,
        removed);
    if (!too_many.empty()) {
        message += fmt::format(
            " Warning: {} media item{} now {} more than {} grade layers; only the first {} "
            "are shown.",
            too_many.size(),
            too_many.size() == 1 ? "" : "s",
            too_many.size() == 1 ? "has" : "have",
            MAX_LAYERS_PER_SHOT,
            MAX_LAYERS_PER_SHOT);
    }
    grade_stack_set_status(true, message);
}

void GradingTool::grade_stack_remove(const utility::JsonStore &args) {

    const auto profile = gs::load_profile_file(local_path(arg_string(args, "path")));
    const auto scope   = arg_string(args, "scope");
    const auto targets = grade_stack_target_media(scope);

    gs::UndoStep undo;
    undo.description = fmt::format("remove '{}'", profile.name);

    size_t shots = 0;
    for (const auto &media : targets) {
        const auto uuids =
            gs::layers_of_profile(profile.id, grade_stack_existing_layers(media));
        if (uuids.empty())
            continue;
        for (const auto &uuid : uuids) {
            undo.removed.push_back(
                {media,
                 uuid,
                 get_bookmark_detail(uuid),
                 grading_data_json(get_bookmark_annotation(uuid))});
            StandardPlugin::remove_bookmark(uuid);
        }
        shots++;
    }

    if (!undo.empty()) {
        grade_stack_undo_ = undo;
        grade_stack_can_undo_->set_value(true);
    }
    grade_stack_reselect_current_layer();

    grade_stack_set_status(
        true,
        fmt::format(
            "Removed '{}' from {} media item{} ({} layer{}). The shots' own layers were not "
            "changed.",
            profile.name,
            shots,
            shots == 1 ? "" : "s",
            undo.removed.size(),
            undo.removed.size() == 1 ? "" : "s"));
}

void GradingTool::grade_stack_undo() {

    if (grade_stack_undo_.empty()) {
        throw std::runtime_error("Nothing to undo.");
    }

    const auto step   = grade_stack_undo_;
    grade_stack_undo_ = gs::UndoStep();
    grade_stack_can_undo_->set_value(false);

    for (const auto &uuid : step.created) {
        StandardPlugin::remove_bookmark(uuid);
    }

    for (const auto &snap : step.updated) {
        bookmark::BookmarkDetail bmd;
        bmd.user_data_ = snap.detail.user_data_;
        bmd.created_   = snap.detail.created_;
        bmd.start_     = snap.detail.start_;
        bmd.duration_  = snap.detail.duration_;
        update_bookmark_detail(snap.bookmark, bmd);
        if (!snap.grading_data.is_null()) {
            GradingData gd(snap.grading_data);
            gd.bookmark_uuid_ = snap.bookmark;
            StandardPlugin::update_bookmark_annotation(
                snap.bookmark, std::make_shared<const GradingData>(gd), false);
        }
    }

    for (const auto &snap : step.removed) {
        if (!snap.media || snap.grading_data.is_null())
            continue;
        auto frame_id  = media_actor_frame_id(snap.media, 0);
        auto bmd       = new_layer_detail();
        bmd.user_data_ = snap.detail.user_data_;
        bmd.created_   = snap.detail.created_;
        const bool full_clip =
            !snap.detail.duration_ || *snap.detail.duration_ == timebase::k_flicks_max;
        auto uuid =
            StandardPlugin::create_bookmark_on_frame(frame_id, "Grading Note", bmd, true);
        if (!uuid)
            continue;
        if (!full_clip) {
            bookmark::BookmarkDetail range;
            range.start_    = snap.detail.start_;
            range.duration_ = snap.detail.duration_;
            update_bookmark_detail(uuid, range);
        }
        GradingData gd(snap.grading_data);
        gd.bookmark_uuid_ = uuid;
        StandardPlugin::update_bookmark_annotation(
            uuid, std::make_shared<const GradingData>(gd), false);
    }

    grade_stack_reselect_current_layer();
    grade_stack_set_status(true, fmt::format("Undid {}.", step.description));
}

void GradingTool::grade_stack_import(const utility::JsonStore &args) {

    const auto source  = local_path(arg_string(args, "source"));
    const auto profile = gs::load_profile_file(source);

    const fs::path folder(grade_stack_folder());
    const auto file_name   = gs::profile_file_name(profile.name);
    const std::string stem = file_name.substr(0, file_name.size() - gs::PROFILE_SUFFIX.size());

    // Same profile (same id) replaces the old copy, anything else gets a
    // free file name.
    std::string target = (folder / file_name).string();
    for (int n = 2; fs::exists(target); ++n) {
        try {
            if (gs::load_profile_file(target).id == profile.id)
                break;
        } catch (const std::exception &) {
        }
        target = (folder / (fmt::format("{}_{}", stem, n) + gs::PROFILE_SUFFIX)).string();
    }

    gs::save_profile_file(profile, target);
    grade_stack_refresh_profiles();
    grade_stack_set_status(true, fmt::format("Imported colour profile '{}'.", profile.name));
}

void GradingTool::grade_stack_export(const utility::JsonStore &args) {

    const auto profile = gs::load_profile_file(local_path(arg_string(args, "path")));
    std::string target = local_path(arg_string(args, "target"));
    if (target.empty())
        throw std::runtime_error("No file name given.");

    if (arg_string(args, "action") == "export_cdl") {
        const auto notes = gs::write_profile_cdl(profile, target);
        std::string message =
            fmt::format("Exported '{}' as ASC CDL to {}.", profile.name, target);
        if (!notes.empty()) {
            message += " Not included: ";
            for (size_t i = 0; i < notes.size(); ++i)
                message += (i ? " " : "") + notes[i];
        }
        grade_stack_set_status(true, message);
    } else {
        if (!utility::ends_with(target, ".json"))
            target += gs::PROFILE_SUFFIX;
        gs::save_profile_file(profile, target);
        grade_stack_set_status(
            true, fmt::format("Exported colour profile '{}' to {}.", profile.name, target));
    }
}

void GradingTool::grade_stack_delete(const utility::JsonStore &args) {

    // Deleted profiles are moved into a 'deleted' sub folder, not erased.
    const fs::path source(local_path(arg_string(args, "path")));
    const auto profile = gs::load_profile_file(source.string());

    const fs::path bin = fs::path(grade_stack_folder()) / "deleted";
    std::error_code ec;
    fs::create_directories(bin, ec);
    const fs::path target = bin / source.filename();
    fs::remove(target, ec);
    fs::rename(source, target, ec);
    if (ec) {
        throw std::runtime_error(
            fmt::format("Could not delete {}: {}", source.string(), ec.message()));
    }

    grade_stack_refresh_profiles();
    grade_stack_set_status(
        true,
        fmt::format("Deleted colour profile '{}' (moved to {}).", profile.name, bin.string()));
}

void GradingTool::grade_stack_restack_above_layers(const caf::actor &media_actor) {

    // Profile layers placed 'above' are always applied last. Keep them at
    // the top of the layer list too when a new shot layer is added (the list
    // sorts by created time).
    if (!media_actor)
        return;

    const auto existing = grade_stack_existing_layers(media_actor);

    std::vector<std::pair<bookmark::BookmarkDetail, utility::Uuid>> above;
    std::vector<gs::ExistingLayer> others;
    for (const auto &l : existing) {
        if (gs::order_group(l.user_data) == 2) {
            bookmark::BookmarkDetail d;
            d.user_data_ = l.user_data;
            d.created_   = l.created;
            above.emplace_back(d, l.bookmark);
        } else {
            others.push_back(l);
        }
    }
    if (above.empty())
        return;

    std::stable_sort(above.begin(), above.end(), [](const auto &a, const auto &b) {
        return gs::layer_less(a.first, b.first);
    });

    bool needed = false;
    for (const auto &o : others) {
        if (o.created &&
            *o.created >= above.front().first.created_.value_or(utility::sys_time_point::min()))
            needed = true;
    }
    if (!needed)
        return;

    const auto times = gs::stack_created_times(
        gs::StackPosition::Above, others, above.size(), utility::sysclock::now());
    for (size_t i = 0; i < above.size(); ++i) {
        bookmark::BookmarkDetail bmd;
        bmd.created_ = times[i];
        update_bookmark_detail(above[i].second, bmd);
    }
}
