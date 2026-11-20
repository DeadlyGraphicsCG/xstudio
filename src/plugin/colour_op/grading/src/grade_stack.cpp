// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>
#include <OpenColorIO/OpenColorIO.h> //NOLINT

#include "grade_stack.h"
#include "grading_data.h"

namespace OCIO = OCIO_NAMESPACE;
namespace fs   = std::filesystem;

using namespace xstudio;
using namespace xstudio::ui::viewport;
using namespace xstudio::ui::viewport::grade_stack;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

template <typename T>
T get_json_or(const nlohmann::json &j, const char *key, const T &fallback) {
    if (!j.is_object() || !j.contains(key) || j.at(key).is_null())
        return fallback;
    try {
        return j.at(key).get<T>();
    } catch (const std::exception &) {
        return fallback;
    }
}

bool valid_uuid_string(const std::string &s) {
    return !s.empty() && !utility::Uuid(s).is_null();
}

std::string new_id() { return utility::to_string(utility::Uuid::generate()); }

std::string id_from_name(const std::string &name) {
    return utility::to_string(utility::Uuid::generate_from_name(name.c_str()));
}

// Checks that the layer's grading data can be loaded by this xSTUDIO.
void check_grading_data(const nlohmann::json &grading_data, size_t layer_index) {
    if (!grading_data.is_object()) {
        throw std::runtime_error(fmt::format("Layer {} has no grade data.", layer_index + 1));
    }
    try {
        GradingData test(utility::JsonStore{grading_data});
        (void)test;
    } catch (const std::exception &e) {
        throw std::runtime_error(fmt::format(
            "Layer {} has grade data this xSTUDIO can't read ({}).",
            layer_index + 1,
            e.what()));
    }
}

std::string cdl_id_for(const std::string &name, std::set<std::string> &used) {
    std::string id;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')
            id += c;
        else if (!id.empty() && id.back() != '_')
            id += '_';
    }
    while (!id.empty() && id.back() == '_')
        id.pop_back();
    if (id.empty())
        id = "layer";
    std::string unique = id;
    int n              = 2;
    while (used.count(unique))
        unique = fmt::format("{}_{}", id, n++);
    used.insert(unique);
    return unique;
}

std::string fmt_num(double v) { return fmt::format("{:.6g}", v); }

} // namespace

std::string grade_stack::to_string(StackPosition p) {
    return p == StackPosition::Above ? "above" : "below";
}

StackPosition grade_stack::position_from_string(const std::string &s, StackPosition fallback) {
    const auto l = lower(s);
    if (l == "below")
        return StackPosition::Below;
    if (l == "above")
        return StackPosition::Above;
    return fallback;
}

utility::JsonStore grade_stack::profile_to_json(const Profile &profile) {

    nlohmann::json layers = nlohmann::json::array();
    for (const auto &layer : profile.layers) {
        nlohmann::json l;
        l["layer_id"]     = layer.layer_id;
        l["name"]         = layer.name;
        l["enabled"]      = layer.enabled;
        l["grading_data"] = layer.grading_data;
        l["user_data"]    = layer.user_data.is_object() ? nlohmann::json(layer.user_data)
                                                        : nlohmann::json::object();
        layers.push_back(l);
    }

    utility::JsonStore j;
    j["schema"]      = PROFILE_SCHEMA;
    j["version"]     = PROFILE_VERSION;
    j["id"]          = profile.id;
    j["name"]        = profile.name;
    j["description"] = profile.description;
    j["position"]    = to_string(profile.position);
    j["layers"]      = layers;
    return j;
}

Profile grade_stack::profile_from_json(const nlohmann::json &data) {

    if (!data.is_object() || get_json_or<std::string>(data, "schema", "") != PROFILE_SCHEMA) {
        throw std::runtime_error("This file is not an xSTUDIO colour profile.");
    }

    const int version = get_json_or<int>(data, "version", 1);
    if (version > PROFILE_VERSION) {
        throw std::runtime_error(fmt::format(
            "This colour profile was made by a newer xSTUDIO (profile version {}, this "
            "xSTUDIO reads up to version {}).",
            version,
            PROFILE_VERSION));
    }

    Profile p;
    p.name = get_json_or<std::string>(data, "name", "");
    if (p.name.empty())
        p.name = "Unnamed profile";
    p.description = get_json_or<std::string>(data, "description", "");
    p.position    = position_from_string(
        get_json_or<std::string>(data, "position", ""), StackPosition::Below);

    p.id = get_json_or<std::string>(data, "id", "");
    if (!valid_uuid_string(p.id)) {
        // Hand written files may skip the id. Derive it from the name so that
        // importing the same file again still syncs the same layers.
        p.id = id_from_name("xstudio.grade_stack." + p.name);
    }

    if (!data.contains("layers") || !data.at("layers").is_array()) {
        throw std::runtime_error("This colour profile has no list of layers.");
    }

    std::set<std::string> seen_ids;
    const auto &layers = data.at("layers");
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto &l = layers.at(i);
        if (!l.is_object()) {
            throw std::runtime_error(fmt::format("Layer {} is not valid.", i + 1));
        }

        ProfileLayer layer;
        layer.name = get_json_or<std::string>(l, "name", "");
        if (layer.name.empty())
            layer.name = fmt::format("Layer {}", i + 1);
        layer.enabled = get_json_or<bool>(l, "enabled", true);

        layer.layer_id = get_json_or<std::string>(l, "layer_id", "");
        if (!valid_uuid_string(layer.layer_id) || seen_ids.count(layer.layer_id)) {
            layer.layer_id = id_from_name(fmt::format("{}.layer.{}", p.id, i));
            if (seen_ids.count(layer.layer_id))
                layer.layer_id = new_id();
        }
        seen_ids.insert(layer.layer_id);

        const nlohmann::json grading_data =
            l.contains("grading_data") ? l.at("grading_data") : nlohmann::json();
        check_grading_data(grading_data, i);
        layer.grading_data = utility::JsonStore(grading_data);

        if (l.contains("user_data") && l.at("user_data").is_object())
            layer.user_data = utility::JsonStore(l.at("user_data"));
        else
            layer.user_data = utility::JsonStore(nlohmann::json::object());

        p.layers.push_back(layer);
    }

    return p;
}

std::string grade_stack::profile_file_name(const std::string &profile_name) {

    std::string stem;
    for (const char c : profile_name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
            stem += c;
        else if (!stem.empty() && stem.back() != '_')
            stem += '_';
    }
    while (!stem.empty() && stem.back() == '_')
        stem.pop_back();
    if (stem.empty())
        stem = "profile";
    return stem + PROFILE_SUFFIX;
}

Profile grade_stack::load_profile_file(const std::string &path) {

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error(fmt::format("Can't open {}", path));
    }
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(ifs);
    } catch (const std::exception &e) {
        throw std::runtime_error(fmt::format("{} is not valid JSON ({})", path, e.what()));
    }
    return profile_from_json(data);
}

void grade_stack::save_profile_file(const Profile &profile, const std::string &path) {

    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }

    // Write next to the target first so a failed write never leaves a
    // half-written profile behind.
    const fs::path tmp = fs::path(path + ".tmp");
    {
        std::ofstream ofs(tmp);
        if (!ofs.is_open()) {
            throw std::runtime_error(fmt::format("Can't write {}", path));
        }
        ofs << profile_to_json(profile).dump(2) << "\n";
        if (!ofs.good()) {
            throw std::runtime_error(fmt::format("Can't write {}", path));
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error(fmt::format("Can't write {}", path));
    }
}

std::optional<StackTag> grade_stack::stack_tag(const utility::JsonStore &user_data) {

    if (!user_data.is_object() || !user_data.contains(STACK_TAG_KEY))
        return {};
    const auto &t = user_data.at(STACK_TAG_KEY);
    if (!t.is_object())
        return {};

    StackTag tag;
    tag.profile_id = get_json_or<std::string>(t, "profile_id", "");
    if (tag.profile_id.empty())
        return {};
    tag.profile_name    = get_json_or<std::string>(t, "profile_name", "");
    tag.layer_id        = get_json_or<std::string>(t, "layer_id", "");
    tag.layer_name      = get_json_or<std::string>(t, "layer_name", "");
    tag.position        = position_from_string(get_json_or<std::string>(t, "position", ""));
    tag.profile_enabled = get_json_or<bool>(t, "profile_enabled", true);
    return tag;
}

std::optional<StackTag>
grade_stack::stack_tag(const std::optional<utility::JsonStore> &user_data) {
    if (!user_data)
        return {};
    return stack_tag(*user_data);
}

std::string
grade_stack::stack_layer_display_name(const Profile &profile, const ProfileLayer &layer) {
    return fmt::format("[{}] {}", profile.name, layer.name);
}

utility::JsonStore grade_stack::make_stack_user_data(
    const Profile &profile,
    const ProfileLayer &layer,
    StackPosition position,
    const std::optional<utility::JsonStore> &existing_user_data) {

    utility::JsonStore ud(
        layer.user_data.is_object() ? nlohmann::json(layer.user_data)
                                    : nlohmann::json::object());

    // On/off: follow the profile, unless this shot already had the layer and
    // the profile's setting has not changed since - then keep the shot's
    // choice (someone may have switched it off for just this shot).
    bool grade_active = layer.enabled;
    if (existing_user_data && existing_user_data->is_object()) {
        const auto old_tag = stack_tag(*existing_user_data);
        if (old_tag && old_tag->profile_enabled == layer.enabled) {
            grade_active =
                get_json_or<bool>(*existing_user_data, "grade_active", layer.enabled);
        }
    }

    ud["grade_active"] = grade_active;
    if (!ud.contains("mask_active"))
        ud["mask_active"] = false;
    ud["layer_name"] = stack_layer_display_name(profile, layer);

    nlohmann::json tag;
    tag["profile_id"]      = profile.id;
    tag["profile_name"]    = profile.name;
    tag["layer_id"]        = layer.layer_id;
    tag["layer_name"]      = layer.name;
    tag["position"]        = to_string(position);
    tag["profile_enabled"] = layer.enabled;
    ud[STACK_TAG_KEY]      = tag;

    return ud;
}

ProfileLayer grade_stack::layer_from_bookmark(
    const utility::JsonStore &grading_data_json,
    const std::optional<utility::JsonStore> &user_data,
    const std::string &profile_id,
    int layer_index) {

    ProfileLayer layer;
    layer.grading_data = grading_data_json;

    nlohmann::json ud = (user_data && user_data->is_object()) ? nlohmann::json(*user_data)
                                                              : nlohmann::json::object();

    const auto tag = user_data ? stack_tag(*user_data) : std::optional<StackTag>();

    layer.enabled = get_json_or<bool>(ud, "grade_active", true);
    if (tag && !tag->layer_name.empty()) {
        layer.name = tag->layer_name;
    } else {
        layer.name = get_json_or<std::string>(ud, "layer_name", "");
    }
    if (layer.name.empty())
        layer.name = fmt::format("Grade Layer {}", layer_index + 1);

    if (tag && tag->profile_id == profile_id && valid_uuid_string(tag->layer_id)) {
        layer.layer_id = tag->layer_id;
    } else {
        layer.layer_id = new_id();
    }

    ud.erase(STACK_TAG_KEY);
    ud.erase("grade_active");
    ud.erase("layer_name");
    layer.user_data = utility::JsonStore(ud);

    return layer;
}

int grade_stack::order_group(const std::optional<utility::JsonStore> &user_data) {
    const auto tag = stack_tag(user_data);
    if (!tag)
        return 1;
    return tag->position == StackPosition::Below ? 0 : 2;
}

bool grade_stack::layer_less(
    const bookmark::BookmarkDetail &a, const bookmark::BookmarkDetail &b) {

    const int ga = order_group(a.user_data_);
    const int gb = order_group(b.user_data_);
    if (ga != gb)
        return ga < gb;

    const auto ca = a.created_.value_or(utility::sys_time_point::min());
    const auto cb = b.created_.value_or(utility::sys_time_point::min());
    return ca < cb;
}

ApplyPlan
grade_stack::plan_apply(const Profile &profile, const std::vector<ExistingLayer> &existing) {

    ApplyPlan plan;

    // layer_id -> bookmark already on the shot for this profile
    std::map<std::string, utility::Uuid> on_shot;
    for (const auto &layer : existing) {
        const auto tag = stack_tag(layer.user_data);
        if (!tag || tag->profile_id != profile.id)
            continue;
        if (tag->layer_id.empty() || on_shot.count(tag->layer_id)) {
            // broken tag or a duplicate (e.g. pasted twice) - tidy it up
            plan.remove.push_back(layer.bookmark);
            continue;
        }
        on_shot[tag->layer_id] = layer.bookmark;
    }

    for (size_t i = 0; i < profile.layers.size(); ++i) {
        auto it = on_shot.find(profile.layers[i].layer_id);
        if (it != on_shot.end()) {
            plan.update.emplace_back(it->second, i);
            on_shot.erase(it);
        } else {
            plan.create.push_back(i);
        }
    }

    // left over = layers that were removed from the profile
    for (const auto &layer : existing) {
        const auto tag = stack_tag(layer.user_data);
        if (tag && tag->profile_id == profile.id && on_shot.count(tag->layer_id) &&
            on_shot[tag->layer_id] == layer.bookmark) {
            plan.remove.push_back(layer.bookmark);
        }
    }

    return plan;
}

std::vector<utility::Uuid> grade_stack::layers_of_profile(
    const std::string &profile_id, const std::vector<ExistingLayer> &existing) {

    std::vector<utility::Uuid> result;
    for (const auto &layer : existing) {
        const auto tag = stack_tag(layer.user_data);
        if (tag && tag->profile_id == profile_id)
            result.push_back(layer.bookmark);
    }
    return result;
}

std::vector<utility::sys_time_point> grade_stack::stack_created_times(
    StackPosition position,
    const std::vector<ExistingLayer> &others,
    size_t count,
    utility::sys_time_point now) {

    using std::chrono::milliseconds;
    std::vector<utility::sys_time_point> times;
    times.reserve(count);

    if (position == StackPosition::Below) {
        auto base = now;
        for (const auto &o : others) {
            if (o.created && *o.created < base)
                base = *o.created;
        }
        for (size_t i = 0; i < count; ++i) {
            times.push_back(base - milliseconds(static_cast<long>(count - i)));
        }
    } else {
        auto base = now;
        for (const auto &o : others) {
            if (o.created && *o.created >= base)
                base = *o.created + milliseconds(1);
        }
        for (size_t i = 0; i < count; ++i) {
            times.push_back(base + milliseconds(static_cast<long>(i)));
        }
    }

    // Layer list sorting works in whole milliseconds; keep times on that grid
    for (auto &t : times) {
        t = std::chrono::time_point_cast<milliseconds>(t);
    }
    return times;
}

std::vector<std::string>
grade_stack::write_profile_cdl(const Profile &profile, const std::string &path) {

    std::vector<std::string> notes;

    std::string format;
    const auto lpath = lower(path);
    if (lpath.size() >= 4 && lpath.substr(lpath.size() - 4) == ".ccc") {
        format = "ColorCorrectionCollection";
    } else if (lpath.size() >= 4 && lpath.substr(lpath.size() - 4) == ".cdl") {
        format = "ColorDecisionList";
    } else if (lpath.size() >= 3 && lpath.substr(lpath.size() - 3) == ".cc") {
        format = "ColorCorrection";
    } else {
        throw std::runtime_error("CDL export needs a .ccc, .cdl or .cc file name.");
    }

    OCIO::GroupTransformRcPtr grp = OCIO::GroupTransform::Create();
    std::set<std::string> used_ids;

    for (const auto &layer : profile.layers) {

        if (!layer.enabled) {
            notes.push_back(
                fmt::format("'{}' is switched off and was not exported.", layer.name));
            continue;
        }

        if (format == "ColorCorrection" && grp->getNumTransforms() > 0) {
            notes.push_back(fmt::format(
                "'{}' was not exported: a .cc file holds only one correction, use .ccc to "
                "export every layer.",
                layer.name));
            continue;
        }

        GradingData gd(layer.grading_data);
        const auto &grade = gd.grade();

        const double exposure_gain = std::pow(2.0, grade.exposure);
        const double slope[3]      = {
            grade.slope[0] * grade.slope[3] * exposure_gain,
            grade.slope[1] * grade.slope[3] * exposure_gain,
            grade.slope[2] * grade.slope[3] * exposure_gain};
        const double offset[3] = {
            grade.offset[0] + grade.offset[3],
            grade.offset[1] + grade.offset[3],
            grade.offset[2] + grade.offset[3]};
        const double power[3] = {
            grade.power[0] * grade.power[3],
            grade.power[1] * grade.power[3],
            grade.power[2] * grade.power[3]};

        OCIO::CDLTransformRcPtr cdl = OCIO::CDLTransform::Create();
        cdl->setSlope(slope);
        cdl->setOffset(offset);
        cdl->setPower(power);
        cdl->setSat(grade.sat);

        OCIO::FormatMetadata &metadata = cdl->getFormatMetadata();
        metadata.setID(cdl_id_for(layer.name, used_ids).c_str());

        std::vector<std::string> description;
        description.push_back(fmt::format(
            "xSTUDIO colour profile '{}', layer '{}'. Graded in {}.",
            profile.name,
            layer.name,
            gd.colour_space()));
        if (grade.exposure != 0.0) {
            description.push_back(fmt::format(
                "Exposure {} stops is folded into the slope.", fmt_num(grade.exposure)));
        }
        if (grade.contrast != 1.0) {
            const auto msg = fmt::format(
                "Contrast {} (pivot 0.18) is not part of ASC CDL and is not included.",
                fmt_num(grade.contrast));
            description.push_back(msg);
            notes.push_back(fmt::format("'{}': {}", layer.name, msg));
        }
        if (!gd.mask().empty()) {
            const std::string msg = "Mask shapes are not part of ASC CDL; this correction "
                                    "applies to the whole frame.";
            description.push_back(msg);
            notes.push_back(fmt::format("'{}': {}", layer.name, msg));
        }
        for (const auto &d : description) {
            metadata.addChildElement(OCIO::METADATA_DESCRIPTION, d.c_str());
        }

        grp->appendTransform(cdl);
    }

    if (grp->getNumTransforms() == 0) {
        throw std::runtime_error("Nothing to export: the profile has no switched on layers.");
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw std::runtime_error(fmt::format("Can't write {}", path));
    }
    // Writing CDL formats does not depend on the colour config, a raw config
    // keeps this independent of $OCIO.
    grp->write(OCIO::Config::CreateRaw(), format.c_str(), ofs);

    return notes;
}
