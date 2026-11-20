// SPDX-License-Identifier: Apache-2.0

// Per layer working spaces: the colour space list taken from the OCIO config
// and the OCIO GPU shader code generated for the grading operation.

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "xstudio/media_reader/image_buffer.hpp"

#include "grading_working_space.h"
#include "grading_colour_op.hpp"

using namespace xstudio;
using namespace xstudio::colour_pipeline;

namespace fs = std::filesystem;

namespace {

// A small OCIO v2 config: a linear reference, two log spaces, a data space
// (not useful to grade in) and an inactive space.
const char *test_config = R"(ocio_profile_version: 2

roles:
  scene_linear: lin_ref
  compositing_log: log_cct
  color_timing: log_cct
  default: lin_ref
  data: raw

displays:
  sRGB:
    - !<View> {name: Raw, colorspace: raw}

active_displays: []
active_views: []
inactive_colorspaces: [hidden_space]

colorspaces:
  - !<ColorSpace>
    name: lin_ref
    isdata: false

  - !<ColorSpace>
    name: log_cct
    isdata: false
    from_scene_reference: !<LogAffineTransform> {base: 2, log_side_slope: 0.0570776, log_side_offset: 0.5547945}

  - !<ColorSpace>
    name: log2
    isdata: false
    from_scene_reference: !<LogTransform> {base: 2}

  - !<ColorSpace>
    name: hidden_space
    isdata: false

  - !<ColorSpace>
    name: raw
    isdata: true
)";

class GradingWorkingSpaceTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        config_path_ = (fs::path(::testing::TempDir()) / "xstudio_grading_test.ocio").string();
        std::ofstream out(config_path_);
        out << test_config;
    }

    static void TearDownTestSuite() {
        std::error_code ec;
        fs::remove(config_path_, ec);
    }

    static utility::JsonStore metadata() {
        utility::JsonStore m;
        m["ocio_config"]   = config_path_;
        m["working_space"] = "scene_linear";
        return m;
    }

    static std::string config_path_;
};

std::string GradingWorkingSpaceTest::config_path_;

} // anonymous namespace

TEST_F(GradingWorkingSpaceTest, ReferenceSpace) {
    EXPECT_EQ(grading::reference_working_space(utility::JsonStore()), "scene_linear");

    utility::JsonStore m;
    m["working_space"] = "ACEScg";
    EXPECT_EQ(grading::reference_working_space(m), "ACEScg");

    m["working_space"] = "";
    EXPECT_EQ(grading::reference_working_space(m), "scene_linear");
}

TEST_F(GradingWorkingSpaceTest, ChoicesComeFromTheConfig) {
    auto config  = grading::ocio_config(metadata());
    auto choices = grading::working_space_choices(config);

    // Roles first, then the active, non data colour spaces
    const std::vector<std::string> expected{
        "scene_linear", "compositing_log", "color_timing", "lin_ref", "log_cct", "log2"};
    EXPECT_EQ(choices, expected);
}

TEST_F(GradingWorkingSpaceTest, ConfigIsCached) {
    EXPECT_EQ(grading::ocio_config(metadata()), grading::ocio_config(metadata()));
}

TEST_F(GradingWorkingSpaceTest, MissingConfigFallsBackToRaw) {
    utility::JsonStore m;
    m["ocio_config"] = "/this/config/does/not/exist.ocio";
    auto config      = grading::ocio_config(m);
    ASSERT_TRUE(config);

    // Still produces valid (identity) shader code
    auto shader = grading::build_working_space_shader(m, {"compositing_log"});
    EXPECT_NE(
        shader.glsl.find("vec4 grading_to_working_space(vec4 rgba, int space)"),
        std::string::npos);
    EXPECT_EQ(shader.shader_descs.size(), 2u);
}

TEST_F(GradingWorkingSpaceTest, ContextVariables) {
    auto m            = metadata();
    m["ocio_context"] = {{"SHOT", "sh010"}, {"NOT_A_STRING", 3}};
    auto config       = grading::ocio_config(m);
    auto context      = grading::ocio_context(config, m);
    EXPECT_STREQ(context->getStringVar("SHOT"), "sh010");

    // The context is part of the cache key: shots can have different transforms
    EXPECT_NE(grading::colour_metadata_key(m), grading::colour_metadata_key(metadata()));
}

TEST_F(GradingWorkingSpaceTest, ShaderWorkingSpaces) {
    // Defaults first, then the other requested spaces sorted. Empty names and
    // the reference space need no conversion.
    const auto spaces = grading::shader_working_spaces(
        {"log2", "", "scene_linear", "compositing_log", "log_cct", "log2"}, "scene_linear");
    const std::vector<std::string> expected{"compositing_log", "log2", "log_cct"};
    EXPECT_EQ(spaces, expected);

    // Nothing requested: just the defaults
    EXPECT_EQ(
        grading::shader_working_spaces({}, "scene_linear"),
        grading::default_shader_working_spaces());
}

TEST_F(GradingWorkingSpaceTest, WorkingSpaceIndex) {
    const std::vector<std::string> spaces{"compositing_log", "log2"};

    EXPECT_EQ(grading::working_space_index(spaces, "scene_linear", "scene_linear"), 0);
    EXPECT_EQ(grading::working_space_index(spaces, "", "scene_linear"), 0);
    EXPECT_EQ(grading::working_space_index(spaces, "compositing_log", "scene_linear"), 1);
    EXPECT_EQ(grading::working_space_index(spaces, "log2", "scene_linear"), 2);
    EXPECT_EQ(grading::working_space_index(spaces, "log_cct", "scene_linear"), -1);

    // Media in another working space: that space is the reference
    EXPECT_EQ(grading::working_space_index(spaces, "ACEScg", "ACEScg"), 0);
    EXPECT_EQ(grading::working_space_index(spaces, "scene_linear", "ACEScg"), -1);
}

TEST_F(GradingWorkingSpaceTest, ShaderHasOneConversionPairPerSpace) {
    auto shader = grading::build_working_space_shader(metadata(), {"compositing_log", "log2"});

    EXPECT_EQ(shader.shader_descs.size(), 4u);
    for (const char *fn :
         {"GradingToWorkingSpace1(",
          "GradingFromWorkingSpace1(",
          "GradingToWorkingSpace2(",
          "GradingFromWorkingSpace2(",
          "vec4 grading_to_working_space(vec4 rgba, int space)",
          "vec4 grading_from_working_space(vec4 rgba, int space)",
          "if (space == 2) return GradingToWorkingSpace2(rgba);",
          "if (space == 1) return GradingFromWorkingSpace1(rgba);"}) {
        EXPECT_NE(shader.glsl.find(fn), std::string::npos) << fn;
    }

    // The colour op shader is renamed by xSTUDIO by replacing the first
    // occurrence of its entry point name, it must not appear in this code.
    EXPECT_EQ(shader.glsl.find("colour_transform_op"), std::string::npos);
}

TEST_F(GradingWorkingSpaceTest, UnknownSpaceFallsBackToNoConversion) {
    // A real conversion and the "no conversion" fallback give different code
    auto real    = grading::build_working_space_shader(metadata(), {"log_cct"});
    auto unknown = grading::build_working_space_shader(metadata(), {"no_such_space"});
    EXPECT_NE(real.glsl, unknown.glsl);
    EXPECT_NE(unknown.glsl.find("GradingToWorkingSpace1("), std::string::npos);

    // compositing_log is a role pointing at log_cct: same conversion
    auto role = grading::build_working_space_shader(metadata(), {"compositing_log"});
    auto name = grading::build_working_space_shader(metadata(), {"log_cct"});
    EXPECT_EQ(role.glsl.substr(role.glsl.find('\n')), name.glsl.substr(name.glsl.find('\n')));
}

TEST_F(GradingWorkingSpaceTest, ColourOpDataRemembersItsSpaces) {
    const std::vector<std::string> spaces{"compositing_log", "log2"};
    auto data = GradingColourOperator::colour_op_data_for(metadata(), spaces);
    ASSERT_TRUE(data);
    ASSERT_TRUE(data->shader_);
    EXPECT_EQ(data->uuid_, GradingColourOperator::PLUGIN_UUID);

    const auto *stored = std::any_cast<std::vector<std::string>>(&data->user_data_);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(*stored, spaces);

    // Cached: same inputs give the same data
    EXPECT_EQ(data, GradingColourOperator::colour_op_data_for(metadata(), spaces));

    // Different working spaces give a different shader (and cache id)
    auto other = GradingColourOperator::colour_op_data_for(
        metadata(), grading::default_shader_working_spaces());
    EXPECT_NE(other->cache_id(), data->cache_id());
}

TEST_F(GradingWorkingSpaceTest, ImageWithoutGradingShaderUsesDefaults) {
    media_reader::ImageBufPtr image;
    EXPECT_EQ(
        GradingColourOperator::image_working_spaces(image),
        grading::default_shader_working_spaces());
}
