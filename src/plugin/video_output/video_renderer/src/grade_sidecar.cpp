// SPDX-License-Identifier: Apache-2.0

#include "grade_sidecar.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <fmt/format.h>
#include <OpenColorIO/OpenColorIO.h> //NOLINT

namespace OCIO = OCIO_NAMESPACE;

namespace xstudio::video_render_plugin_1_0::grade_sidecar {

namespace {

    double number_or(const nlohmann::json &j, const char *key, const double def) {
        if (j.is_object() && j.contains(key) && j[key].is_number())
            return j[key].get<double>();
        return def;
    }

    std::array<double, 4>
    array4_or(const nlohmann::json &j, const char *key, const std::array<double, 4> &def) {
        std::array<double, 4> result = def;
        if (j.is_object() && j.contains(key) && j[key].is_array()) {
            const auto &a = j[key];
            for (size_t i = 0; i < 4 && i < a.size(); ++i) {
                if (a[i].is_number())
                    result[i] = a[i].get<double>();
            }
        }
        return result;
    }

    // optional [R, G, B] value: exactly three numbers, otherwise the default
    std::array<double, 3>
    array3_or(const nlohmann::json &j, const char *key, const std::array<double, 3> &def) {
        if (!j.is_object() || !j.contains(key) || !j[key].is_array() || j[key].size() != 3)
            return def;
        std::array<double, 3> result;
        for (size_t i = 0; i < 3; ++i) {
            if (!j[key][i].is_number())
                return def;
            result[i] = j[key][i].get<double>();
        }
        return result;
    }

    bool canvas_has_shapes(const nlohmann::json &canvas) {
        if (!canvas.is_object())
            return false;
        for (const char *key : {"pen_strokes", "captions", "quads", "polygons", "ellipses"}) {
            if (canvas.contains(key) && canvas[key].is_array() && !canvas[key].empty())
                return true;
        }
        return false;
    }

    const nlohmann::json &base_of(const nlohmann::json &bookmark_actor_json) {
        static const nlohmann::json empty = nlohmann::json::object();
        if (bookmark_actor_json.is_object() && bookmark_actor_json.contains("base") &&
            bookmark_actor_json["base"].is_object())
            return bookmark_actor_json["base"];
        return empty;
    }

    int64_t created_of(const nlohmann::json &base) {
        if (base.contains("created") && base["created"].is_object()) {
            const auto &c = base["created"];
            if (c.contains("since_epoch") && c["since_epoch"].is_number_integer()) {
                int64_t v = c["since_epoch"].get<int64_t>();
                if (c.value("unit", std::string("microseconds")) == "milliseconds")
                    v *= 1000;
                return v;
            }
        }
        return std::numeric_limits<int64_t>::max();
    }

    std::string owner_of(const nlohmann::json &base) {
        if (base.contains("owner") && base["owner"].is_string())
            return base["owner"].get<std::string>();
        return std::string();
    }

    std::string xml_escape(const std::string &s) {
        std::string r;
        for (const char c : s) {
            switch (c) {
            case '&':
                r += "&amp;";
                break;
            case '<':
                r += "&lt;";
                break;
            case '>':
                r += "&gt;";
                break;
            case '"':
                r += "&quot;";
                break;
            case '\'':
                r += "&apos;";
                break;
            default:
                r += c;
            }
        }
        return r;
    }

    std::string id_safe(const std::string &s) {
        std::string r;
        for (const char c : s) {
            if (std::isspace(static_cast<unsigned char>(c)))
                r += '_';
            else
                r += c;
        }
        return r;
    }

    // GLSL style helpers
    inline float glsl_sign(const float v) {
        return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
    }

} // namespace

bool GradeLayer::is_identity() const {
    return slope == std::array<double, 4>{1.0, 1.0, 1.0, 1.0} &&
           offset == std::array<double, 4>{0.0, 0.0, 0.0, 0.0} &&
           power == std::array<double, 4>{1.0, 1.0, 1.0, 1.0} && sat == 1.0 &&
           exposure == 0.0 && contrast == 1.0 &&
           exposure_rgb == std::array<double, 3>{0.0, 0.0, 0.0} &&
           contrast_rgb == std::array<double, 3>{1.0, 1.0, 1.0} &&
           sat_rgb == std::array<double, 3>{1.0, 1.0, 1.0};
}

std::array<double, 3> GradeLayer::effective_exposure() const {
    return {exposure + exposure_rgb[0], exposure + exposure_rgb[1], exposure + exposure_rgb[2]};
}

std::array<double, 3> GradeLayer::effective_contrast() const {
    return {contrast * contrast_rgb[0], contrast * contrast_rgb[1], contrast * contrast_rgb[2]};
}

std::array<double, 3> GradeLayer::effective_sat() const {
    return {sat * sat_rgb[0], sat * sat_rgb[1], sat * sat_rgb[2]};
}

bool is_grading_bookmark(const nlohmann::json &bookmark_actor_json) {
    const auto &base = base_of(bookmark_actor_json);
    return base.contains("user_type") && base["user_type"].is_string() &&
           base["user_type"].get<std::string>() == "Grading";
}

size_t count_grading_bookmarks(const nlohmann::json &bookmarks_serialisation) {
    size_t count = 0;
    if (bookmarks_serialisation.is_object() && bookmarks_serialisation.contains("actors") &&
        bookmarks_serialisation["actors"].is_object()) {
        for (const auto &a : bookmarks_serialisation["actors"]) {
            if (is_grading_bookmark(a))
                count++;
        }
    }
    return count;
}

std::vector<GradeLayer> grade_layers_for_media(
    const nlohmann::json &bookmarks_serialisation, const std::string &media_uuid) {

    std::vector<std::pair<int64_t, GradeLayer>> found;

    if (!bookmarks_serialisation.is_object() || !bookmarks_serialisation.contains("actors") ||
        !bookmarks_serialisation["actors"].is_object())
        return {};

    for (const auto &actor : bookmarks_serialisation["actors"]) {

        if (!is_grading_bookmark(actor))
            continue;
        const auto &base = base_of(actor);
        if (owner_of(base) != media_uuid)
            continue;
        if (!base.contains("annotation") || !base["annotation"].is_object())
            continue;
        const auto &anno = base["annotation"];
        if (!anno.contains("Data") || !anno["Data"].is_object())
            continue;
        const auto &data = anno["Data"];
        const nlohmann::json grade =
            data.contains("grade") ? data["grade"] : nlohmann::json::object();

        GradeLayer layer;
        if (data.contains("colour_space") && data["colour_space"].is_string())
            layer.colour_space = data["colour_space"].get<std::string>();
        layer.slope    = array4_or(grade, "slope", layer.slope);
        layer.offset   = array4_or(grade, "offset", layer.offset);
        layer.power    = array4_or(grade, "power", layer.power);
        layer.sat      = number_or(grade, "sat", layer.sat);
        layer.exposure = number_or(grade, "exposure", layer.exposure);
        layer.contrast = number_or(grade, "contrast", layer.contrast);
        // optional per channel trims (newer grades)
        layer.exposure_rgb = array3_or(grade, "exposure_rgb", layer.exposure_rgb);
        layer.contrast_rgb = array3_or(grade, "contrast_rgb", layer.contrast_rgb);
        layer.sat_rgb      = array3_or(grade, "sat_rgb", layer.sat_rgb);
        layer.masked       = data.contains("mask") && canvas_has_shapes(data["mask"]);
        if (data.contains("layer_name") && data["layer_name"].is_string())
            layer.name = data["layer_name"].get<std::string>();

        const nlohmann::json user_data =
            base.contains("user_data") && base["user_data"].is_object()
                ? base["user_data"]
                : nlohmann::json::object();
        if (user_data.contains("grade_active") && user_data["grade_active"].is_boolean())
            layer.active = user_data["grade_active"].get<bool>();
        if (layer.name.empty() && user_data.contains("layer_name") &&
            user_data["layer_name"].is_string())
            layer.name = user_data["layer_name"].get<std::string>();

        // A grade that only covers part of the clip has a finite duration.
        // Full clip grades use the biggest possible duration.
        if (base.contains("duration") && base["duration"].is_object() &&
            base["duration"].contains("count") && base["duration"]["count"].is_number()) {
            const auto d = base["duration"]["count"].get<double>();
            layer.full_range =
                d >= static_cast<double>(std::numeric_limits<int64_t>::max()) * 0.5;
        }

        found.emplace_back(created_of(base), layer);
    }

    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    std::vector<GradeLayer> result;
    for (auto &f : found) {
        if (f.second.name.empty())
            f.second.name = fmt::format("layer{}", result.size() + 1);
        result.push_back(f.second);
    }
    return result;
}

std::string
clip_name_from_colour_params(const nlohmann::json &colour_params, const std::string &fallback) {

    if (colour_params.is_object()) {
        if (colour_params.contains("ocio_context") &&
            colour_params["ocio_context"].is_object()) {
            const auto &ctx = colour_params["ocio_context"];
            if (ctx.contains("SHOT") && ctx["SHOT"].is_string() &&
                !ctx["SHOT"].get<std::string>().empty())
                return ctx["SHOT"].get<std::string>();
        }
        if (colour_params.contains("path") && colour_params["path"].is_string()) {
            std::string p = colour_params["path"].get<std::string>();
            // strip uri scheme if there is one
            const auto scheme = p.find("://");
            if (scheme != std::string::npos)
                p = p.substr(scheme + 3);
            auto stem = std::filesystem::path(p).stem().string();
            // frame sequences: "shot.####", "shot.%04d", "shot.{:04d}" or
            // "shot.1001" (first frame) -> "shot"
            static const std::regex frame_part(
                R"(^(.+?)(?:[._](?:#+|@+|%0?\d*d|\{:0?\d*d\})|\.\d+)$)");
            std::smatch m;
            if (std::regex_match(stem, m, frame_part))
                stem = m[1].str();
            if (!stem.empty())
                return stem;
        }
    }
    return fallback;
}

std::array<float, 3>
apply_grade_maths(const std::array<float, 3> &in, const GradeLayer &layer) {

    std::array<float, 3> rgb = in;

    const auto exposure = layer.effective_exposure();
    if (exposure != std::array<double, 3>{0.0, 0.0, 0.0}) {
        for (int i = 0; i < 3; ++i)
            rgb[i] *= static_cast<float>(std::pow(2.0, exposure[i]));
    }

    const auto contrast = layer.effective_contrast();
    if (contrast != std::array<double, 3>{1.0, 1.0, 1.0}) {
        for (int i = 0; i < 3; ++i)
            rgb[i] = std::pow(std::abs(rgb[i] / 0.18f), static_cast<float>(contrast[i])) *
                     glsl_sign(rgb[i]) * 0.18f;
    }

    std::array<float, 3> slope, offset, power;
    for (int i = 0; i < 3; ++i) {
        slope[i]  = static_cast<float>(layer.slope[i] * layer.slope[3]);
        offset[i] = static_cast<float>(layer.offset[i] + layer.offset[3]);
        power[i]  = static_cast<float>(layer.power[i] * layer.power[3]);
    }

    const bool unit_power = power == std::array<float, 3>{1.0f, 1.0f, 1.0f};
    if (slope != std::array<float, 3>{1.0f, 1.0f, 1.0f} ||
        offset != std::array<float, 3>{0.0f, 0.0f, 0.0f} || !unit_power) {
        for (int i = 0; i < 3; ++i) {
            rgb[i] = rgb[i] * slope[i] + offset[i];
            if (!unit_power)
                rgb[i] = std::pow(std::abs(rgb[i]), power[i]) * glsl_sign(rgb[i]);
        }
    }

    const auto sat = layer.effective_sat();
    if (sat != std::array<double, 3>{1.0, 1.0, 1.0}) {
        const float luma =
            rgb[0] * 0.212599993f + rgb[1] * 0.715200007f + rgb[2] * 0.0722000003f;
        for (int i = 0; i < 3; ++i)
            rgb[i] = luma + static_cast<float>(sat[i]) * (rgb[i] - luma);
    }

    return rgb;
}

CDLValues layer_to_cdl(const GradeLayer &layer) {
    CDLValues r;
    const auto exposure = layer.effective_exposure();
    for (int i = 0; i < 3; ++i) {
        r.slope[i]  = layer.slope[i] * layer.slope[3] * std::pow(2.0, exposure[i]);
        r.offset[i] = layer.offset[i] + layer.offset[3];
        r.power[i]  = layer.power[i] * layer.power[3];
    }
    // a CDL has one saturation value and no contrast
    const auto sat     = layer.effective_sat();
    const bool one_sat = sat[0] == sat[1] && sat[1] == sat[2];
    r.sat              = one_sat ? sat[0] : layer.sat;
    r.exact = one_sat && layer.effective_contrast() == std::array<double, 3>{1.0, 1.0, 1.0};
    return r;
}

std::string make_ccc(const std::vector<ClipGrade> &clips, std::vector<std::string> &warnings) {

    std::ostringstream body;
    int count = 0;

    for (const auto &clip : clips) {

        std::vector<const GradeLayer *> layers;
        for (const auto &l : clip.layers) {
            if (l.active)
                layers.push_back(&l);
        }

        for (size_t i = 0; i < layers.size(); ++i) {

            const auto &layer = *layers[i];
            const auto cdl    = layer_to_cdl(layer);
            const std::string id =
                layers.size() == 1 ? clip.clip_name : clip.clip_name + "_" + layer.name;

            std::vector<std::string> notes;
            if (layers.size() > 1)
                notes.push_back(fmt::format(
                    "Layer {} of {} for {} - apply in this order",
                    i + 1,
                    layers.size(),
                    clip.clip_name));
            notes.push_back(fmt::format(
                "Working colour space: {}",
                layer.colour_space.empty() ? "scene_linear" : layer.colour_space));

            if (!cdl.exact) {
                notes.push_back("Contrast and per channel saturation are not part of this CDL");
                warnings.push_back(fmt::format(
                    "{}: layer \"{}\" uses contrast or per channel saturation, which a CDL "
                    "can't hold. The .cube LUT includes them.",
                    clip.clip_name,
                    layer.name));
            }
            if (layer.masked) {
                notes.push_back("This layer has a mask, which a CDL can't hold");
                warnings.push_back(fmt::format(
                    "{}: layer \"{}\" has a mask. The CDL applies it to the whole frame.",
                    clip.clip_name,
                    layer.name));
            }
            if (!layer.full_range) {
                notes.push_back("This layer only covers part of the clip");
                warnings.push_back(fmt::format(
                    "{}: layer \"{}\" only covers part of the clip.",
                    clip.clip_name,
                    layer.name));
            }

            body << fmt::format("    <ColorCorrection id=\"{}\">\n", xml_escape(id_safe(id)));
            for (const auto &n : notes)
                body << fmt::format("        <Description>{}</Description>\n", xml_escape(n));
            body << "        <SOPNode>\n";
            body << fmt::format(
                "            <Slope>{:.6f} {:.6f} {:.6f}</Slope>\n",
                cdl.slope[0],
                cdl.slope[1],
                cdl.slope[2]);
            body << fmt::format(
                "            <Offset>{:.6f} {:.6f} {:.6f}</Offset>\n",
                cdl.offset[0],
                cdl.offset[1],
                cdl.offset[2]);
            body << fmt::format(
                "            <Power>{:.6f} {:.6f} {:.6f}</Power>\n",
                cdl.power[0],
                cdl.power[1],
                cdl.power[2]);
            body << "        </SOPNode>\n";
            body << "        <SatNode>\n";
            body << fmt::format("            <Saturation>{:.6f}</Saturation>\n", cdl.sat);
            body << "        </SatNode>\n";
            body << "    </ColorCorrection>\n";
            count++;
        }
    }

    if (!count)
        return std::string();

    std::ostringstream result;
    result << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    result << "<ColorCorrectionCollection xmlns=\"urn:ASC:CDL:v1.01\">\n";
    result << "    <Description>Grades exported by xSTUDIO</Description>\n";
    result << body.str();
    result << "</ColorCorrectionCollection>\n";
    return result.str();
}

std::string
bake_cube(const ClipGrade &clip, const int cube_size, std::vector<std::string> &warnings) {

    std::vector<const GradeLayer *> layers;
    for (const auto &l : clip.layers) {
        if (!l.active)
            continue;
        if (l.masked) {
            warnings.push_back(fmt::format(
                "{}: layer \"{}\" has a mask and is left out of the .cube LUT.",
                clip.clip_name,
                l.name));
            continue;
        }
        if (!l.full_range) {
            warnings.push_back(fmt::format(
                "{}: layer \"{}\" only covers part of the clip, the .cube LUT applies it to "
                "the whole clip.",
                clip.clip_name,
                l.name));
        }
        layers.push_back(&l);
    }
    if (layers.empty())
        return std::string();

    const int size = std::clamp(cube_size, 2, 129);

    // Colour space conversions, set up like the grading colour op does it:
    // pixels arrive in the media working space (the reference), each layer
    // is applied in its own working space. The LUT itself is encoded in
    // compositing_log.
    static const std::string lut_space = "compositing_log";
    std::string reference              = "scene_linear";
    if (clip.colour_params.is_object() && clip.colour_params.contains("working_space") &&
        clip.colour_params["working_space"].is_string() &&
        !clip.colour_params["working_space"].get<std::string>().empty())
        reference = clip.colour_params["working_space"].get<std::string>();

    const std::string config_path = clip.colour_params.is_object() &&
                                            clip.colour_params.contains("ocio_config") &&
                                            clip.colour_params["ocio_config"].is_string()
                                        ? clip.colour_params["ocio_config"].get<std::string>()
                                        : std::string();

    OCIO::ConstConfigRcPtr config;
    OCIO::ContextRcPtr context;
    try {
        if (config_path.empty())
            throw std::runtime_error("no OCIO config for this media");
        config  = OCIO::Config::CreateFromFile(config_path.c_str());
        context = config->getCurrentContext()->createEditableCopy();
        if (clip.colour_params.contains("ocio_context") &&
            clip.colour_params["ocio_context"].is_object()) {
            for (const auto &item : clip.colour_params["ocio_context"].items()) {
                if (item.value().is_string())
                    context->setStringVar(
                        item.key().c_str(), item.value().get<std::string>().c_str());
            }
        }
    } catch (const std::exception &e) {
        config.reset();
        warnings.push_back(fmt::format(
            "{}: could not load the OCIO config for the LUT ({}). The LUT works on the pixel "
            "values directly.",
            clip.clip_name,
            e.what()));
    }

    // CPU processors, made once per pair of colour spaces. Null means "no
    // change" (same space, or a space the config doesn't have).
    std::map<std::pair<std::string, std::string>, OCIO::ConstCPUProcessorRcPtr> processors;
    std::set<std::string> failed;
    auto processor = [&](const std::string &src,
                         const std::string &dst) -> OCIO::ConstCPUProcessorRcPtr {
        if (!config || src == dst)
            return OCIO::ConstCPUProcessorRcPtr();
        const auto key = std::make_pair(src, dst);
        auto p         = processors.find(key);
        if (p != processors.end())
            return p->second;
        OCIO::ConstCPUProcessorRcPtr proc;
        try {
            proc = config->getProcessor(context, src.c_str(), dst.c_str())
                       ->getDefaultCPUProcessor();
        } catch (const std::exception &e) {
            const auto missing = src == reference ? dst : src;
            if (failed.insert(missing).second)
                warnings.push_back(fmt::format(
                    "{}: the OCIO config has no colour space \"{}\" ({}), so the LUT uses the "
                    "pixel values as they are instead.",
                    clip.clip_name,
                    missing,
                    e.what()));
        }
        processors[key] = proc;
        return proc;
    };
    auto convert =
        [&](std::array<float, 3> &v, const std::string &src, const std::string &dst) {
            auto proc = processor(src, dst);
            if (proc)
                proc->applyRGB(v.data());
        };

    std::string space_note = "the pixel values as they are (no OCIO config)";
    if (config) {
        if (processor(lut_space, reference)) {
            auto cs    = config->getColorSpace(lut_space.c_str());
            space_note = fmt::format(
                "{} (OCIO role compositing_log)", cs ? cs->getName() : lut_space.c_str());
        } else {
            space_note = "the pixel values as they are (no compositing_log role in the OCIO "
                         "config)";
        }
    }

    std::ostringstream out;
    out << fmt::format("TITLE \"xSTUDIO grade {}\"\n", clip.clip_name);
    out << "# Made by xSTUDIO from the grade layers of this clip.\n";
    out << fmt::format("# Input and output: {}\n", space_note);
    if (!config_path.empty())
        out << fmt::format("# OCIO config: {}\n", config_path);
    out << fmt::format("LUT_3D_SIZE {}\n", size);

    const float scale = 1.0f / static_cast<float>(size - 1);
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {

                std::array<float, 3> v{r * scale, g * scale, b * scale};

                // LUT input is log encoded, grades start in the reference
                convert(v, lut_space, reference);
                std::string current = reference;

                for (const auto *layer : layers) {
                    const std::string space =
                        layer->colour_space.empty() ? reference : layer->colour_space;
                    if (space != current) {
                        // like the shader, always go through the reference
                        convert(v, current, reference);
                        convert(v, reference, space);
                        current = space;
                    }
                    v = apply_grade_maths(v, *layer);
                }
                convert(v, current, reference);
                convert(v, reference, lut_space);
                out << fmt::format("{:.6f} {:.6f} {:.6f}\n", v[0], v[1], v[2]);
            }
        }
    }
    return out.str();
}

} // namespace xstudio::video_render_plugin_1_0::grade_sidecar
