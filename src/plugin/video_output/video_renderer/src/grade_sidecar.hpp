// SPDX-License-Identifier: Apache-2.0
#pragma once

// Helpers that let the video renderer deal with xSTUDIO grades without
// linking to the grading plugin. Grades are bookmarks with the user type
// "Grading" whose annotation holds the grade values. We read them from the
// JSON that the bookmarks actor serialises to, so this file only needs
// nlohmann::json and OpenColorIO.
//
// Used for:
//  - leaving the grade out of a render (strip_grading_bookmarks)
//  - writing the grade next to an ungraded render, as an ASC CDL collection
//    (.ccc) and/or a 3D LUT (.cube)

#include <array>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace xstudio::video_render_plugin_1_0::grade_sidecar {

// One grade layer, with the same maths as the GLSL in grading_colour_op.cpp
struct GradeLayer {
    std::string name;
    // OCIO colour space (or role) the layer is applied in. Empty or
    // "scene_linear" means the space the pixels arrive in.
    std::string colour_space{"scene_linear"};
    std::array<double, 4> slope{1.0, 1.0, 1.0, 1.0};
    std::array<double, 4> offset{0.0, 0.0, 0.0, 0.0};
    std::array<double, 4> power{1.0, 1.0, 1.0, 1.0};
    double sat{1.0};
    double exposure{0.0};
    double contrast{1.0};
    // optional per channel trims on top of the master values: exposure_rgb
    // is added (stops), contrast_rgb and sat_rgb are multiplied
    std::array<double, 3> exposure_rgb{0.0, 0.0, 0.0};
    std::array<double, 3> contrast_rgb{1.0, 1.0, 1.0};
    std::array<double, 3> sat_rgb{1.0, 1.0, 1.0};
    bool active{true};
    bool masked{false};
    bool full_range{true};

    bool is_log() const { return colour_space == "compositing_log"; }
    bool is_identity() const;

    // master and per channel values combined
    std::array<double, 3> effective_exposure() const;
    std::array<double, 3> effective_contrast() const;
    std::array<double, 3> effective_sat() const;
};

// True if the serialised bookmark (an entry of the "actors" dict that
// BookmarksActor serialises to) is a grade layer.
bool is_grading_bookmark(const nlohmann::json &bookmark_actor_json);

// Copy of a BookmarksActor serialisation without any grade layers.
nlohmann::json strip_grading_bookmarks(const nlohmann::json &bookmarks_serialisation);

// Number of grade layers in a BookmarksActor serialisation.
size_t count_grading_bookmarks(const nlohmann::json &bookmarks_serialisation);

// All grade layers owned by the media with the given uuid, in the order they
// are applied (creation time, oldest first).
std::vector<GradeLayer> grade_layers_for_media(
    const nlohmann::json &bookmarks_serialisation, const std::string &media_uuid);

// Name used for a clip in sidecar files: the OCIO context SHOT variable if
// there is one, otherwise the file name (without extension) of the media,
// otherwise 'fallback'. Same rule as the grading panel's "Save CDL".
std::string
clip_name_from_colour_params(const nlohmann::json &colour_params, const std::string &fallback);

// Applies one layer (without mask) to a scene linear or log RGB triplet,
// exactly like apply_grade() in the grading shader.
std::array<float, 3>
apply_grade_maths(const std::array<float, 3> &rgb, const GradeLayer &layer);

// The CDL values for a layer. Exposure is folded into the slope, as the
// grading panel's own CDL export does. 'exact' is false if the layer uses
// contrast or a different saturation per channel, which a CDL cannot
// carry (the master saturation is used then).
struct CDLValues {
    std::array<double, 3> slope{1.0, 1.0, 1.0};
    std::array<double, 3> offset{0.0, 0.0, 0.0};
    std::array<double, 3> power{1.0, 1.0, 1.0};
    double sat{1.0};
    bool exact{true};
};
CDLValues layer_to_cdl(const GradeLayer &layer);

struct ClipGrade {
    std::string clip_name;
    std::vector<GradeLayer> layers;
    nlohmann::json colour_params; // media source colour params (ocio_config etc.)
};

// Writes an ASC CDL collection (.ccc) with one ColorCorrection per active
// layer. Ids are the clip name (one layer) or clip name + layer name.
// Notes about things a CDL can't hold (masks, contrast, part-clip layers)
// are added to 'warnings'. Returns the file text, empty if there was
// nothing to write.
std::string make_ccc(const std::vector<ClipGrade> &clips, std::vector<std::string> &warnings);

// Bakes the whole grade stack of one clip into a 3D LUT in .cube format.
// The LUT works on images encoded in the OCIO config's compositing_log
// colour space (the space xSTUDIO uses for log grades), in and out. Layers
// are applied in their own working space, converted from the media's
// working space ('working_space' colour param, scene_linear by default),
// like the grading shader does. If the config can't be used the LUT works
// directly on the pixel values. Masked layers are left out (with a
// warning). Returns the file text, empty if the clip has no active grade.
std::string
bake_cube(const ClipGrade &clip, const int cube_size, std::vector<std::string> &warnings);

} // namespace xstudio::video_render_plugin_1_0::grade_sidecar
