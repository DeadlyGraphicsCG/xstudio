// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include <OpenColorIO/OpenColorIO.h> //NOLINT

#include "xstudio/utility/json_store.hpp"

namespace OCIO = OCIO_NAMESPACE;

// Helpers for the per layer "working space" of the grading tool: the OCIO
// colour space a grade layer is applied in. They have no actor or GPU
// dependencies so they can be unit tested on their own.

namespace xstudio::colour_pipeline::grading {

// Name of the colour space the pixels are in when they reach the grading
// operation (the 'working_space' entry of the media colour metadata, or
// "scene_linear" when it is not set). Layers using this space need no
// conversion.
std::string reference_working_space(const utility::JsonStore &colour_metadata);

// Short text that identifies the OCIO config, context and reference space of
// some media. Used to build cache keys.
std::string colour_metadata_key(const utility::JsonStore &colour_metadata);

// Loads the OCIO config named in the media colour metadata ('ocio_config').
// Configs are loaded once and then shared. Never returns null: if the config
// can't be loaded the OCIO 'raw' config is returned instead.
OCIO::ConstConfigRcPtr ocio_config(const utility::JsonStore &colour_metadata);

// The OCIO context of the media: the config context plus the per media
// variables found in 'ocio_context' (e.g. SHOT).
OCIO::ConstContextRcPtr
ocio_context(const OCIO::ConstConfigRcPtr &config, const utility::JsonStore &colour_metadata);

// Colour spaces offered in the "working space" menu of a grade layer. Useful
// roles come first (scene_linear, compositing_log and color_timing when the
// config defines them), followed by every active, scene referred colour space
// of the config that is not a 'data' space.
std::vector<std::string> working_space_choices(const OCIO::ConstConfigRcPtr &config);

// Working spaces always compiled into the grading shader, so the common case
// never needs a different shader. This is "compositing_log", which was the only
// working space available before working spaces could be picked from the config.
const std::vector<std::string> &default_shader_working_spaces();

// Returns the list of working spaces the shader must support so that every
// space in 'requested' is available. It starts with the default spaces and then
// adds the others in sorted order, so the same set of layers always gives the
// same shader. Empty names and the reference space need no conversion and are
// left out.
std::vector<std::string>
shader_working_spaces(const std::vector<std::string> &requested, const std::string &reference);

// Value for the 'color_space' shader uniform of a layer: 0 for the reference
// space (no conversion), i + 1 for shader_spaces[i], or -1 when the shader does
// not support that space.
int working_space_index(
    const std::vector<std::string> &shader_spaces,
    const std::string &space,
    const std::string &reference);

struct WorkingSpaceShader {
    // GLSL code with one pair of OCIO conversion functions per working space
    // and these two functions, where 'space' is a working_space_index() value:
    //   vec4 grading_to_working_space(vec4 rgba, int space);   // reference -> space
    //   vec4 grading_from_working_space(vec4 rgba, int space); // space -> reference
    std::string glsl;

    // The OCIO shader descriptions, needed to upload any LUT textures.
    std::vector<OCIO::ConstGpuShaderDescRcPtr> shader_descs;
};

// Builds the GLSL conversion code for the given working spaces. If OCIO can't
// convert to one of the spaces (unknown name, config not found...) that space
// falls back to "no conversion" rather than failing.
WorkingSpaceShader build_working_space_shader(
    const utility::JsonStore &colour_metadata, const std::vector<std::string> &spaces);

} // namespace xstudio::colour_pipeline::grading
