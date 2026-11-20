// SPDX-License-Identifier: Apache-2.0

#include <any>
#include <limits>
#include <map>
#include <mutex>
#include <caf/actor_registry.hpp>

#include "xstudio/ui/opengl/shader_program_base.hpp"
#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/string_helpers.hpp"

#include "grading.h"
#include "grading_colour_op.hpp"
#include "grading_mask_render_data.h"
#include "grading_mask_gl_renderer.h"
#include "grading_common.h"
#include "grading_working_space.h"

using namespace xstudio;
using namespace xstudio::bookmark;
using namespace xstudio::colour_pipeline;
using namespace xstudio::ui::viewport;


namespace {


const char *fragment_shader = R"(
#version 410 core
// Index of the space the pixels arrive in (the media working space, usually
// scene_linear). Other indexes are the working spaces of the grade layers.
#define REFERENCE_SPACE 0

struct Grade {
    int color_space;
    bool grade_active;

    // Masking
    bool mask_active;
    bool mask_editing;

    // CDL
    vec3 slope;
    vec3 offset;
    vec3 power;
    vec3 sat;

    // Per channel values (master and R/G/B trims already combined)
    vec3 exposure;
    vec3 contrast;

    // Can be extended with more grading operations
};

uniform bool      tool_active;
uniform int       grade_count;
uniform sampler2D masks[8];
uniform Grade     grades[8];

vec4 apply_grade(vec4 rgba, int layer_index)
{

    Grade grade = grades[layer_index];

    vec4 outColor = rgba;

    // Exposure

    if (grade.exposure != vec3(0., 0., 0.))
    {
        outColor.rgb *= pow(vec3(2.0), grade.exposure);
    }

    // Contrast

    if (grade.contrast != vec3(1., 1., 1.))
    {
        outColor.rgb = pow( abs(outColor.rgb / 0.18), grade.contrast) * sign(outColor.rgb) * 0.18;
    }

    // CDL SOP

    if (grade.slope != vec3(1., 1., 1.)
     || grade.offset != vec3(0., 0., 0.)
     || grade.power != vec3(1., 1., 1.))
    {
        outColor.rgb *= grade.slope;
        outColor.rgb += grade.offset;
        if (grade.power != vec3(1., 1., 1.))
        {
            // Strict CDL specs do not match Nuke, using OCIO mirrored style here.
            // outColor.rgb = pow(clamp(outColor.rgb, 0.0, 1.0), grade.power);
            outColor.rgb = pow(abs(outColor.rgb), grade.power) * sign(outColor.rgb);
        }
    }

    // CDL Sat

    // Per channel saturation works like Nuke's ColorCorrect: each channel is
    // pushed away from (or pulled towards) the luma by its own amount.

    if (grade.sat != vec3(1., 1., 1.))
    {
        vec3 lumaWgts = vec3(0.212599993, 0.715200007, 0.0722000003);
        float luma = dot(outColor.rgb, lumaWgts);
        outColor.rgb = luma + grade.sat * (outColor.rgb - luma);
    }

    // Can be extended with more grading operations

    return outColor;
}

vec4 apply_layer(vec4 rgba, vec2 image_pos, int layer_index)
{

    Grade grade = grades[layer_index];
    vec4 mask_color = grade.mask_active ? texture(masks[layer_index], image_pos) : vec4(1.0);
    float mask_alpha = clamp(mask_color.a, 0.0, 1.0);

    if (grade.mask_active && !grade.mask_editing)
    {
        vec4 graded_col = apply_grade(rgba, layer_index);
        return vec4(mix(rgba.rgb, graded_col.rgb, mask_alpha), rgba.a);
    }
    else if (grade.mask_active)
    {
        float mask_opacity = 0.5 * mask_alpha;
        return vec4(mix(rgba.rgb, mask_color.rgb, mask_opacity), rgba.a);
    }
    else
    {
        return apply_grade(rgba, layer_index);
    }
}

//INJECT_WORKING_SPACES

vec4 apply_color_conv(vec4 rgba, int source_space, int dest_space)
{
    if (source_space != dest_space)
    {
        // Always go through the reference space
        if (source_space != REFERENCE_SPACE)
        {
            rgba = grading_from_working_space(rgba, source_space);
        }
        if (dest_space != REFERENCE_SPACE)
        {
            rgba = grading_to_working_space(rgba, dest_space);
        }
    }

    return rgba;
}

vec4 colour_transform_op(vec4 rgba, vec2 image_pos)
{
    vec4 image_col = rgba;

    if (tool_active)
    {
        // xStudio guarantees the pixels arrive in the media working space
        int current_space = REFERENCE_SPACE;

        for (int i = 0; i < grade_count; ++i)
        {
            if (grades[i].grade_active) {
                if (grades[i].color_space != current_space)
                {
                    image_col = apply_color_conv(image_col, current_space, grades[i].color_space);
                    current_space = grades[i].color_space;
                }
                image_col = apply_layer(image_col, image_pos, i);
            }
        }

        if (current_space != REFERENCE_SPACE)
        {
            image_col = apply_color_conv(image_col, current_space, REFERENCE_SPACE);
        }
    }

    return image_col;
}
)";

} // anonymous namespace

GradingColourOperator::GradingColourOperator(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : ColourOpPlugin(cfg, "GradingColourOperator", init_settings) {

    // ask plugin manager for the instance of the GradingTool plugin
    auto pm = system().registry().template get<caf::actor>(plugin_manager_registry);
    mail(plugin_manager::get_resident_atom_v, GradingTool::PLUGIN_UUID)
        .request(pm, infinite)
        .then(
            [=](caf::actor grading_tool) mutable {
                // ping the grading tool with a pointer to ourselves, so it can
                // send us updates on the 'bypass' attr. GradingTool of course has
                // the necessary message handler for this
                anon_mail("follow_bypass", caf::actor_cast<caf::actor>(this))
                    .send(grading_tool);
            },
            [=](caf::error &err) mutable {

            });
}

caf::message_handler GradingColourOperator::message_handler_extensions() {

    // here's our handler for the messages coming from the GradingTool about
    // the state of its 'bypass' attribute.
    return caf::message_handler(
               {[=](utility::event_atom, const std::string &desc, bool bypass) {
                   if (desc == "bypass") {
                       bypass_ = bypass;
                   }
               }})
        .or_else(ColourOpPlugin::message_handler_extensions());
}

ColourOperationDataPtr GradingColourOperator::colour_op_graphics_data(
    utility::UuidActor &media_source, const utility::JsonStore &media_source_colour_metadata) {

    // The result of this call depends on the ocio config and on the
    // ocio_context (for some jobs the lin to log transform varies PER SHOT),
    // both are part of the cache key used by colour_op_data_for(). Here we
    // build the shader with the default working spaces only. If a grade layer
    // on the image uses another working space, the GradingMaskRenderer
    // pre-draw hook swaps in a shader that supports it just before drawing.
    return colour_op_data_for(
        media_source_colour_metadata, grading::default_shader_working_spaces());
}

utility::JsonStore
GradingColourOperator::update_shader_uniforms(const media_reader::ImageBufPtr &image) {

    utility::JsonStore uniforms_dict;
    uniforms_dict["grade_count"] = 0;
    uniforms_dict["tool_active"] = false;

    if (bypass_) {
        return uniforms_dict;
    }

    // The working spaces built into the shader that will draw this image, so
    // that the 'color_space' index we send matches the shader.
    const std::vector<std::string> shader_spaces = image_working_spaces(image);
    const std::string reference = grading::reference_working_space(image.frame_id().params());

    size_t grade_count = 0;
    auto active_grades = get_active_grades(image);
    for (const auto grade_info : active_grades) {

        auto grade_data = grade_info.data;

        std::string grade_str = fmt::format("grades[{}].", grade_count);

        uniforms_dict[grade_str + "grade_active"] = grade_info.grade_active;

        // Index of the layer working space in the shader. A space the shader
        // does not know yet (-1) is applied in the reference space for now;
        // the pre-draw hook replaces the shader and fixes this index before
        // the image is drawn.
        uniforms_dict[grade_str + "color_space"] = std::max(
            0,
            grading::working_space_index(shader_spaces, grade_data->colour_space(), reference));
        uniforms_dict[grade_str + "mask_active"] = !grade_data->mask().empty();

        // NOTE (Ted) ..
        // When grade_data->mask_editing() is true, the mask is drawn applied
        // to the image as a yellow mix - in other words the grade isn't applied.
        // but the mask is shown in transparent yellow.
        // This depends on the 'display_mode' attr in the plugin, but ther is
        // nowhere in the UI to control this. Therefore, I disable here because
        // otherwise I'm sometimes seeing the mask_editing() flag as true, seems
        // a bit random and it's undesired
        uniforms_dict[grade_str + "mask_editing"] = false;
        // Here's what we used to have here:
        // uniforms_dict[grade_str + "mask_editing"] = grade_data->mask_editing();

        const auto &grade = grade_data->grade();

        uniforms_dict[grade_str + "slope"] = {
            "vec3",
            1,
            grade.slope[0] * grade.slope[3],
            grade.slope[1] * grade.slope[3],
            grade.slope[2] * grade.slope[3]};
        uniforms_dict[grade_str + "offset"] = {
            "vec3",
            1,
            grade.offset[0] + grade.offset[3],
            grade.offset[1] + grade.offset[3],
            grade.offset[2] + grade.offset[3]};
        uniforms_dict[grade_str + "power"] = {
            "vec3",
            1,
            grade.power[0] * grade.power[3],
            grade.power[1] * grade.power[3],
            grade.power[2] * grade.power[3]};

        // Master and per channel values are combined here, the shader only
        // sees the final value per channel.
        const auto sat      = grade.effective_sat();
        const auto exposure = grade.effective_exposure();
        const auto contrast = grade.effective_contrast();

        uniforms_dict[grade_str + "sat"]      = {"vec3", 1, sat[0], sat[1], sat[2]};
        uniforms_dict[grade_str + "exposure"] = {
            "vec3", 1, exposure[0], exposure[1], exposure[2]};
        uniforms_dict[grade_str + "contrast"] = {
            "vec3", 1, contrast[0], contrast[1], contrast[2]};

        grade_count++;
    }

    if (grade_count) {
        uniforms_dict["grade_count"] = grade_count;
        uniforms_dict["tool_active"] = true;
    }

    // std::cerr << uniforms_dict.dump() << "\n";

    return uniforms_dict;
}

std::vector<std::string>
GradingColourOperator::image_working_spaces(const media_reader::ImageBufPtr &image) {

    if (image.colour_pipe_data()) {
        for (const auto &op : image.colour_pipe_data()->operations()) {
            if (op && op->uuid_ == PLUGIN_UUID) {
                if (const auto *spaces =
                        std::any_cast<std::vector<std::string>>(&op->user_data_)) {
                    return *spaces;
                }
            }
        }
    }
    return grading::default_shader_working_spaces();
}

ColourOperationDataPtr GradingColourOperator::colour_op_data_for(
    const utility::JsonStore &media_source_colour_metadata,
    const std::vector<std::string> &working_spaces) {

    // Shared by all GradingColourOperator instances and by the pre-draw hooks
    // (which run in the viewport drawing thread), hence the mutex.
    static std::mutex cache_mutex;
    static std::map<std::string, ColourOperationDataPtr> cache;

    std::string key = grading::colour_metadata_key(media_source_colour_metadata);
    for (const auto &space : working_spaces) {
        key += "\n" + space;
    }

    std::lock_guard<std::mutex> lock(cache_mutex);

    auto p = cache.find(key);
    if (p != cache.end()) {
        return p->second;
    }

    // Keep memory in check if many configs / combinations are used in a
    // long session. Data still referenced elsewhere stays alive.
    if (cache.size() > 256) {
        cache.clear();
    }

    auto data  = setup_shader_data(media_source_colour_metadata, working_spaces);
    cache[key] = data;
    return data;
}

std::shared_ptr<ColourOperationData> GradingColourOperator::setup_shader_data(
    const utility::JsonStore &media_source_colour_metadata,
    const std::vector<std::string> &working_spaces) {

    auto colour_op_data =
        std::make_shared<ColourOperationData>(ColourOperationData(PLUGIN_UUID, "Grade OP"));

    std::string fs_str                 = fragment_shader;
    std::vector<ColourLUTPtr> &fs_luts = colour_op_data->luts_;

    // Inject the conversions between the reference space and the working
    // spaces of the grade layers
    auto working_space_shader =
        grading::build_working_space_shader(media_source_colour_metadata, working_spaces);
    fs_str =
        utility::replace_once(fs_str, "//INJECT_WORKING_SPACES", working_space_shader.glsl);
    for (auto &desc : working_space_shader.shader_descs) {
        auto luts = setup_ocio_textures(desc);
        fs_luts.insert(fs_luts.end(), luts.begin(), luts.end());
    }

    colour_op_data->shader_ = std::make_shared<ui::opengl::OpenGLShader>(PLUGIN_UUID, fs_str);

    // Remember which working space each 'color_space' index refers to
    colour_op_data->user_data_ = working_spaces;

    // The shader text depends on the OCIO config, the context and the working
    // spaces, so it is part of the cache id (with the LUT contents).
    colour_op_data->set_cache_id(fmt::format("{}", std::hash<std::string>{}(fs_str)));
    for (const auto &lut : fs_luts) {
        colour_op_data->set_cache_id(
            colour_op_data->cache_id() + fmt::format("{}", lut->cache_id()));
    }
    return colour_op_data;
}

plugin::GPUPreDrawHookPtr
GradingColourOperator::make_pre_draw_gpu_hook(const std::string &viewport_name) {
    return std::make_shared<GradingMaskRenderer>(viewport_name);
}

std::vector<ColourLUTPtr>
GradingColourOperator::setup_ocio_textures(OCIO::ConstGpuShaderDescRcPtr &shader) {

    std::vector<ColourLUTPtr> luts;

    // Process 3D LUTs
    const unsigned max_texture_3D = shader->getNum3DTextures();
    for (unsigned idx = 0; idx < max_texture_3D; ++idx) {
        const char *textureName           = nullptr;
        const char *samplerName           = nullptr;
        unsigned edgelen                  = 0;
        OCIO::Interpolation interpolation = OCIO::INTERP_LINEAR;

        shader->get3DTexture(idx, textureName, samplerName, edgelen, interpolation);
        if (!textureName || !*textureName || !samplerName || !*samplerName || edgelen == 0) {
            throw std::runtime_error(
                "OCIO::ShaderDesc::get3DTexture - The texture data is corrupted");
        }

        const float *ocio_lut_data = nullptr;
        shader->get3DTextureValues(idx, ocio_lut_data);
        if (!ocio_lut_data) {
            throw std::runtime_error(
                "OCIO::ShaderDesc::get3DTextureValues - The texture values are missing");
        }

        auto xs_dtype    = LUTDescriptor::FLOAT32;
        auto xs_channels = LUTDescriptor::RGB;
        auto xs_interp   = interpolation == OCIO::INTERP_LINEAR ? LUTDescriptor::LINEAR
                                                                : LUTDescriptor::NEAREST;
        auto xs_lut      = std::make_shared<ColourLUT>(
            LUTDescriptor::Create3DLUT(edgelen, xs_dtype, xs_channels, xs_interp), samplerName);

        const int channels          = 3;
        const std::size_t data_size = edgelen * edgelen * edgelen * channels * sizeof(float);
        auto *xs_lut_data           = (float *)xs_lut->writeable_data();
        std::memcpy(xs_lut_data, ocio_lut_data, data_size);

        xs_lut->update_content_hash();
        luts.push_back(xs_lut);
    }

    // Process 1D LUTs
    const unsigned max_texture_2D = shader->getNumTextures();
    for (unsigned idx = 0; idx < max_texture_2D; ++idx) {
        const char *textureName                  = nullptr;
        const char *samplerName                  = nullptr;
        unsigned width                           = 0;
        unsigned height                          = 0;
        OCIO::GpuShaderDesc::TextureType channel = OCIO::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
        OCIO::Interpolation interpolation        = OCIO::INTERP_LINEAR;
        bool is2DTexture                         = false;

#if OCIO_VERSION_HEX >= 0x02030000
        OCIO::GpuShaderDesc::TextureDimensions dimensions = OCIO::GpuShaderDesc::TEXTURE_1D;
        shader->getTexture(
            idx, textureName, samplerName, width, height, channel, dimensions, interpolation);
        is2DTexture = dimensions == OCIO::GpuShaderDesc::TEXTURE_2D;
#else
        shader->getTexture(
            idx, textureName, samplerName, width, height, channel, interpolation);
        is2DTexture = height > 1;
#endif
        if (!textureName || !*textureName || !samplerName || !*samplerName || width == 0) {
            throw std::runtime_error(
                "OCIO::ShaderDesc::getTexture - The texture data is corrupted");
        }

        const float *ocio_lut_data = nullptr;
        shader->getTextureValues(idx, ocio_lut_data);
        if (!ocio_lut_data) {
            throw std::runtime_error(
                "OCIO::ShaderDesc::getTextureValues - The texture values are missing");
        }

        auto xs_dtype    = LUTDescriptor::FLOAT32;
        auto xs_channels = channel == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL
                               ? LUTDescriptor::RED
                               : LUTDescriptor::RGB;
        auto xs_interp   = interpolation == OCIO::INTERP_LINEAR ? LUTDescriptor::LINEAR
                                                                : LUTDescriptor::NEAREST;
        auto xs_lut      = std::make_shared<ColourLUT>(
            is2DTexture
                     ? LUTDescriptor::Create2DLUT(width, height, xs_dtype, xs_channels, xs_interp)
                     : LUTDescriptor::Create1DLUT(width, xs_dtype, xs_channels, xs_interp),
            samplerName);

        const int channels = channel == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL ? 1 : 3;
        const std::size_t data_size = width * height * channels * sizeof(float);
        auto *xs_lut_data           = (float *)xs_lut->writeable_data();
        std::memcpy(xs_lut_data, ocio_lut_data, data_size);

        xs_lut->update_content_hash();
        luts.push_back(xs_lut);
    }

    return luts;
}
