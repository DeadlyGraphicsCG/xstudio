// SPDX-License-Identifier: Apache-2.0
#include <utility>

#include "grading_data.h"
#include "grading_data_serialiser.hpp"
#include "grading.h"

using namespace xstudio::ui::viewport;
using namespace xstudio;

namespace {

// Reads an optional [R, G, B] array. Anything missing or malformed leaves
// the default value untouched.
void read_rgb(const nlohmann::json &j, const char *key, std::array<double, 3> &value) {
    if (!j.contains(key))
        return;
    const auto &v = j.at(key);
    if (!v.is_array() || v.size() != 3)
        return;
    for (size_t i = 0; i < 3; ++i) {
        if (!v[i].is_number())
            return;
    }
    for (size_t i = 0; i < 3; ++i) {
        value[i] = v[i].get<double>();
    }
}

} // anonymous namespace

void xstudio::ui::viewport::from_json(const nlohmann::json &j, Grade &g) {

    j.at("slope").get_to(static_cast<std::array<double, 4> &>(g.slope));
    j.at("offset").get_to(static_cast<std::array<double, 4> &>(g.offset));
    j.at("power").get_to(static_cast<std::array<double, 4> &>(g.power));
    j.at("sat").get_to(g.sat);

    if (j.contains("exposure"))
        j.at("exposure").get_to(g.exposure);
    if (j.contains("contrast"))
        j.at("contrast").get_to(g.contrast);

    // Per channel values are optional: grades saved before they existed
    // simply keep the defaults (no per channel change).
    read_rgb(j, "exposure_rgb", g.exposure_rgb);
    read_rgb(j, "contrast_rgb", g.contrast_rgb);
    read_rgb(j, "sat_rgb", g.sat_rgb);
}

void xstudio::ui::viewport::to_json(nlohmann::json &j, const Grade &g) {

    j["slope"]    = g.slope;
    j["offset"]   = g.offset;
    j["power"]    = g.power;
    j["sat"]      = g.sat;
    j["exposure"] = g.exposure;
    j["contrast"] = g.contrast;

    // New optional fields. The master values above keep their type, so older
    // xSTUDIO builds can still load grades written by this version (they
    // simply ignore these keys).
    j["exposure_rgb"] = g.exposure_rgb;
    j["contrast_rgb"] = g.contrast_rgb;
    j["sat_rgb"]      = g.sat_rgb;
}

std::array<double, 3> Grade::effective_exposure() const {
    return {exposure + exposure_rgb[0], exposure + exposure_rgb[1], exposure + exposure_rgb[2]};
}

std::array<double, 3> Grade::effective_contrast() const {
    return {contrast * contrast_rgb[0], contrast * contrast_rgb[1], contrast * contrast_rgb[2]};
}

std::array<double, 3> Grade::effective_sat() const {
    return {sat * sat_rgb[0], sat * sat_rgb[1], sat * sat_rgb[2]};
}

bool Grade::has_per_channel_values() const {
    const Grade defaults;
    return exposure_rgb != defaults.exposure_rgb || contrast_rgb != defaults.contrast_rgb ||
           sat_rgb != defaults.sat_rgb;
}

GradingData::GradingData(const utility::JsonStore &s) : bookmark::AnnotationBase() {

    GradingDataSerialiser::deserialise(this, s);
}

utility::JsonStore GradingData::serialise(utility::Uuid &plugin_uuid) const {

    plugin_uuid = colour_pipeline::GradingTool::PLUGIN_UUID;
    return GradingDataSerialiser::serialise((const GradingData *)this);
}

bool GradingData::identity() const { return (grade_ == Grade() && mask_.empty()); }

void xstudio::ui::viewport::from_json(const nlohmann::json &j, GradingData &l) {

    if (j.contains("colour_space"))
        j.at("colour_space").get_to(l.colour_space_);
    if (j.contains("layer_name") && j.at("layer_name").is_string())
        j.at("layer_name").get_to(l.layer_name_);

    j.at("grade").get_to(l.grade_);
    j.at("mask_active").get_to(l.mask_active_);
    j.at("mask_editing").get_to(l.mask_editing_);
    j.at("mask").get_to(l.mask_);
}

void xstudio::ui::viewport::to_json(nlohmann::json &j, const GradingData &l) {

    j["colour_space"] = l.colour_space_;
    j["layer_name"]   = l.layer_name_;
    j["grade"]        = l.grade_;
    j["mask_active"]  = l.mask_active_;
    j["mask_editing"] = l.mask_editing_;
    j["mask"]         = l.mask_;
}