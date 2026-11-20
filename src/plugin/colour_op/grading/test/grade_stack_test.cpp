// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <OpenColorIO/OpenColorIO.h> //NOLINT

#include "grade_stack.h"
#include "grading_data.h"
#include "grading_data_serialiser.hpp"

using namespace xstudio;
using namespace xstudio::ui::viewport;
using namespace xstudio::ui::viewport::grade_stack;

namespace fs   = std::filesystem;
namespace OCIO = OCIO_NAMESPACE;

namespace {

utility::JsonStore make_grade_json(
    double slope, double offset, double power, double exposure = 0.0, double contrast = 1.0) {
    GradingData gd;
    gd.grade().slope    = {slope, slope, slope, 1.0};
    gd.grade().offset   = {offset, offset, offset, 0.0};
    gd.grade().power    = {power, power, power, 1.0};
    gd.grade().exposure = exposure;
    gd.grade().contrast = contrast;
    return GradingDataSerialiser::serialise(&gd);
}

Profile make_profile() {
    Profile p;
    p.id          = "6a1b2c3d-1111-4222-8333-444455556666";
    p.name        = "Show Look";
    p.description = "Neutral balance + warm";
    p.position    = StackPosition::Below;

    ProfileLayer a;
    a.layer_id     = "6a1b2c3d-0000-4000-8000-000000000001";
    a.name         = "Balance";
    a.enabled      = true;
    a.grading_data = make_grade_json(1.2, 0.01, 1.1);
    a.user_data    = utility::JsonStore(nlohmann::json{{"mask_active", false}});

    ProfileLayer b;
    b.layer_id     = "6a1b2c3d-0000-4000-8000-000000000002";
    b.name         = "Warm";
    b.enabled      = false;
    b.grading_data = make_grade_json(0.9, 0.0, 1.0, 0.5, 1.3);
    b.user_data    = utility::JsonStore(nlohmann::json::object());

    p.layers = {a, b};
    return p;
}

utility::JsonStore tagged(
    const std::string &profile_id,
    const std::string &layer_id,
    const std::string &position = "below",
    bool active                 = true) {
    nlohmann::json j;
    j["grade_active"] = active;
    j["layer_name"]   = "x";
    j[STACK_TAG_KEY]  = {
        {"profile_id", profile_id},
        {"profile_name", "P"},
        {"layer_id", layer_id},
        {"layer_name", "x"},
        {"position", position},
        {"profile_enabled", true}};
    return utility::JsonStore(j);
}

utility::sys_time_point t(int ms) {
    return utility::sys_time_point(std::chrono::milliseconds(1700000000000LL + ms));
}

bookmark::BookmarkDetail make_detail(
    std::optional<utility::JsonStore> ud, std::optional<utility::sys_time_point> created) {
    bookmark::BookmarkDetail d;
    d.user_data_ = ud;
    d.created_   = created;
    return d;
}

fs::path temp_dir() {
    auto dir = fs::temp_directory_path() /
               ("grade_stack_test_" + utility::to_string(utility::Uuid::generate()));
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST(GradeStackProfile, JsonRoundTrip) {
    const auto p = make_profile();
    const auto j = profile_to_json(p);

    EXPECT_EQ(j["schema"], PROFILE_SCHEMA);
    EXPECT_EQ(j["version"], PROFILE_VERSION);

    // through text, as on disk
    const auto back = profile_from_json(nlohmann::json::parse(j.dump()));

    EXPECT_EQ(back.id, p.id);
    EXPECT_EQ(back.name, p.name);
    EXPECT_EQ(back.description, p.description);
    EXPECT_EQ(back.position, p.position);
    ASSERT_EQ(back.layers.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(back.layers[i].layer_id, p.layers[i].layer_id);
        EXPECT_EQ(back.layers[i].name, p.layers[i].name);
        EXPECT_EQ(back.layers[i].enabled, p.layers[i].enabled);
        EXPECT_EQ(back.layers[i].user_data, p.layers[i].user_data);
        EXPECT_TRUE(
            GradingData(back.layers[i].grading_data) == GradingData(p.layers[i].grading_data));
    }
}

TEST(GradeStackProfile, MaskSurvivesRoundTrip) {
    GradingData gd;
    gd.grade().sat = 0.5;
    gd.mask().start_quad(
        utility::ColourTriplet(1.0f, 1.0f, 1.0f),
        {Imath::V2f(-0.5, -0.5),
         Imath::V2f(-0.5, 0.5),
         Imath::V2f(0.5, 0.5),
         Imath::V2f(0.5, -0.5)});
    gd.mask().end_draw();
    ASSERT_FALSE(gd.mask().empty());

    auto p                   = make_profile();
    p.layers[0].grading_data = GradingDataSerialiser::serialise(&gd);

    const auto back = profile_from_json(nlohmann::json::parse(profile_to_json(p).dump()));
    GradingData loaded(back.layers[0].grading_data);
    EXPECT_FALSE(loaded.mask().empty());
    EXPECT_TRUE(loaded == gd);
}

TEST(GradeStackProfile, FileRoundTripAndFileName) {
    EXPECT_EQ(profile_file_name("Show Look"), "Show_Look.xsgrade.json");
    EXPECT_EQ(profile_file_name("a/b\\c:d"), "a_b_c_d.xsgrade.json");
    EXPECT_EQ(profile_file_name("  "), "profile.xsgrade.json");
    EXPECT_EQ(profile_file_name("Look v2-final"), "Look_v2-final.xsgrade.json");

    const auto dir  = temp_dir();
    const auto path = (dir / "sub" / profile_file_name("Show Look")).string();
    const auto p    = make_profile();
    save_profile_file(p, path); // also creates the missing sub folder
    const auto back = load_profile_file(path);
    EXPECT_EQ(back.name, p.name);
    EXPECT_EQ(back.layers.size(), p.layers.size());
    EXPECT_FALSE(fs::exists(path + ".tmp"));

    fs::remove_all(dir);
}

TEST(GradeStackProfile, RejectsBadFiles) {
    EXPECT_THROW(profile_from_json(nlohmann::json::array()), std::runtime_error);
    EXPECT_THROW(profile_from_json(nlohmann::json{{"schema", "other"}}), std::runtime_error);

    auto j           = nlohmann::json(profile_to_json(make_profile()));
    auto newer       = j;
    newer["version"] = PROFILE_VERSION + 1;
    EXPECT_THROW(profile_from_json(newer), std::runtime_error);

    auto no_layers = j;
    no_layers.erase("layers");
    EXPECT_THROW(profile_from_json(no_layers), std::runtime_error);

    auto bad_grade                         = j;
    bad_grade["layers"][1]["grading_data"] = {{"nonsense", 1}};
    try {
        profile_from_json(bad_grade);
        FAIL() << "expected an error";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Layer 2"), std::string::npos) << e.what();
    }

    EXPECT_THROW(
        load_profile_file("/this/file/does/not/exist.xsgrade.json"), std::runtime_error);
}

TEST(GradeStackProfile, MinimalAndFutureFieldsAccepted) {
    // a hand written file: no id, no layer ids, no position, extra keys
    nlohmann::json j;
    j["schema"]       = PROFILE_SCHEMA;
    j["name"]         = "Hand made";
    j["future_thing"] = {1, 2, 3};
    j["layers"]       = nlohmann::json::array();
    j["layers"].push_back({{"grading_data", make_grade_json(1.1, 0.0, 1.0)}, {"extra", true}});
    j["layers"].push_back({{"grading_data", make_grade_json(1.0, 0.1, 1.0)}});

    const auto a = profile_from_json(j);
    const auto b = profile_from_json(j);
    EXPECT_EQ(a.position, StackPosition::Below);
    EXPECT_FALSE(utility::Uuid(a.id).is_null());
    // ids are stable, so importing the same file twice syncs the same layers
    EXPECT_EQ(a.id, b.id);
    ASSERT_EQ(a.layers.size(), 2u);
    EXPECT_EQ(a.layers[0].layer_id, b.layers[0].layer_id);
    EXPECT_NE(a.layers[0].layer_id, a.layers[1].layer_id);
    EXPECT_EQ(a.layers[0].name, "Layer 1");
    EXPECT_TRUE(a.layers[0].enabled);
}

TEST(GradeStackProfile, OldGradingDataStillLoads) {
    // GradingData as written by older xSTUDIO builds: no colour_space, no
    // exposure / contrast keys.
    nlohmann::json old_grade = {
        {"Grading Serialiser Version", 256},
        {"Data",
         {{"grade",
           {{"slope", {1.1, 1.0, 0.9, 1.0}},
            {"offset", {0.0, 0.0, 0.0, 0.0}},
            {"power", {1.0, 1.0, 1.0, 1.0}},
            {"sat", 0.8}}},
          {"mask_active", false},
          {"mask_editing", false},
          {"mask", GradingData().mask()}}}};

    nlohmann::json j;
    j["schema"] = PROFILE_SCHEMA;
    j["name"]   = "Old";
    j["layers"] = {{{"grading_data", old_grade}}};

    const auto p = profile_from_json(j);
    GradingData gd(p.layers[0].grading_data);
    EXPECT_DOUBLE_EQ(gd.grade().slope[0], 1.1);
    EXPECT_DOUBLE_EQ(gd.grade().sat, 0.8);
    EXPECT_DOUBLE_EQ(gd.grade().exposure, 0.0);
    EXPECT_DOUBLE_EQ(gd.grade().contrast, 1.0);
}

TEST(GradeStackOrder, UntaggedLayersKeepCreatedOrder) {
    // old sessions: no tags at all -> plain created time order, as before
    auto a = make_detail(utility::JsonStore(nlohmann::json{{"grade_active", true}}), t(10));
    auto b = make_detail(std::nullopt, t(20));
    EXPECT_TRUE(layer_less(a, b));
    EXPECT_FALSE(layer_less(b, a));
    EXPECT_FALSE(layer_less(a, a));

    // missing created time sorts first, and the ordering stays strict
    auto c = make_detail(std::nullopt, std::nullopt);
    EXPECT_TRUE(layer_less(c, a));
    EXPECT_FALSE(layer_less(a, c));
    EXPECT_FALSE(layer_less(c, c));
}

TEST(GradeStackOrder, BelowShotAbove) {
    const std::string pid = "6a1b2c3d-1111-4222-8333-444455556666";
    // created times deliberately "wrong" to show the tag wins
    auto below = make_detail(tagged(pid, "l1", "below"), t(500));
    auto shot  = make_detail(utility::JsonStore(nlohmann::json::object()), t(100));
    auto above = make_detail(tagged(pid, "l2", "above"), t(0));

    EXPECT_EQ(order_group(below.user_data_), 0);
    EXPECT_EQ(order_group(shot.user_data_), 1);
    EXPECT_EQ(order_group(above.user_data_), 2);

    std::vector<bookmark::BookmarkDetail> v = {above, shot, below};
    std::stable_sort(v.begin(), v.end(), layer_less);
    EXPECT_EQ(order_group(v[0].user_data_), 0);
    EXPECT_EQ(order_group(v[1].user_data_), 1);
    EXPECT_EQ(order_group(v[2].user_data_), 2);
}

TEST(GradeStackSync, FirstApplyCreatesAll) {
    const auto p                    = make_profile();
    std::vector<ExistingLayer> shot = {
        {utility::Uuid::generate(), utility::JsonStore(nlohmann::json::object()), t(0)}};
    const auto plan = plan_apply(p, shot);
    EXPECT_EQ(plan.create, (std::vector<size_t>{0, 1}));
    EXPECT_TRUE(plan.update.empty());
    EXPECT_TRUE(plan.remove.empty()); // the shot's own layer is untouched
}

TEST(GradeStackSync, ReapplyUpdatesInPlace) {
    const auto p                    = make_profile();
    const auto own                  = utility::Uuid::generate();
    const auto l1                   = utility::Uuid::generate();
    const auto l2                   = utility::Uuid::generate();
    const auto other                = utility::Uuid::generate();
    std::vector<ExistingLayer> shot = {
        {own, utility::JsonStore(nlohmann::json{{"layer_name", "mine"}}), t(0)},
        {l2, tagged(p.id, p.layers[1].layer_id), t(1)},
        {l1, tagged(p.id, p.layers[0].layer_id), t(2)},
        // a layer from a different profile is never touched
        {other, tagged("ffffffff-1111-4222-8333-444455556666", p.layers[0].layer_id), t(3)}};

    const auto plan = plan_apply(p, shot);
    EXPECT_TRUE(plan.create.empty());
    EXPECT_TRUE(plan.remove.empty());
    ASSERT_EQ(plan.update.size(), 2u);
    EXPECT_EQ(plan.update[0], std::make_pair(l1, size_t(0)));
    EXPECT_EQ(plan.update[1], std::make_pair(l2, size_t(1)));
}

TEST(GradeStackSync, RemovedAndDuplicateLayers) {
    auto p            = make_profile();
    const auto old_l2 = p.layers[1].layer_id;
    p.layers.pop_back(); // layer 2 was taken out of the profile

    const auto l1                   = utility::Uuid::generate();
    const auto dup                  = utility::Uuid::generate();
    const auto l2                   = utility::Uuid::generate();
    std::vector<ExistingLayer> shot = {
        {l1, tagged(p.id, p.layers[0].layer_id), t(0)},
        {dup, tagged(p.id, p.layers[0].layer_id), t(1)},
        {l2, tagged(p.id, old_l2), t(2)}};

    const auto plan = plan_apply(p, shot);
    EXPECT_TRUE(plan.create.empty());
    ASSERT_EQ(plan.update.size(), 1u);
    EXPECT_EQ(plan.update[0].first, l1);
    EXPECT_EQ(plan.remove.size(), 2u);
    EXPECT_NE(std::find(plan.remove.begin(), plan.remove.end(), dup), plan.remove.end());
    EXPECT_NE(std::find(plan.remove.begin(), plan.remove.end(), l2), plan.remove.end());

    EXPECT_EQ(layers_of_profile(p.id, shot).size(), 3u);
    EXPECT_TRUE(layers_of_profile("nope", shot).empty());
}

TEST(GradeStackSync, UserDataTagAndOnOffChoice) {
    const auto p = make_profile();

    // new layer: follows the profile
    auto ud  = make_stack_user_data(p, p.layers[0], StackPosition::Above);
    auto tag = stack_tag(ud);
    ASSERT_TRUE(tag);
    EXPECT_EQ(tag->profile_id, p.id);
    EXPECT_EQ(tag->layer_id, p.layers[0].layer_id);
    EXPECT_EQ(tag->layer_name, "Balance");
    EXPECT_EQ(tag->position, StackPosition::Above);
    EXPECT_EQ(ud["layer_name"], "[Show Look] Balance");
    EXPECT_EQ(ud["grade_active"], true);
    EXPECT_EQ(ud["mask_active"], false);

    // the shot switched the layer off; profile unchanged -> stays off
    auto shot_off            = ud;
    shot_off["grade_active"] = false;
    auto synced = make_stack_user_data(p, p.layers[0], StackPosition::Above, shot_off);
    EXPECT_EQ(synced["grade_active"], false);

    // profile switched the layer off since -> the profile wins
    auto p2              = p;
    p2.layers[0].enabled = false;
    auto shot_on         = ud;
    synced = make_stack_user_data(p2, p2.layers[0], StackPosition::Above, shot_on);
    EXPECT_EQ(synced["grade_active"], false);
}

TEST(GradeStackSync, LayerFromBookmark) {
    const auto p    = make_profile();
    const auto json = make_grade_json(1.0, 0.0, 1.0);

    // a shot layer from this profile keeps its id and plain name
    auto ud    = make_stack_user_data(p, p.layers[0], StackPosition::Below);
    auto layer = layer_from_bookmark(json, ud, p.id, 0);
    EXPECT_EQ(layer.layer_id, p.layers[0].layer_id);
    EXPECT_EQ(layer.name, "Balance");
    EXPECT_FALSE(layer.user_data.contains(STACK_TAG_KEY));
    EXPECT_FALSE(layer.user_data.contains("grade_active"));

    // a plain shot layer gets a fresh id
    auto plain = layer_from_bookmark(
        json,
        utility::JsonStore(nlohmann::json{{"layer_name", "Key"}, {"grade_active", false}}),
        p.id,
        3);
    EXPECT_FALSE(utility::Uuid(plain.layer_id).is_null());
    EXPECT_NE(plain.layer_id, p.layers[0].layer_id);
    EXPECT_EQ(plain.name, "Key");
    EXPECT_FALSE(plain.enabled);

    // no user data at all (very old layers)
    auto bare = layer_from_bookmark(json, std::nullopt, p.id, 1);
    EXPECT_EQ(bare.name, "Grade Layer 2");
    EXPECT_TRUE(bare.enabled);
}

TEST(GradeStackSync, CreatedTimesMatchPlacement) {
    std::vector<ExistingLayer> others = {
        {utility::Uuid::generate(), std::nullopt, t(1000)},
        {utility::Uuid::generate(), std::nullopt, t(5000)},
        {utility::Uuid::generate(), std::nullopt, std::nullopt}};

    const auto below = stack_created_times(StackPosition::Below, others, 3, t(9000));
    ASSERT_EQ(below.size(), 3u);
    EXPECT_LT(below[0], below[1]);
    EXPECT_LT(below[1], below[2]);
    EXPECT_LT(below[2], t(1000));

    const auto above = stack_created_times(StackPosition::Above, others, 3, t(2000));
    ASSERT_EQ(above.size(), 3u);
    EXPECT_LT(above[0], above[1]);
    EXPECT_LT(above[1], above[2]);
    EXPECT_GT(above[0], t(5000));

    // no other layers: around 'now'
    const auto alone = stack_created_times(StackPosition::Above, {}, 2, t(3000));
    EXPECT_EQ(alone[0], t(3000));
    EXPECT_EQ(alone[1], t(3001));
}

TEST(GradeStackCdl, CccReadableByOcio) {
    auto p              = make_profile();
    p.layers[1].enabled = true; // export both layers

    const auto dir   = temp_dir();
    const auto path  = (dir / "stack.ccc").string();
    const auto notes = write_profile_cdl(p, path);

    // layer 2 has contrast, which CDL can't hold - this must be reported
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_NE(notes[0].find("Contrast"), std::string::npos);

    auto config = OCIO::Config::CreateRaw();
    auto apply  = [&](const char *ccc_id, float v) {
        auto ft = OCIO::FileTransform::Create();
        ft->setSrc(path.c_str());
        ft->setCCCId(ccc_id);
        auto cpu     = config->getProcessor(ft)->getDefaultCPUProcessor();
        float rgb[3] = {v, v, v};
        cpu->applyRGB(rgb);
        return rgb[0];
    };

    // layer 1: (0.5 * 1.2 + 0.01) ^ 1.1
    EXPECT_NEAR(apply("Balance", 0.5f), std::pow(0.61, 1.1), 1e-4);
    // layer 2: exposure 0.5 stop folded into slope 0.9
    EXPECT_NEAR(apply("Warm", 0.25f), 0.25 * 0.9 * std::pow(2.0, 0.5), 1e-4);

    fs::remove_all(dir);
}

TEST(GradeStackCdl, SkipsSwitchedOffAndChecksFormat) {
    const auto p   = make_profile(); // layer 2 is switched off
    const auto dir = temp_dir();

    const auto notes = write_profile_cdl(p, (dir / "one.cdl").string());
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_NE(notes[0].find("switched off"), std::string::npos);

    EXPECT_THROW(write_profile_cdl(p, (dir / "x.txt").string()), std::runtime_error);

    auto all_off              = p;
    all_off.layers[0].enabled = false;
    EXPECT_THROW(write_profile_cdl(all_off, (dir / "x.ccc").string()), std::runtime_error);

    fs::remove_all(dir);
}
