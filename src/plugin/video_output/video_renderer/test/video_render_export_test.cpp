// SPDX-License-Identifier: Apache-2.0
//
// Tests for the "export with grade" helpers of the video renderer: export
// profiles, naming templates and the grade sidecar files (.ccc / .cube).

#include <gtest/gtest.h>

#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <sstream>

#include <OpenColorIO/OpenColorIO.h> //NOLINT

#include "export_profiles.hpp"
#include "grade_sidecar.hpp"

using namespace xstudio::video_render_plugin_1_0;
using nlohmann::json;
namespace fs   = std::filesystem;
namespace OCIO = OCIO_NAMESPACE;

namespace {

// A temporary folder that is removed again at the end of the test
struct TempDir {
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("xs_vr_export_test_" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

void touch(const fs::path &p) { std::ofstream(p.string()) << "x"; }

// One serialised grade bookmark, in the shape BookmarksActor serialises to
json grade_bookmark(
    const std::string &owner,
    const int64_t created_us,
    const json &grade,
    const json &user_data     = json::object(),
    const std::string &space  = "scene_linear",
    const bool with_mask      = false,
    const bool full_range     = true,
    const std::string &u_type = "Grading") {

    json mask = {
        {"pen_strokes", json::array()},
        {"captions", json::array()},
        {"quads", json::array()},
        {"polygons", json::array()},
        {"ellipses", json::array()}};
    if (with_mask)
        mask["ellipses"].push_back(json{{"_id", 1}});

    json base;
    base["owner"]     = owner;
    base["user_type"] = u_type;
    base["user_data"] = user_data;
    base["created"]   = {{"since_epoch", created_us}, {"unit", "microseconds"}};
    base["duration"]  = {
        {"count", full_range ? std::numeric_limits<int64_t>::max() : int64_t(0)}};
    base["annotation"] = {
        {"Grading Serialiser Version", 256},
        {"plugin_uuid", "5598e01e-c6bc-4cf9-80ff-74bb560df12a"},
        {"Data",
         {{"colour_space", space},
          {"grade", grade},
          {"mask_active", with_mask},
          {"mask_editing", false},
          {"mask", mask}}}};
    return json{{"store", json::object()}, {"base", base}};
}

json grade_values(
    const std::array<double, 4> &slope  = {1, 1, 1, 1},
    const std::array<double, 4> &offset = {0, 0, 0, 0},
    const std::array<double, 4> &power  = {1, 1, 1, 1},
    double sat                          = 1.0,
    double exposure                     = 0.0,
    double contrast                     = 1.0) {
    return json{
        {"slope", slope},
        {"offset", offset},
        {"power", power},
        {"sat", sat},
        {"exposure", exposure},
        {"contrast", contrast}};
}

const std::string media_a = "11111111-1111-1111-1111-111111111111";
const std::string media_b = "22222222-2222-2222-2222-222222222222";

json bookmarks_fixture() {
    json j;
    j["base"]   = json::object();
    j["actors"] = json::object();
    // created later, but listed first: must come second
    j["actors"]["b2"] = grade_bookmark(
        media_a,
        2000,
        grade_values({1, 1, 1, 1}, {0, 0, 0, 0}, {1, 1, 1, 1}, 0.5),
        json{{"layer_name", "look"}});
    j["actors"]["b1"] = grade_bookmark(
        media_a,
        1000,
        grade_values({2, 1, 1, 1}),
        json{{"layer_name", "balance"}, {"grade_active", true}});
    j["actors"]["b3"] = grade_bookmark(
        media_b, 1500, grade_values(), json{{"grade_active", false}}, "compositing_log", true);
    // a normal note, not a grade
    j["actors"]["n1"] =
        grade_bookmark(media_a, 10, grade_values(), json::object(), "", false, true, "");
    j["actors"]["n1"]["base"]["annotation"] = nullptr;
    return j;
}

} // namespace

// ---------------------------------------------------------------------------
// Export profiles
// ---------------------------------------------------------------------------

TEST(VideoRenderExportProfiles, DefaultsAreFilledIn) {

    const auto p = export_profiles::normalise_export_profile(json{{"name", "  Dailies "}});
    const auto d = export_profiles::default_export_profile();

    for (auto it = d.begin(); it != d.end(); ++it) {
        ASSERT_TRUE(p.contains(it.key())) << it.key();
    }
    EXPECT_EQ(p["name"], "Dailies");
    EXPECT_EQ(p["bake_grade"], true);
    EXPECT_EQ(p["container"], "mov");
    EXPECT_EQ(p["naming_template"], "{media_name}_{version}");
}

TEST(VideoRenderExportProfiles, OldAndOddProfilesStillLoad) {

    // an older or hand edited profile: missing fields, wrong types, extras
    const json old = {
        {"name", "Old"},
        {"video_preset", "ProRes 422 HQ "},
        {"bake_grade", "yes"},  // wrong type -> default
        {"frame_rate", 25},     // number -> string
        {"container", ".MP4 "}, // tidied up
        {"naming_template", "  "},
        {"some_future_field", {1, 2, 3}}};

    const auto p = export_profiles::normalise_export_profile(old);
    EXPECT_EQ(p["video_preset"], "ProRes 422 HQ ");
    EXPECT_EQ(p["bake_grade"], true);
    EXPECT_EQ(p["frame_rate"], "25");
    EXPECT_EQ(p["container"], "mp4");
    EXPECT_EQ(p["naming_template"], "{media_name}_{version}");
    EXPECT_EQ(p["some_future_field"], json({1, 2, 3}));
    EXPECT_EQ(p["sidecar_cdl"], false);

    // not an object at all
    EXPECT_EQ(export_profiles::normalise_export_profile(json(42))["name"], "");
}

TEST(VideoRenderExportProfiles, JsonRoundTrip) {

    json profiles        = json::array();
    json p               = export_profiles::default_export_profile();
    p["name"]            = "Client review";
    p["bake_grade"]      = false;
    p["sidecar_lut"]     = true;
    p["ocio_display"]    = "sRGB - Display";
    p["ocio_view"]       = "ACES 1.0 - SDR Video";
    p["naming_template"] = "{media_name}_{date}_{version}";
    ASSERT_TRUE(export_profiles::save_export_profile(profiles, p));

    // what the preferences store keeps is plain JSON text
    const auto reloaded =
        export_profiles::normalise_export_profiles(json::parse(profiles.dump()));
    ASSERT_EQ(reloaded.size(), 1u);
    EXPECT_EQ(reloaded[0], export_profiles::normalise_export_profile(p));
}

TEST(VideoRenderExportProfiles, ListIsCleanedUp) {

    EXPECT_EQ(export_profiles::normalise_export_profiles(json("nope")), json::array());

    const json list = {
        json{{"name", "A"}},
        json(3),
        json{{"name", ""}},
        json{{"name", "B"}},
        json{{"name", "A"}, {"container", "mp4"}}};
    const auto clean = export_profiles::normalise_export_profiles(list);
    ASSERT_EQ(clean.size(), 2u);
    EXPECT_EQ(clean[0]["name"], "A");
    EXPECT_EQ(clean[0]["container"], "mov"); // first one wins
    EXPECT_EQ(clean[1]["name"], "B");
}

TEST(VideoRenderExportProfiles, SaveRenameDelete) {

    json profiles = json::array();
    EXPECT_FALSE(export_profiles::save_export_profile(profiles, json{{"name", " "}}));
    EXPECT_TRUE(export_profiles::save_export_profile(profiles, json{{"name", "A"}}));
    EXPECT_TRUE(export_profiles::save_export_profile(profiles, json{{"name", "B"}}));
    // saving under an existing name replaces it
    EXPECT_TRUE(export_profiles::save_export_profile(
        profiles, json{{"name", "A"}, {"container", "mxf"}}));
    ASSERT_EQ(profiles.size(), 2u);
    EXPECT_EQ(profiles[0]["container"], "mxf");

    std::string error;
    EXPECT_FALSE(export_profiles::rename_export_profile(profiles, "A", "B", error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(export_profiles::rename_export_profile(profiles, "Nope", "C", error));
    EXPECT_FALSE(export_profiles::rename_export_profile(profiles, "A", "  ", error));
    EXPECT_TRUE(export_profiles::rename_export_profile(profiles, "A", " C ", error));
    EXPECT_EQ(export_profiles::find_export_profile(profiles, "C"), 0);
    EXPECT_EQ(export_profiles::find_export_profile(profiles, "A"), -1);

    EXPECT_TRUE(export_profiles::delete_export_profile(profiles, "C"));
    EXPECT_FALSE(export_profiles::delete_export_profile(profiles, "C"));
    ASSERT_EQ(profiles.size(), 1u);
    EXPECT_EQ(profiles[0]["name"], "B");
}

TEST(VideoRenderExportProfiles, ShippedProfilesAreComplete) {

    std::ifstream f(VIDEO_RENDER_PREFS_FILE);
    ASSERT_TRUE(f.is_open());
    const auto prefs    = json::parse(f);
    const auto &entry   = prefs["plugin"]["video_render"]["export_profiles"];
    const auto profiles = export_profiles::normalise_export_profiles(entry["value"]);
    ASSERT_EQ(profiles.size(), entry["value"].size());
    ASSERT_GE(profiles.size(), 2u);

    // every shipped profile already has every field
    for (size_t i = 0; i < profiles.size(); ++i)
        EXPECT_EQ(profiles[i], entry["value"][i]);

    // the shipped codec presets must exist
    std::set<std::string> presets;
    for (const auto &p :
         prefs["plugin"]["video_render"]["video_codec_defaults_preset"]["value"])
        presets.insert(p[0].get<std::string>());
    for (const auto &p : profiles)
        EXPECT_TRUE(presets.count(p["video_preset"].get<std::string>())) << p["video_preset"];
}

TEST(VideoRenderExportProfiles, OutputPathFromProfile) {

    json p               = export_profiles::default_export_profile();
    p["output_folder"]   = "/renders/";
    p["naming_template"] = "{media_name}_{version}";
    EXPECT_EQ(
        export_profiles::profile_output_path(p, "/x"), "/renders/{media_name}_{version}.mov");

    p["output_folder"] = "";
    EXPECT_EQ(export_profiles::profile_output_path(p, "/x"), "/x/{media_name}_{version}.mov");
    EXPECT_EQ(export_profiles::profile_output_path(p, ""), "{media_name}_{version}.mov");

    // image formats become frame sequences
    p["container"] = "PNG";
    EXPECT_EQ(
        export_profiles::profile_output_path(p, "C:\\out\\"),
        "C:\\out\\{media_name}_{version}.####.png");

    // extension typed into the template is not doubled
    p["container"]       = "mp4";
    p["naming_template"] = "{media_name}.mp4";
    EXPECT_EQ(export_profiles::profile_output_path(p, "/x"), "/x/{media_name}.mp4");

    EXPECT_TRUE(export_profiles::is_frame_based_container("tif"));
    EXPECT_FALSE(export_profiles::is_frame_based_container("mov"));
}

// ---------------------------------------------------------------------------
// Naming templates
// ---------------------------------------------------------------------------

TEST(VideoRenderNamingTemplate, ExpandsTokens) {

    const std::map<std::string, std::string> tokens = {
        {"media_name", "shot 010/comp: v2"},
        {"date", "20261120"},
        {"frame_range", "1001-1100"}};

    EXPECT_EQ(
        export_profiles::expand_naming_template(
            "/out/{media_name}_{date}_{version}_{frame_range}.mov", tokens, 7),
        "/out/shot_010_comp__v2_20261120_v007_1001-1100.mov");

    // unknown tokens stay visible, unclosed braces are left alone
    EXPECT_EQ(
        export_profiles::expand_naming_template("{nope}_{date}_{", tokens),
        "{nope}_20261120_{");

    EXPECT_TRUE(export_profiles::has_tokens("a{b}c"));
    EXPECT_FALSE(export_profiles::has_tokens("a{bc"));
    EXPECT_FALSE(export_profiles::has_tokens("/plain/path.mov"));

    EXPECT_EQ(export_profiles::sanitise_token_value(" a  b\tc "), "a_b_c");
    EXPECT_EQ(export_profiles::sanitise_token_value(".."), "__");
    EXPECT_EQ(export_profiles::sanitise_token_value("a<b>c|d?e*f\"g"), "a_b_c_d_e_f_g");
}

TEST(VideoRenderNamingTemplate, ClockTokens) {

    std::tm tm{};
    tm.tm_year        = 2026 - 1900;
    tm.tm_mon         = 10;
    tm.tm_mday        = 20;
    tm.tm_hour        = 9;
    tm.tm_min         = 5;
    tm.tm_sec         = 7;
    tm.tm_isdst       = -1;
    const auto when   = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    const auto tokens = export_profiles::clock_tokens(when);
    EXPECT_EQ(tokens.at("date"), "20261120");
    EXPECT_EQ(tokens.at("time"), "090507");
    EXPECT_EQ(tokens.at("year"), "2026");
}

TEST(VideoRenderNamingTemplate, PicksNextFreeVersion) {

    const std::map<std::string, std::string> tokens = {{"media_name", "shot"}};
    const std::set<std::string> taken               = {"/r/shot_v001.mov", "/r/shot_v002.mov"};
    auto exists = [&](const std::string &p) { return taken.count(p) != 0; };

    int used = 0;
    EXPECT_EQ(
        export_profiles::expand_naming_template_next_version(
            "/r/{media_name}_{version}.mov", tokens, exists, &used),
        "/r/shot_v003.mov");
    EXPECT_EQ(used, 3);

    // no {version}: nothing to pick
    EXPECT_EQ(
        export_profiles::expand_naming_template_next_version(
            "/r/{media_name}.mov", tokens, exists, &used),
        "/r/shot.mov");
    EXPECT_EQ(used, 1);

    // a version given by the caller wins
    auto with_version       = tokens;
    with_version["version"] = "v042";
    EXPECT_EQ(
        export_profiles::expand_naming_template_next_version(
            "/r/{media_name}_{version}.mov", with_version, exists),
        "/r/shot_v042.mov");
}

TEST(VideoRenderNamingTemplate, ExistingOutputsOnDisk) {

    TempDir tmp;
    touch(tmp.path / "movie_v001.mov");
    touch(tmp.path / "frames_v001.1001.png");

    const auto dir = tmp.path.string();
    EXPECT_TRUE(export_profiles::output_path_exists(dir + "/movie_v001.mov"));
    EXPECT_FALSE(export_profiles::output_path_exists(dir + "/movie_v002.mov"));
    EXPECT_TRUE(export_profiles::output_path_exists(dir + "/frames_v001.####.png"));
    EXPECT_FALSE(export_profiles::output_path_exists(dir + "/frames_v002.####.png"));
    EXPECT_FALSE(export_profiles::output_path_exists(dir + "/no_such_folder/a.####.png"));

    EXPECT_EQ(
        export_profiles::expand_naming_template_next_version(
            dir + "/frames_{version}.####.png", {}, export_profiles::output_path_exists),
        dir + "/frames_v002.####.png");
}

// ---------------------------------------------------------------------------
// Grades in bookmarks
// ---------------------------------------------------------------------------

TEST(VideoRenderGradeSidecar, CountGradingBookmarks) {

    const auto bm = bookmarks_fixture();
    EXPECT_EQ(grade_sidecar::count_grading_bookmarks(bm), 3u);
    EXPECT_TRUE(grade_sidecar::is_grading_bookmark(bm["actors"]["b1"]));
    EXPECT_FALSE(grade_sidecar::is_grading_bookmark(bm["actors"]["n1"]));

    // things that are not bookmark serialisations are fine too
    EXPECT_EQ(grade_sidecar::count_grading_bookmarks(json(5)), 0u);
    EXPECT_EQ(grade_sidecar::count_grading_bookmarks(json::object()), 0u);
}

TEST(VideoRenderGradeSidecar, LayersForMedia) {

    const auto bm = bookmarks_fixture();

    const auto a = grade_sidecar::grade_layers_for_media(bm, media_a);
    ASSERT_EQ(a.size(), 2u);
    EXPECT_EQ(a[0].name, "balance"); // oldest first
    EXPECT_DOUBLE_EQ(a[0].slope[0], 2.0);
    EXPECT_EQ(a[1].name, "look");
    EXPECT_DOUBLE_EQ(a[1].sat, 0.5);
    EXPECT_TRUE(a[0].active);
    EXPECT_FALSE(a[0].masked);
    EXPECT_TRUE(a[0].full_range);

    const auto b = grade_sidecar::grade_layers_for_media(bm, media_b);
    ASSERT_EQ(b.size(), 1u);
    EXPECT_FALSE(b[0].active);
    EXPECT_TRUE(b[0].masked);
    EXPECT_TRUE(b[0].is_log());
    EXPECT_EQ(b[0].name, "layer1");

    EXPECT_TRUE(grade_sidecar::grade_layers_for_media(bm, "nobody").empty());

    // missing or oddly typed values fall back to "no change"
    json odd;
    odd["actors"]["x"] =
        grade_bookmark(media_a, 1, json{{"slope", "big"}, {"exposure", {1, 2}}});
    odd["actors"]["x"]["base"]["duration"]["count"] = 1000;
    const auto o = grade_sidecar::grade_layers_for_media(odd, media_a);
    ASSERT_EQ(o.size(), 1u);
    EXPECT_TRUE(o[0].is_identity());
    EXPECT_FALSE(o[0].full_range);
}

TEST(VideoRenderGradeSidecar, ClipName) {
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(
            json{{"ocio_context", {{"SHOT", "sh010"}}}, {"path", "/a/b.mov"}}, "x"),
        "sh010");
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(
            json{{"path", "file:///jobs/plate.####.exr"}}, "x"),
        "plate");
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(
            json{{"path", "/a/sh020_comp.1001.png"}}, "x"),
        "sh020_comp");
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(json{{"path", "/a/sh030.{:04d}.exr"}}, "x"),
        "sh030");
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(json{{"path", "/a/sh040_v2.mov"}}, "x"),
        "sh040_v2");
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(json{{"path", "/a/sh_050.mov"}}, "x"),
        "sh_050");
    EXPECT_EQ(
        grade_sidecar::clip_name_from_colour_params(json::object(), "fallback"), "fallback");
}

TEST(VideoRenderGradeSidecar, GradeMathsMatchesShader) {

    grade_sidecar::GradeLayer l;
    std::array<float, 3> v{0.1f, 0.2f, 0.4f};

    // identity
    auto r = grade_sidecar::apply_grade_maths(v, l);
    EXPECT_FLOAT_EQ(r[1], 0.2f);

    // +1 stop doubles the values
    l.exposure = 1.0;
    r          = grade_sidecar::apply_grade_maths(v, l);
    EXPECT_FLOAT_EQ(r[0], 0.2f);
    EXPECT_FLOAT_EQ(r[2], 0.8f);

    // contrast pivots around 0.18
    l          = grade_sidecar::GradeLayer();
    l.contrast = 2.0;
    r          = grade_sidecar::apply_grade_maths({0.18f, 0.36f, -0.36f}, l);
    EXPECT_NEAR(r[0], 0.18f, 1e-6);
    EXPECT_NEAR(r[1], 0.72f, 1e-6);
    EXPECT_NEAR(r[2], -0.72f, 1e-6); // mirrored for negatives

    // CDL with the 4th (master) channel
    l        = grade_sidecar::GradeLayer();
    l.slope  = {2.0, 1.0, 1.0, 0.5};
    l.offset = {0.1, 0.0, 0.0, 0.05};
    l.power  = {1.0, 1.0, 2.0, 1.0};
    r        = grade_sidecar::apply_grade_maths(v, l);
    EXPECT_NEAR(r[0], 0.1f * 1.0f + 0.15f, 1e-6);
    EXPECT_NEAR(r[1], 0.2f * 0.5f + 0.05f, 1e-6);
    EXPECT_NEAR(r[2], std::pow(0.4f * 0.5f + 0.05f, 2.0f), 1e-6);

    // saturation 0 gives Rec.709 luma
    l                = grade_sidecar::GradeLayer();
    l.sat            = 0.0;
    r                = grade_sidecar::apply_grade_maths(v, l);
    const float luma = 0.1f * 0.2126f + 0.2f * 0.7152f + 0.4f * 0.0722f;
    EXPECT_NEAR(r[0], luma, 1e-5);
    EXPECT_NEAR(r[2], luma, 1e-5);
}

TEST(VideoRenderGradeSidecar, CdlValues) {

    grade_sidecar::GradeLayer l;
    l.slope    = {1.0, 2.0, 1.0, 1.5};
    l.exposure = 1.0;
    auto cdl   = grade_sidecar::layer_to_cdl(l);
    EXPECT_DOUBLE_EQ(cdl.slope[0], 3.0);
    EXPECT_DOUBLE_EQ(cdl.slope[1], 6.0);
    EXPECT_TRUE(cdl.exact);

    l.contrast = 1.2;
    EXPECT_FALSE(grade_sidecar::layer_to_cdl(l).exact);
}

TEST(VideoRenderGradeSidecar, CccFileReadsBackInOCIO) {

    grade_sidecar::ClipGrade clip;
    clip.clip_name = "sh010";
    grade_sidecar::GradeLayer l1;
    l1.name                      = "balance";
    l1.slope                     = {1.2, 1.0, 0.8, 1.0};
    l1.offset                    = {0.01, 0.0, -0.01, 0.0};
    l1.power                     = {1.1, 1.0, 0.9, 1.0};
    l1.sat                       = 0.8;
    grade_sidecar::GradeLayer l2 = l1;
    l2.name                      = "contrasty";
    l2.contrast                  = 1.3;
    grade_sidecar::GradeLayer off;
    off.name    = "off";
    off.active  = false;
    clip.layers = {l1, l2, off};

    std::vector<std::string> warnings;
    const auto ccc = grade_sidecar::make_ccc({clip}, warnings);
    ASSERT_FALSE(ccc.empty());
    EXPECT_NE(ccc.find("<ColorCorrection id=\"sh010_balance\">"), std::string::npos);
    EXPECT_NE(ccc.find("<ColorCorrection id=\"sh010_contrasty\">"), std::string::npos);
    EXPECT_EQ(ccc.find("sh010_off"), std::string::npos); // inactive layers are left out
    EXPECT_EQ(warnings.size(), 1u);                      // contrast can't go in a CDL

    // nothing to write
    std::vector<std::string> w2;
    EXPECT_TRUE(grade_sidecar::make_ccc({}, w2).empty());

    // OCIO must be able to read the file and apply the same CDL maths
    TempDir tmp;
    const auto path = (tmp.path / "grade.ccc").string();
    std::ofstream(path) << ccc;

    auto config = OCIO::Config::CreateRaw();
    auto ft     = OCIO::FileTransform::Create();
    ft->setSrc(path.c_str());
    ft->setCCCId("sh010_balance");
    auto cpu = config->getProcessor(ft)->getDefaultCPUProcessor();

    float px[3] = {0.3f, 0.4f, 0.5f};
    cpu->applyRGB(px);

    const auto expected = grade_sidecar::apply_grade_maths({0.3f, 0.4f, 0.5f}, l1);
    EXPECT_NEAR(px[0], expected[0], 1e-4);
    EXPECT_NEAR(px[1], expected[1], 1e-4);
    EXPECT_NEAR(px[2], expected[2], 1e-4);
}

TEST(VideoRenderGradeSidecar, CubeWithoutOcioConfig) {

    grade_sidecar::ClipGrade clip;
    clip.clip_name = "plate";
    grade_sidecar::GradeLayer l;
    l.exposure  = 1.0;
    clip.layers = {l};

    std::vector<std::string> warnings;
    const auto cube = grade_sidecar::bake_cube(clip, 5, warnings);
    ASSERT_FALSE(cube.empty());
    EXPECT_EQ(warnings.size(), 1u); // no OCIO config: works on raw values

    std::istringstream in(cube);
    std::string line;
    std::vector<std::array<float, 3>> rows;
    bool size_ok = false;
    while (std::getline(in, line)) {
        if (line.rfind("LUT_3D_SIZE 5", 0) == 0)
            size_ok = true;
        if (line.empty() || line[0] == '#' || !(std::isdigit(line[0]) || line[0] == '-'))
            continue;
        std::istringstream ls(line);
        std::array<float, 3> v{};
        ls >> v[0] >> v[1] >> v[2];
        rows.push_back(v);
    }
    EXPECT_TRUE(size_ok);
    ASSERT_EQ(rows.size(), 125u);
    // red changes fastest: row 1 is r=0.25, g=0, b=0 -> doubled
    EXPECT_NEAR(rows[1][0], 0.5f, 1e-5);
    EXPECT_NEAR(rows[1][1], 0.0f, 1e-5);
    // last row is white -> 2.0
    EXPECT_NEAR(rows.back()[2], 2.0f, 1e-5);

    // masked and inactive layers give no LUT
    clip.layers[0].masked = true;
    EXPECT_TRUE(grade_sidecar::bake_cube(clip, 5, warnings).empty());
}

TEST(VideoRenderGradeSidecar, CubeInAcesLogSpace) {

#if OCIO_VERSION_HEX >= 0x02020000
    OCIO::ConstConfigRcPtr config;
    const char *builtin = "ocio://cg-config-v1.0.0_aces-v1.3_ocio-v2.1";
    try {
        config = OCIO::Config::CreateFromFile(builtin);
        if (!config->getColorSpace("compositing_log"))
            GTEST_SKIP() << "built in config has no compositing_log role";
    } catch (const std::exception &e) {
        GTEST_SKIP() << "no built in OCIO config: " << e.what();
    }

    grade_sidecar::ClipGrade clip;
    clip.clip_name     = "aces";
    clip.colour_params = json{{"ocio_config", builtin}};
    grade_sidecar::GradeLayer l;
    l.exposure  = 1.0;
    clip.layers = {l};

    std::vector<std::string> warnings;
    const int n     = 33;
    const auto cube = grade_sidecar::bake_cube(clip, n, warnings);
    ASSERT_FALSE(cube.empty());
    EXPECT_TRUE(warnings.empty());
    EXPECT_NE(cube.find("OCIO role compositing_log"), std::string::npos);

    // The compositing_log space of this config is ACEScct: one stop is
    // 1/17.52 in log code values (above the toe).
    std::istringstream in(cube);
    std::string line;
    int idx = 0;
    while (std::getline(in, line)) {
        if (line.empty() || !(std::isdigit(line[0]) || line[0] == '-'))
            continue;
        const int r = idx % n;
        const int g = (idx / n) % n;
        const int b = idx / (n * n);
        if (r == g && g == b && r >= 12 && r <= 24) {
            std::istringstream ls(line);
            float v[3];
            ls >> v[0] >> v[1] >> v[2];
            const float input = r / float(n - 1);
            EXPECT_NEAR(v[0] - input, 1.0f / 17.52f, 2e-3) << "lattice point " << r;
        }
        idx++;
    }
    EXPECT_EQ(idx, n * n * n);
#else
    GTEST_SKIP() << "OCIO older than 2.2 has no built in configs";
#endif
}

namespace {

std::vector<std::array<float, 3>> cube_rows(const std::string &cube) {
    std::vector<std::array<float, 3>> rows;
    std::istringstream in(cube);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || !(std::isdigit(line[0]) || line[0] == '-'))
            continue;
        std::istringstream ls(line);
        std::array<float, 3> v{};
        ls >> v[0] >> v[1] >> v[2];
        rows.push_back(v);
    }
    return rows;
}

} // namespace

TEST(VideoRenderGradeSidecar, PerChannelValues) {

    // newer grades carry optional per channel trims and a layer name
    json bm;
    bm["actors"]["x"] = grade_bookmark(
        media_a,
        1,
        json{
            {"exposure", 0.5},
            {"exposure_rgb", {0.5, 0.0, -0.5}},
            {"contrast_rgb", {1.0, 1.0, 1.0}},
            {"sat_rgb", {0.0, 1.0, 2.0}},
            {"contrast_rgb_bad", "x"}},
        json{{"layer_name", "old_name"}});
    bm["actors"]["x"]["base"]["annotation"]["Data"]["layer_name"] = "skin";
    const auto layers = grade_sidecar::grade_layers_for_media(bm, media_a);
    ASSERT_EQ(layers.size(), 1u);
    const auto &l = layers[0];
    EXPECT_EQ(l.name, "skin");
    EXPECT_DOUBLE_EQ(l.effective_exposure()[0], 1.0);
    EXPECT_DOUBLE_EQ(l.effective_exposure()[2], 0.0);
    EXPECT_DOUBLE_EQ(l.effective_sat()[2], 2.0);
    EXPECT_FALSE(l.is_identity());

    // maths: red gets one stop, blue none; red saturation 0 -> luma
    grade_sidecar::GradeLayer e;
    e.exposure_rgb = {1.0, 0.0, 0.0};
    auto r         = grade_sidecar::apply_grade_maths({0.1f, 0.2f, 0.4f}, e);
    EXPECT_FLOAT_EQ(r[0], 0.2f);
    EXPECT_FLOAT_EQ(r[1], 0.2f);
    EXPECT_FLOAT_EQ(r[2], 0.4f);

    grade_sidecar::GradeLayer s;
    s.sat_rgb        = {0.0, 1.0, 1.0};
    r                = grade_sidecar::apply_grade_maths({0.1f, 0.2f, 0.4f}, s);
    const float luma = 0.1f * 0.2126f + 0.2f * 0.7152f + 0.4f * 0.0722f;
    EXPECT_NEAR(r[0], luma, 1e-5);
    EXPECT_NEAR(r[1], 0.2f, 1e-6);

    // CDL: per channel exposure folds into the slope, per channel
    // saturation does not fit
    auto cdl = grade_sidecar::layer_to_cdl(e);
    EXPECT_DOUBLE_EQ(cdl.slope[0], 2.0);
    EXPECT_DOUBLE_EQ(cdl.slope[1], 1.0);
    EXPECT_TRUE(cdl.exact);
    EXPECT_FALSE(grade_sidecar::layer_to_cdl(s).exact);
}

TEST(VideoRenderGradeSidecar, CubeWithLogWorkingSpace) {

#if OCIO_VERSION_HEX >= 0x02020000
    const char *builtin = "ocio://cg-config-v1.0.0_aces-v1.3_ocio-v2.1";
    try {
        auto config = OCIO::Config::CreateFromFile(builtin);
        if (!config->getColorSpace("compositing_log"))
            GTEST_SKIP() << "built in config has no compositing_log role";
    } catch (const std::exception &e) {
        GTEST_SKIP() << "no built in OCIO config: " << e.what();
    }

    // a linear exposure layer, then an offset applied in log
    grade_sidecar::ClipGrade clip;
    clip.clip_name     = "mixed";
    clip.colour_params = json{{"ocio_config", builtin}};
    grade_sidecar::GradeLayer lin;
    lin.exposure = 1.0;
    grade_sidecar::GradeLayer log;
    log.colour_space = "compositing_log";
    log.offset       = {0.05, 0.05, 0.05, 0.0};
    clip.layers      = {lin, log};

    std::vector<std::string> warnings;
    const int n     = 17;
    const auto rows = cube_rows(grade_sidecar::bake_cube(clip, n, warnings));
    ASSERT_EQ(rows.size(), size_t(n * n * n));
    EXPECT_TRUE(warnings.empty());

    for (int i = 6; i <= 11; ++i) {
        const auto &v     = rows[i + i * n + i * n * n];
        const float input = i / float(n - 1);
        EXPECT_NEAR(v[1] - input, 1.0f / 17.52f + 0.05f, 2e-3) << "lattice point " << i;
    }

    // a working space the config doesn't have is reported, not fatal
    clip.layers[1].colour_space = "no such space";
    warnings.clear();
    EXPECT_EQ(cube_rows(grade_sidecar::bake_cube(clip, 5, warnings)).size(), 125u);
    EXPECT_EQ(warnings.size(), 1u);
#else
    GTEST_SKIP() << "OCIO older than 2.2 has no built in configs";
#endif
}

// Our own main: the gtest_main library is not on the runtime library path of
// the build tree (other xSTUDIO tests get their main from ACTOR_TEST_MINIMAL).
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
