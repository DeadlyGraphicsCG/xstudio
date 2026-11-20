// SPDX-License-Identifier: Apache-2.0
#pragma once

// Grade stack profiles ("colour profiles").
//
// A profile is a named, ordered list of grade layers that can be saved to a
// portable JSON file and applied to many shots at once (selected media, or a
// whole playlist / sequence). Layers created on a shot from a profile carry a
// small tag in their bookmark user_data so that they can be found again later:
// re-applying a profile updates those layers in place ("sync") instead of
// adding copies, and the shot's own layers are never touched.
//
// Everything in this file is plain data + pure functions so it can be unit
// tested without a running xSTUDIO session. The plugin side (bookmarks,
// media, UI attributes) lives in grade_stack_actions.cpp.

#include <optional>
#include <string>
#include <vector>

#include "xstudio/bookmark/bookmark.hpp"
#include "xstudio/utility/chrono.hpp"
#include "xstudio/utility/json_store.hpp"
#include "xstudio/utility/uuid.hpp"

namespace xstudio::ui::viewport::grade_stack {

// Value of the "schema" key at the top of every profile file.
inline const std::string PROFILE_SCHEMA = "xstudio.grade_stack";
// Bump this when the file layout changes in a way older readers can't handle.
// Readers accept files with a version <= PROFILE_VERSION and ignore unknown
// keys, so new optional keys do not need a version bump.
inline constexpr int PROFILE_VERSION = 1;
// File name suffix used for profiles in the profile folder.
inline const std::string PROFILE_SUFFIX = ".xsgrade.json";
// Key in a grade layer's bookmark user_data that marks it as coming from a
// profile. Layers without this key are ordinary per-shot layers.
inline const std::string STACK_TAG_KEY = "grade_stack";

// Where profile layers go relative to the shot's own layers.
// Below = applied first (like a "pre-clip" group grade),
// Above = applied last (like a "post-clip" group grade).
enum class StackPosition { Below, Above };

std::string to_string(StackPosition p);
// Accepts "below"/"above" (any case). Anything else gives the fallback.
StackPosition
position_from_string(const std::string &s, StackPosition fallback = StackPosition::Below);

struct ProfileLayer {
    // Stable id of this layer inside its profile, used to match layers on a
    // shot when the profile is applied again.
    std::string layer_id;
    std::string name;
    bool enabled{true};
    // The layer's GradingData exactly as written by GradingDataSerialiser
    // (the same JSON xSTUDIO stores in sessions), so any new grade fields
    // are carried along automatically.
    utility::JsonStore grading_data;
    // Other per-layer bookmark user_data (minus the stack tag, "grade_active"
    // and "layer_name", which have their own fields above).
    utility::JsonStore user_data;
};

struct Profile {
    std::string id;
    std::string name;
    std::string description;
    StackPosition position{StackPosition::Below};
    std::vector<ProfileLayer> layers; // in the order they are applied
};

// ---------------------------------------------------------------------------
// JSON (de)serialisation of profile files

utility::JsonStore profile_to_json(const Profile &profile);

// Throws std::runtime_error with a readable message if the data is not a
// usable profile. Unknown keys are ignored. Missing optional keys get
// defaults. Each layer's grading_data is checked by building a GradingData.
Profile profile_from_json(const nlohmann::json &data);

// File name (no folder) for a profile name, e.g. "Show Look" ->
// "Show_Look.xsgrade.json".
std::string profile_file_name(const std::string &profile_name);

// Read / write a profile file. Throw std::runtime_error on failure.
Profile load_profile_file(const std::string &path);
void save_profile_file(const Profile &profile, const std::string &path);

// ---------------------------------------------------------------------------
// Stack tag stored in a layer's bookmark user_data

struct StackTag {
    std::string profile_id;
    std::string profile_name;
    std::string layer_id;
    std::string layer_name; // the layer's own name, without the profile prefix
    StackPosition position{StackPosition::Below};
    // The profile's on/off setting for this layer when it was last applied.
    bool profile_enabled{true};
};

std::optional<StackTag> stack_tag(const utility::JsonStore &user_data);
std::optional<StackTag> stack_tag(const std::optional<utility::JsonStore> &user_data);

// Name shown in the layer list for a layer that came from a profile.
std::string stack_layer_display_name(const Profile &profile, const ProfileLayer &layer);

// Bookmark user_data for a layer created / updated from a profile. If the
// layer already exists on the shot, pass its current user_data so that the
// shot's own on/off choice for that layer is kept.
utility::JsonStore make_stack_user_data(
    const Profile &profile,
    const ProfileLayer &layer,
    StackPosition position,
    const std::optional<utility::JsonStore> &existing_user_data = std::nullopt);

// Builds a profile layer from a shot's grade layer. If the shot layer itself
// came from the profile being (re)saved, its layer id is kept so that a later
// sync updates the matching layers in place.
ProfileLayer layer_from_bookmark(
    const utility::JsonStore &grading_data_json,
    const std::optional<utility::JsonStore> &user_data,
    const std::string &profile_id,
    int layer_index);

// ---------------------------------------------------------------------------
// Layer order

// 0 = profile layers placed below, 1 = ordinary shot layers, 2 = profile
// layers placed above. Untagged (old) layers are always group 1.
int order_group(const std::optional<utility::JsonStore> &user_data);

// Strict weak ordering for grade layers: by order group, then by created
// time (as xSTUDIO always did). Layers without a created time sort first.
bool layer_less(const bookmark::BookmarkDetail &a, const bookmark::BookmarkDetail &b);

// ---------------------------------------------------------------------------
// Apply / sync planning

struct ExistingLayer {
    utility::Uuid bookmark;
    std::optional<utility::JsonStore> user_data;
    std::optional<utility::sys_time_point> created;
};

struct ApplyPlan {
    // indices into profile.layers that need a new layer on the shot
    std::vector<size_t> create;
    // existing layer to refresh from profile.layers[second]
    std::vector<std::pair<utility::Uuid, size_t>> update;
    // stack layers of this profile that are no longer in it (or duplicates)
    std::vector<utility::Uuid> remove;
};

// Works out what to do on one shot so that it ends up with exactly one layer
// per profile layer. Only layers tagged with profile.id are touched.
ApplyPlan plan_apply(const Profile &profile, const std::vector<ExistingLayer> &existing);

// Stack layers of the given profile on a shot (to remove a profile).
std::vector<utility::Uuid>
layers_of_profile(const std::string &profile_id, const std::vector<ExistingLayer> &existing);

// Created times for the profile's layers on one shot so that xSTUDIO's layer
// list (which sorts by created time) shows them in the same place as they are
// applied: before every other layer for Below, after every other layer for
// Above. 'others' are the shot's layers that do not belong to this profile.
std::vector<utility::sys_time_point> stack_created_times(
    StackPosition position,
    const std::vector<ExistingLayer> &others,
    size_t count,
    utility::sys_time_point now);

// What one apply / remove changed, so it can be undone in one go.
struct UndoStep {
    std::string description;
    // layers that were added (undo removes them)
    std::vector<utility::Uuid> created;
    // layers that were changed or removed, as they were before
    struct LayerSnapshot {
        caf::actor media;
        utility::Uuid bookmark;
        bookmark::BookmarkDetail detail;
        utility::JsonStore grading_data;
    };
    std::vector<LayerSnapshot> updated;
    std::vector<LayerSnapshot> removed;

    [[nodiscard]] bool empty() const {
        return created.empty() && updated.empty() && removed.empty();
    }
};

// ---------------------------------------------------------------------------
// ASC CDL export (for OCIO / Nuke / Resolve). One ColorCorrection per layer,
// in stack order, written as .ccc (ColorCorrectionCollection), .cdl
// (ColorDecisionList) or .cc (first enabled layer only). Exposure is folded
// into the slope (as the existing single layer CDL export does). Things CDL
// can't hold (contrast, masks, the grading colour space) are listed in each
// correction's <Description>. Returns the notes about what was left out.
std::vector<std::string> write_profile_cdl(const Profile &profile, const std::string &path);

} // namespace xstudio::ui::viewport::grade_stack
