// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

#include <fmt/format.h>

#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/logging.hpp"

#include "grading_working_space.h"

using namespace xstudio;
using namespace xstudio::colour_pipeline;

namespace fs = std::filesystem;

namespace {

const std::string DEFAULT_REFERENCE_SPACE{"scene_linear"};

std::mutex s_config_mutex;
std::map<std::string, OCIO::ConstConfigRcPtr> s_config_cache;

OCIO::ConstConfigRcPtr load_config(const std::string &config_name) {

    if (config_name.empty() || config_name == "__raw__") {
        return OCIO::Config::CreateRaw();
    } else if (config_name == "__current__") {
        return OCIO::GetCurrentConfig();
    }

    // Same rules as the OCIO colour pipeline plugin: a file path, or the name
    // of a config built into OCIO (e.g. "ocio://default").
    const std::string path = utility::forward_remap_file_path(config_name);
#if OCIO_VERSION_HEX >= 0x02020100
    if (!fs::exists(path)) {
        return OCIO::Config::CreateFromBuiltinConfig(path.c_str());
    }
#endif
    return OCIO::Config::CreateFromFile(path.c_str());
}

// The GLSL code of one OCIO conversion, from 'src' to 'dst'. Falls back to
// "no conversion" when OCIO can't build the conversion.
OCIO::ConstGpuShaderDescRcPtr conversion_shader(
    const OCIO::ConstConfigRcPtr &config,
    const OCIO::ConstContextRcPtr &context,
    const std::string &function_name,
    const std::string &resource_prefix,
    const std::string &src,
    const std::string &dst) {

    auto desc = OCIO::GpuShaderDesc::CreateShaderDesc();
    desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
    desc->setFunctionName(function_name.c_str());
    desc->setResourcePrefix(resource_prefix.c_str());

    try {

        auto gpu =
            config->getProcessor(context, src.c_str(), dst.c_str())->getDefaultGPUProcessor();
        gpu->extractGpuShaderInfo(desc);

    } catch (const std::exception &e) {

        spdlog::warn(
            "Grading: can't convert from '{}' to '{}' ({}), this working space will be "
            "ignored.",
            src,
            dst,
            e.what());

        desc = OCIO::GpuShaderDesc::CreateShaderDesc();
        desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
        desc->setFunctionName(function_name.c_str());
        desc->setResourcePrefix(resource_prefix.c_str());

        auto raw = OCIO::Config::CreateRaw();
        raw->getProcessor("raw", "raw")->getDefaultGPUProcessor()->extractGpuShaderInfo(desc);
    }

    return desc;
}

} // anonymous namespace

namespace xstudio::colour_pipeline::grading {

std::string reference_working_space(const utility::JsonStore &colour_metadata) {

    if (colour_metadata.is_object() && colour_metadata.contains("working_space") &&
        colour_metadata["working_space"].is_string()) {
        const auto space = colour_metadata["working_space"].get<std::string>();
        if (!space.empty())
            return space;
    }
    return DEFAULT_REFERENCE_SPACE;
}

std::string colour_metadata_key(const utility::JsonStore &colour_metadata) {

    std::string key;
    if (colour_metadata.is_object()) {
        key += colour_metadata.get_or("ocio_config", std::string(""));
        key += "\n";
        if (colour_metadata.contains("ocio_context")) {
            key += colour_metadata["ocio_context"].dump();
        }
    }
    key += "\n";
    key += reference_working_space(colour_metadata);
    return key;
}

OCIO::ConstConfigRcPtr ocio_config(const utility::JsonStore &colour_metadata) {

    const std::string config_name = colour_metadata.is_object()
                                        ? colour_metadata.get_or("ocio_config", std::string(""))
                                        : std::string("");

    std::lock_guard<std::mutex> lock(s_config_mutex);

    auto p = s_config_cache.find(config_name);
    if (p != s_config_cache.end()) {
        return p->second;
    }

    OCIO::ConstConfigRcPtr config;
    try {
        config = load_config(config_name);
    } catch (const std::exception &e) {
        spdlog::warn(
            "Grading: failed to load OCIO config '{}' ({}), using the raw config.",
            config_name,
            e.what());
    }

    if (!config) {
        config = OCIO::Config::CreateRaw();
    }

    s_config_cache[config_name] = config;
    return config;
}

OCIO::ConstContextRcPtr
ocio_context(const OCIO::ConstConfigRcPtr &config, const utility::JsonStore &colour_metadata) {

    auto context = config->getCurrentContext()->createEditableCopy();

    if (colour_metadata.is_object() && colour_metadata.contains("ocio_context") &&
        colour_metadata["ocio_context"].is_object()) {
        for (const auto &item : colour_metadata["ocio_context"].items()) {
            if (item.value().is_string()) {
                context->setStringVar(
                    item.key().c_str(), item.value().get<std::string>().c_str());
            }
        }
    }

    return context;
}

std::vector<std::string> working_space_choices(const OCIO::ConstConfigRcPtr &config) {

    std::vector<std::string> result;
    if (!config) {
        return result;
    }

    std::set<std::string> added;
    auto add = [&](const std::string &name) {
        if (!name.empty() && added.insert(name).second) {
            result.push_back(name);
        }
    };

    // Roles first: they are the names most pipelines (and saved grades) use.
    for (const char *role :
         {OCIO::ROLE_SCENE_LINEAR, OCIO::ROLE_COMPOSITING_LOG, OCIO::ROLE_COLOR_TIMING}) {
        if (config->hasRole(role)) {
            add(role);
        }
    }

    // Then every active scene referred colour space. Display referred spaces
    // and 'data' spaces (normals, masks...) are not useful to grade in.
    const int count =
        config->getNumColorSpaces(OCIO::SEARCH_REFERENCE_SPACE_SCENE, OCIO::COLORSPACE_ACTIVE);
    for (int i = 0; i < count; ++i) {
        const char *name = config->getColorSpaceNameByIndex(
            OCIO::SEARCH_REFERENCE_SPACE_SCENE, OCIO::COLORSPACE_ACTIVE, i);
        if (!name || !*name) {
            continue;
        }
        auto cs = config->getColorSpace(name);
        if (cs && cs->isData()) {
            continue;
        }
        add(name);
    }

    return result;
}

const std::vector<std::string> &default_shader_working_spaces() {
    static const std::vector<std::string> defaults{OCIO::ROLE_COMPOSITING_LOG};
    return defaults;
}

std::vector<std::string>
shader_working_spaces(const std::vector<std::string> &requested, const std::string &reference) {

    std::vector<std::string> result = default_shader_working_spaces();

    std::set<std::string> extra;
    for (const auto &space : requested) {
        if (space.empty() || space == reference) {
            continue;
        }
        if (std::find(result.begin(), result.end(), space) == result.end()) {
            extra.insert(space);
        }
    }
    result.insert(result.end(), extra.begin(), extra.end());

    return result;
}

int working_space_index(
    const std::vector<std::string> &shader_spaces,
    const std::string &space,
    const std::string &reference) {

    if (space.empty() || space == reference) {
        return 0;
    }

    auto p = std::find(shader_spaces.begin(), shader_spaces.end(), space);
    if (p == shader_spaces.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(shader_spaces.begin(), p)) + 1;
}

WorkingSpaceShader build_working_space_shader(
    const utility::JsonStore &colour_metadata, const std::vector<std::string> &spaces) {

    WorkingSpaceShader result;

    auto config                 = ocio_config(colour_metadata);
    auto context                = ocio_context(config, colour_metadata);
    const std::string reference = reference_working_space(colour_metadata);

    std::string to_dispatch   = "vec4 grading_to_working_space(vec4 rgba, int space)\n{\n";
    std::string from_dispatch = "vec4 grading_from_working_space(vec4 rgba, int space)\n{\n";

    for (size_t i = 0; i < spaces.size(); ++i) {

        const int index           = static_cast<int>(i) + 1;
        const std::string to_fn   = fmt::format("GradingToWorkingSpace{}", index);
        const std::string from_fn = fmt::format("GradingFromWorkingSpace{}", index);

        auto to_desc = conversion_shader(
            config,
            context,
            to_fn,
            fmt::format("grading_ws{}_to_", index),
            reference,
            spaces[i]);
        auto from_desc = conversion_shader(
            config,
            context,
            from_fn,
            fmt::format("grading_ws{}_from_", index),
            spaces[i],
            reference);

        result.glsl += fmt::format("// Working space {}: {}\n", index, spaces[i]);
        result.glsl += to_desc->getShaderText();
        result.glsl += "\n";
        result.glsl += from_desc->getShaderText();
        result.glsl += "\n";

        to_dispatch += fmt::format("    if (space == {}) return {}(rgba);\n", index, to_fn);
        from_dispatch += fmt::format("    if (space == {}) return {}(rgba);\n", index, from_fn);

        result.shader_descs.push_back(to_desc);
        result.shader_descs.push_back(from_desc);
    }

    to_dispatch += "    return rgba;\n}\n";
    from_dispatch += "    return rgba;\n}\n";

    result.glsl += to_dispatch;
    result.glsl += from_dispatch;

    return result;
}

} // namespace xstudio::colour_pipeline::grading
