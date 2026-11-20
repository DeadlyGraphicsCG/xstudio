// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "xstudio/ui/keyboard.hpp"

#include "../src/grading_hotkeys_logic.h"

using namespace xstudio::colour_pipeline::grading_hotkeys;

TEST(GradingHotkeysTest, StepLayerIndexNoLayers) {
    EXPECT_EQ(step_layer_index(-1, 0, 1), -1);
    EXPECT_EQ(step_layer_index(-1, 0, -1), -1);
    EXPECT_EQ(step_layer_index(0, 0, 1), -1);
}

TEST(GradingHotkeysTest, StepLayerIndexNothingSelected) {
    // next picks the first layer, previous picks the last one
    EXPECT_EQ(step_layer_index(-1, 3, 1), 0);
    EXPECT_EQ(step_layer_index(-1, 3, -1), 2);
    // an out of range selection behaves like no selection
    EXPECT_EQ(step_layer_index(7, 3, 1), 0);
    EXPECT_EQ(step_layer_index(7, 3, -1), 2);
}

TEST(GradingHotkeysTest, StepLayerIndexWrapsAround) {
    EXPECT_EQ(step_layer_index(0, 3, 1), 1);
    EXPECT_EQ(step_layer_index(1, 3, 1), 2);
    EXPECT_EQ(step_layer_index(2, 3, 1), 0);
    EXPECT_EQ(step_layer_index(0, 3, -1), 2);
    EXPECT_EQ(step_layer_index(2, 3, -1), 1);
    // single layer stays selected
    EXPECT_EQ(step_layer_index(0, 1, 1), 0);
    EXPECT_EQ(step_layer_index(0, 1, -1), 0);
    // bigger steps and no step
    EXPECT_EQ(step_layer_index(1, 3, 5), 0);
    EXPECT_EQ(step_layer_index(1, 3, -5), 2);
    EXPECT_EQ(step_layer_index(1, 3, 0), 1);
}

TEST(GradingHotkeysTest, IndexOf) {
    const std::vector<std::string> v = {"a", "b", "c"};
    EXPECT_EQ(index_of(v, std::string("a")), 0);
    EXPECT_EQ(index_of(v, std::string("c")), 2);
    EXPECT_EQ(index_of(v, std::string("x")), -1);
}

TEST(GradingHotkeysTest, SortLayersByCreated) {
    using tp = std::chrono::system_clock::time_point;
    const tp t0{};
    // layer name -> optional creation time
    std::vector<std::pair<std::string, std::optional<tp>>> data = {
        {"third", t0 + std::chrono::seconds(30)},
        {"no_time_1", std::nullopt},
        {"first", t0 + std::chrono::seconds(10)},
        {"no_time_2", std::nullopt},
        {"second", t0 + std::chrono::seconds(20)}};

    std::vector<std::string> layers;
    for (const auto &d : data)
        layers.push_back(d.first);

    sort_layers_by_created(layers, [&data](const std::string &name) {
        for (const auto &d : data)
            if (d.first == name)
                return d.second;
        return std::optional<tp>();
    });

    const std::vector<std::string> expected = {
        "first", "second", "third", "no_time_1", "no_time_2"};
    EXPECT_EQ(layers, expected);
}

TEST(GradingHotkeysTest, DefaultHotkeysAreValidAndUnique) {

    // Default key sequences used elsewhere in xSTUDIO (C++ and QML hotkeys).
    // The grading defaults must not reuse any of them.
    const std::vector<std::string> taken = {
        "Ctrl+B",
        "Ctrl+Z",
        "Ctrl+Shift+Z",
        "R",
        "G",
        "B",
        "A",
        "C",
        "Ctrl+L",
        "Ctrl+R",
        "E",
        "Y",
        "Alt+S",
        "D",
        "Ctrl+D",
        "V",
        "Ctrl+V",
        "Q",
        "Z",
        "X",
        "F",
        "Shift+F",
        "H",
        "F7",
        "Space Bar",
        "L",
        "J",
        "K",
        "P",
        "Ctrl+Shift+P",
        "I",
        "O",
        "Left",
        "Right",
        "Up",
        "Down",
        "Home",
        "End",
        "Ctrl+Up",
        "Ctrl+Down",
        ",",
        ".",
        "U",
        "N",
        "Tab",
        "Escape",
        ";",
        "/",
        "Alt+F",
        "Ctrl+H",
        "Alt+N",
        "Alt+P",
        "Alt+L",
        "Alt+R",
        "Alt+Up",
        "Alt+Down",
        "Shift+Up",
        "Shift+Down",
        "Shift+C",
        "Delete",
        "Ctrl+Left",
        "Ctrl+Right",
        "Ctrl+M",
        "Meta+S",
        "Ctrl+U",
        "PageUp",
        "PageDown",
        "Ctrl+A",
        "Ctrl+C",
        "Ctrl+N",
        "Ctrl+O",
        "Ctrl+S",
        "Ctrl+Shift+S",
        "Ctrl+Q",
        "Ctrl+,",
        "Ctrl+F",
        "Shift+?",
        "1"};

    std::set<std::pair<int, int>> taken_codes;
    for (const auto &seq : taken) {
        int key = 0, mod = 0;
        xstudio::ui::Hotkey::sequence_to_key_and_modifier(seq, key, mod);
        taken_codes.insert({key, mod});
    }

    std::set<std::pair<int, int>> ours;
    std::set<std::string> names;
    for (const auto &hk : default_hotkeys()) {
        int key = 0, mod = 0;
        EXPECT_NO_THROW(
            xstudio::ui::Hotkey::sequence_to_key_and_modifier(hk.sequence, key, mod))
            << hk.sequence;
        EXPECT_GT(key, 0) << hk.sequence;
        EXPECT_EQ(taken_codes.count({key, mod}), 0u) << hk.name << " " << hk.sequence;
        EXPECT_TRUE(ours.insert({key, mod}).second) << "duplicate default " << hk.sequence;
        EXPECT_TRUE(names.insert(hk.name).second) << "duplicate name " << hk.name;
    }
    EXPECT_EQ(default_hotkeys().size(), 4u);
    EXPECT_STREQ(hotkey_category(), "Grading");
}
