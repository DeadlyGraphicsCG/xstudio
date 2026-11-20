// SPDX-License-Identifier: Apache-2.0

// Serialisation of grade layers (GradingData / Grade), including the per
// channel values and the layer name, and loading of grades saved before
// these fields existed.

#include <gtest/gtest.h>

#include "grading_data.h"
#include "grading_data_serialiser.hpp"

using namespace xstudio;
using namespace xstudio::ui::viewport;

namespace {

// A grade layer as written by xSTUDIO before per channel values, layer names
// and working spaces from the OCIO config were added.
const char *legacy_grading_data = R"({
    "colour_space": "compositing_log",
    "grade": {
        "slope": [1.1, 1.0, 0.9, 1.0],
        "offset": [0.0, 0.01, 0.0, 0.0],
        "power": [1.0, 1.0, 1.0, 1.2],
        "sat": 0.8,
        "exposure": 0.5,
        "contrast": 1.1
    },
    "mask_active": false,
    "mask_editing": false,
    "mask": {}
})";

// Even older grades had no exposure / contrast either
const char *legacy_grade_without_exposure = R"({
    "slope": [1.0, 1.0, 1.0, 1.0],
    "offset": [0.0, 0.0, 0.0, 0.0],
    "power": [1.0, 1.0, 1.0, 1.0],
    "sat": 1.0
})";

GradingData make_full_grading_data() {
    GradingData gd;
    gd.set_colour_space("ACEScct");
    gd.set_layer_name("Warm key | hero");
    auto &g        = gd.grade();
    g.slope        = {1.2, 1.0, 0.8, 1.1};
    g.offset       = {0.01, 0.0, -0.01, 0.0};
    g.power        = {1.0, 1.1, 1.0, 1.0};
    g.sat          = 0.9;
    g.exposure     = 0.25;
    g.contrast     = 1.05;
    g.exposure_rgb = {0.5, 0.0, -0.5};
    g.contrast_rgb = {1.0, 1.2, 1.0};
    g.sat_rgb      = {1.0, 1.0, 0.5};
    return gd;
}

} // anonymous namespace

TEST(GradingDataTest, DefaultsAreIdentity) {
    GradingData gd;
    EXPECT_TRUE(gd.identity());
    EXPECT_EQ(gd.colour_space(), "scene_linear");
    EXPECT_EQ(gd.layer_name(), "");
    EXPECT_FALSE(gd.grade().has_per_channel_values());
}

TEST(GradingDataTest, PerChannelValuesBreakIdentity) {
    for (int channel = 0; channel < 3; ++channel) {
        GradingData exposure;
        exposure.grade().exposure_rgb[channel] = 0.1;
        EXPECT_FALSE(exposure.identity());
        EXPECT_TRUE(exposure.grade().has_per_channel_values());

        GradingData contrast;
        contrast.grade().contrast_rgb[channel] = 1.1;
        EXPECT_FALSE(contrast.identity());

        GradingData sat;
        sat.grade().sat_rgb[channel] = 0.5;
        EXPECT_FALSE(sat.identity());
    }
}

TEST(GradingDataTest, LayerNameAndWorkingSpaceDoNotBreakIdentity) {
    // A renamed layer without any grade still changes nothing in the image
    GradingData gd;
    gd.set_layer_name("Sky");
    gd.set_colour_space("compositing_log");
    EXPECT_TRUE(gd.identity());
}

TEST(GradingDataTest, EqualityIncludesNewFields) {
    const GradingData a = make_full_grading_data();

    GradingData b = a;
    EXPECT_TRUE(a == b);

    b.set_layer_name("Other");
    EXPECT_FALSE(a == b);

    b                         = a;
    b.grade().exposure_rgb[2] = 0.0;
    EXPECT_FALSE(a == b);

    b                         = a;
    b.grade().contrast_rgb[1] = 1.0;
    EXPECT_FALSE(a == b);

    b                    = a;
    b.grade().sat_rgb[2] = 1.0;
    EXPECT_FALSE(a == b);
}

TEST(GradingDataTest, CopyKeepsLayerName) {
    const GradingData a = make_full_grading_data();
    GradingData copy(a);
    EXPECT_EQ(copy.layer_name(), "Warm key | hero");
    GradingData assigned;
    assigned = a;
    EXPECT_EQ(assigned.layer_name(), "Warm key | hero");
    EXPECT_EQ(assigned.grade().exposure_rgb, a.grade().exposure_rgb);
}

TEST(GradingDataTest, EffectiveValuesCombineMasterAndChannels) {
    Grade g;
    g.exposure     = 1.0;
    g.exposure_rgb = {0.5, 0.0, -1.5};
    g.contrast     = 2.0;
    g.contrast_rgb = {1.0, 0.5, 1.5};
    g.sat          = 0.5;
    g.sat_rgb      = {2.0, 1.0, 0.0};

    // exposure trims are added (stops)
    EXPECT_EQ(g.effective_exposure(), (std::array<double, 3>{1.5, 1.0, -0.5}));
    // contrast and saturation trims are multiplied
    EXPECT_EQ(g.effective_contrast(), (std::array<double, 3>{2.0, 1.0, 3.0}));
    EXPECT_EQ(g.effective_sat(), (std::array<double, 3>{1.0, 0.5, 0.0}));

    // Defaults: the master values on all three channels
    Grade d;
    d.exposure = -0.5;
    d.contrast = 1.2;
    d.sat      = 0.7;
    EXPECT_EQ(d.effective_exposure(), (std::array<double, 3>{-0.5, -0.5, -0.5}));
    EXPECT_EQ(d.effective_contrast(), (std::array<double, 3>{1.2, 1.2, 1.2}));
    EXPECT_EQ(d.effective_sat(), (std::array<double, 3>{0.7, 0.7, 0.7}));
}

TEST(GradingDataTest, JsonRoundTrip) {
    const GradingData original = make_full_grading_data();

    nlohmann::json j = original;
    EXPECT_EQ(j.at("layer_name"), "Warm key | hero");
    EXPECT_EQ(j.at("colour_space"), "ACEScct");
    EXPECT_EQ(j.at("grade").at("exposure_rgb").size(), 3u);
    EXPECT_EQ(j.at("grade").at("contrast_rgb").size(), 3u);
    EXPECT_EQ(j.at("grade").at("sat_rgb").size(), 3u);

    // The master values keep their old type, so older xSTUDIO versions can
    // still read grades written by this version.
    EXPECT_TRUE(j.at("grade").at("exposure").is_number());
    EXPECT_TRUE(j.at("grade").at("contrast").is_number());
    EXPECT_TRUE(j.at("grade").at("sat").is_number());

    const GradingData restored = j.get<GradingData>();
    EXPECT_TRUE(restored == original);
    EXPECT_EQ(restored.grade().exposure_rgb, original.grade().exposure_rgb);
    EXPECT_EQ(restored.grade().contrast_rgb, original.grade().contrast_rgb);
    EXPECT_EQ(restored.grade().sat_rgb, original.grade().sat_rgb);
}

TEST(GradingDataTest, SerialiserRoundTripKeepsVersion) {
    const GradingData original = make_full_grading_data();

    utility::Uuid plugin_uuid;
    const utility::JsonStore stored = original.serialise(plugin_uuid);

    // Still written as serialiser version 1.0, so older builds can load it
    EXPECT_EQ(stored.at("Grading Serialiser Version").get<int>(), (1 << 8) + 0);
    EXPECT_FALSE(plugin_uuid.is_null());

    const GradingData restored(stored);
    EXPECT_TRUE(restored == original);
    EXPECT_EQ(restored.layer_name(), "Warm key | hero");
}

TEST(GradingDataTest, LoadsLegacyGrade) {
    utility::JsonStore stored;
    stored["Grading Serialiser Version"] = (1 << 8) + 0;
    stored["Data"]                       = nlohmann::json::parse(legacy_grading_data);

    const GradingData gd(stored);

    EXPECT_EQ(gd.colour_space(), "compositing_log");
    EXPECT_EQ(gd.layer_name(), "");
    EXPECT_DOUBLE_EQ(gd.grade().sat, 0.8);
    EXPECT_DOUBLE_EQ(gd.grade().exposure, 0.5);
    EXPECT_DOUBLE_EQ(gd.grade().contrast, 1.1);
    EXPECT_DOUBLE_EQ(gd.grade().slope[0], 1.1);
    EXPECT_DOUBLE_EQ(gd.grade().power[3], 1.2);

    // No per channel values in old grades: nothing changes per channel
    EXPECT_FALSE(gd.grade().has_per_channel_values());
    EXPECT_EQ(gd.grade().effective_exposure(), (std::array<double, 3>{0.5, 0.5, 0.5}));
    EXPECT_EQ(gd.grade().effective_sat(), (std::array<double, 3>{0.8, 0.8, 0.8}));
}

TEST(GradingDataTest, LoadsVeryOldGradeWithoutExposure) {
    const Grade g = nlohmann::json::parse(legacy_grade_without_exposure).get<Grade>();
    EXPECT_TRUE(g == Grade());
}

TEST(GradingDataTest, IgnoresMalformedPerChannelValues) {
    auto j            = nlohmann::json::parse(legacy_grade_without_exposure);
    j["exposure_rgb"] = {1.0, 2.0};      // wrong size
    j["contrast_rgb"] = "not an array";  // wrong type
    j["sat_rgb"]      = {1.0, "x", 1.0}; // wrong element type
    const Grade g     = j.get<Grade>();
    EXPECT_FALSE(g.has_per_channel_values());

    auto j2              = nlohmann::json::parse(legacy_grading_data);
    j2["layer_name"]     = 42; // wrong type, ignored
    const GradingData gd = j2.get<GradingData>();
    EXPECT_EQ(gd.layer_name(), "");
}
