// SPDX-License-Identifier: Apache-2.0
#pragma once

// Plain helpers (no actors, no Qt) for the "export with grade" workflow of
// the video renderer:
//
//  - reusable, named export profiles that are stored in the user
//    preferences as a JSON list
//  - expansion of file naming templates such as
//    "{media_name}_{date}_{version}"
//
// Everything in here is pure logic so that it can be unit tested.

#include <chrono>
#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace xstudio::video_render_plugin_1_0::export_profiles {

// ---------------------------------------------------------------------------
// Export profiles
// ---------------------------------------------------------------------------

// A profile with every known field set to its default value. Profiles are
// JSON objects with these keys:
//
//  name                 display name, must be unique and not empty
//  video_preset         name of a "Video & Audio Encoding Preset"
//  container            output file extension (mov, mp4, mkv, mxf, webm,
//                       png, jpg, tif, ...). Image formats give frame
//                       sequences.
//  resolution           resolution option text, e.g. "1920x1080 (HD)".
//                       Empty means "leave the current choice".
//  frame_rate           frame rate text, e.g. "24.0". Empty means "leave".
//  ocio_display         OCIO display. Empty means "use the default".
//  ocio_view            OCIO view. Empty means "use the default".
//  bake_grade           true: the xSTUDIO grade is burnt into the pixels.
//                       false: the render is left ungraded.
//  sidecar_cdl          only used when bake_grade is false: write the grade
//                       as an ASC CDL collection (.ccc) next to the render
//  sidecar_lut          only used when bake_grade is false: write the grade
//                       as a 3D LUT (.cube) next to the render
//  output_folder        folder for the render. Empty means "leave".
//  naming_template      file name without extension, may use tokens
//  include_annotations  burn annotations into the render
//  render_audio         include audio
nlohmann::json default_export_profile();

// Returns a copy of 'profile' where missing or badly typed fields are
// replaced by their defaults. Unknown fields are kept untouched so that
// profiles written by newer versions survive a round trip through older
// ones.
nlohmann::json normalise_export_profile(const nlohmann::json &profile);

// Normalises a list of profiles: entries that are not objects or have an
// empty name are dropped, later duplicates of a name are dropped. A value
// that is not an array gives an empty list.
nlohmann::json normalise_export_profiles(const nlohmann::json &profiles);

// Returns the index of the profile called 'name', or -1.
int find_export_profile(const nlohmann::json &profiles, const std::string &name);

// Adds 'profile' to 'profiles', replacing an existing profile with the same
// name. Returns false (and leaves the list alone) if the profile has no name.
bool save_export_profile(nlohmann::json &profiles, const nlohmann::json &profile);

// Renames a profile. Fails (returns false and fills 'error') if the old
// profile does not exist, the new name is empty or already used.
bool rename_export_profile(
    nlohmann::json &profiles,
    const std::string &old_name,
    const std::string &new_name,
    std::string &error);

// Removes the profile called 'name'. Returns false if there was none.
bool delete_export_profile(nlohmann::json &profiles, const std::string &name);

// Builds the output file path for a profile: folder + naming template +
// container extension. Image containers get a "####" frame number part if
// the template does not have one already.
std::string
profile_output_path(const nlohmann::json &profile, const std::string &fallback_folder);

// True for containers that are written as one image file per frame.
bool is_frame_based_container(const std::string &container);

// ---------------------------------------------------------------------------
// Naming templates
// ---------------------------------------------------------------------------

// Makes a token value safe for use in a file name: path separators and
// characters that are not allowed on Windows become '_', whitespace runs
// become a single '_'.
std::string sanitise_token_value(const std::string &value);

// Tokens that come from the clock: {date} (YYYYMMDD), {time} (HHMMSS) and
// {year}.
std::map<std::string, std::string>
clock_tokens(const std::chrono::system_clock::time_point &when);

// Replaces every {token} in 'tmpl' by its (sanitised) value. {version}
// becomes "v" followed by 'version' padded to three digits. Tokens that are
// not known are left as they are, so mistakes stay visible.
std::string expand_naming_template(
    const std::string &tmpl, const std::map<std::string, std::string> &tokens, int version = 1);

// True if 'tmpl' contains at least one {token}.
bool has_tokens(const std::string &tmpl);

// Like expand_naming_template, but picks the lowest version number (from 1)
// for which 'exists' says the expanded path is not taken yet. Without a
// {version} token this is the same as expand_naming_template. The chosen
// number is written to 'version_used' if that is not null.
std::string expand_naming_template_next_version(
    const std::string &tmpl,
    const std::map<std::string, std::string> &tokens,
    const std::function<bool(const std::string &)> &exists,
    int *version_used = nullptr);

// Checks if an output path is already taken on disk. For frame sequences
// ("name.####.png") any file starting with "name." in that folder counts.
bool output_path_exists(const std::string &path);

} // namespace xstudio::video_render_plugin_1_0::export_profiles
