// SPDX-License-Identifier: Apache-2.0

#include "export_profiles.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <set>

#include <fmt/format.h>

namespace fs = std::filesystem;

namespace xstudio::video_render_plugin_1_0::export_profiles {

namespace {

    const std::set<std::string> frame_based_containers{
        "jpg", "jpeg", "tiff", "tif", "png", "bmp"};

    std::string clean_container(std::string c) {
        while (!c.empty() &&
               (c.front() == '.' || std::isspace(static_cast<unsigned char>(c.front()))))
            c.erase(c.begin());
        while (!c.empty() && std::isspace(static_cast<unsigned char>(c.back())))
            c.pop_back();
        std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return c;
    }

    std::string trimmed(const std::string &s) {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return std::string();
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

} // namespace

nlohmann::json default_export_profile() {
    return nlohmann::json{
        {"name", ""},
        {"video_preset", "H.264 (Film)"},
        {"container", "mov"},
        {"resolution", ""},
        {"frame_rate", ""},
        {"ocio_display", ""},
        {"ocio_view", ""},
        {"bake_grade", true},
        {"sidecar_cdl", false},
        {"sidecar_lut", false},
        {"output_folder", ""},
        {"naming_template", "{media_name}_{version}"},
        {"include_annotations", false},
        {"render_audio", true}};
}

nlohmann::json normalise_export_profile(const nlohmann::json &profile) {

    nlohmann::json result = default_export_profile();
    if (!profile.is_object())
        return result;

    // keep unknown keys as they are
    for (auto it = profile.begin(); it != profile.end(); ++it) {
        if (!result.contains(it.key()))
            result[it.key()] = it.value();
    }

    // known keys: only take values of the expected type
    for (auto it = result.begin(); it != result.end(); ++it) {
        if (!profile.contains(it.key()))
            continue;
        const auto &v = profile.at(it.key());
        if (it.value().is_boolean() && v.is_boolean()) {
            it.value() = v;
        } else if (it.value().is_string() && v.is_string()) {
            it.value() = v;
        } else if (it.value().is_string() && v.is_number()) {
            // e.g. a frame rate written as a number
            it.value() = v.dump();
        }
    }

    result["name"]      = trimmed(result["name"].get<std::string>());
    result["container"] = clean_container(result["container"].get<std::string>());
    if (result["container"].get<std::string>().empty())
        result["container"] = default_export_profile()["container"];
    if (trimmed(result["naming_template"].get<std::string>()).empty())
        result["naming_template"] = default_export_profile()["naming_template"];

    return result;
}

nlohmann::json normalise_export_profiles(const nlohmann::json &profiles) {

    nlohmann::json result = nlohmann::json::array();
    if (!profiles.is_array())
        return result;

    std::set<std::string> names;
    for (const auto &p : profiles) {
        if (!p.is_object())
            continue;
        auto n          = normalise_export_profile(p);
        const auto name = n["name"].get<std::string>();
        if (name.empty() || names.count(name))
            continue;
        names.insert(name);
        result.push_back(n);
    }
    return result;
}

int find_export_profile(const nlohmann::json &profiles, const std::string &name) {
    if (!profiles.is_array())
        return -1;
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto &p = profiles[i];
        if (p.is_object() && p.contains("name") && p["name"].is_string() &&
            p["name"].get<std::string>() == name)
            return static_cast<int>(i);
    }
    return -1;
}

bool save_export_profile(nlohmann::json &profiles, const nlohmann::json &profile) {

    auto n = normalise_export_profile(profile);
    if (n["name"].get<std::string>().empty())
        return false;

    if (!profiles.is_array())
        profiles = nlohmann::json::array();

    const int idx = find_export_profile(profiles, n["name"].get<std::string>());
    if (idx >= 0)
        profiles[idx] = n;
    else
        profiles.push_back(n);
    return true;
}

bool rename_export_profile(
    nlohmann::json &profiles,
    const std::string &old_name,
    const std::string &new_name,
    std::string &error) {

    const auto name = trimmed(new_name);
    const int idx   = find_export_profile(profiles, old_name);
    if (idx < 0) {
        error = fmt::format("There is no export profile called \"{}\".", old_name);
        return false;
    }
    if (name.empty()) {
        error = "The new profile name is empty.";
        return false;
    }
    if (name != old_name && find_export_profile(profiles, name) >= 0) {
        error = fmt::format("An export profile called \"{}\" already exists.", name);
        return false;
    }
    profiles[idx]["name"] = name;
    return true;
}

bool delete_export_profile(nlohmann::json &profiles, const std::string &name) {
    const int idx = find_export_profile(profiles, name);
    if (idx < 0)
        return false;
    profiles.erase(profiles.begin() + idx);
    return true;
}

bool is_frame_based_container(const std::string &container) {
    return frame_based_containers.count(clean_container(container)) != 0;
}

std::string
profile_output_path(const nlohmann::json &profile, const std::string &fallback_folder) {

    const auto p = normalise_export_profile(profile);

    std::string folder = p["output_folder"].get<std::string>();
    if (trimmed(folder).empty())
        folder = fallback_folder;

    std::string name      = p["naming_template"].get<std::string>();
    const std::string ext = p["container"].get<std::string>();

    // let people type the extension into the template too
    const std::string dot_ext = "." + ext;
    if (name.size() > dot_ext.size() &&
        name.compare(name.size() - dot_ext.size(), dot_ext.size(), dot_ext) == 0) {
        name.resize(name.size() - dot_ext.size());
    }

    if (is_frame_based_container(ext) && name.find('#') == std::string::npos)
        name += ".####";

    std::string result;
    if (!folder.empty()) {
        result = folder;
        if (result.back() != '/' && result.back() != '\\')
            result += "/";
    }
    result += name + dot_ext;
    return result;
}

std::string sanitise_token_value(const std::string &value) {

    static const std::string bad_chars = "/\\:*?\"<>|";

    std::string result;
    bool last_was_space = false;
    for (const char c : trimmed(value)) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) {
            if (!last_was_space)
                result += '_';
            last_was_space = true;
            continue;
        }
        last_was_space = false;
        if (uc < 32 || bad_chars.find(c) != std::string::npos)
            result += '_';
        else
            result += c;
    }
    // never produce "." or ".." (these mean folders)
    if (!result.empty() && result.find_first_not_of('.') == std::string::npos)
        result = std::string(result.size(), '_');
    return result;
}

std::map<std::string, std::string>
clock_tokens(const std::chrono::system_clock::time_point &when) {

    const std::time_t t = std::chrono::system_clock::to_time_t(when);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::map<std::string, std::string> result;
    result["date"] =
        fmt::format("{:04d}{:02d}{:02d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    result["time"] = fmt::format("{:02d}{:02d}{:02d}", tm.tm_hour, tm.tm_min, tm.tm_sec);
    result["year"] = fmt::format("{:04d}", tm.tm_year + 1900);
    return result;
}

bool has_tokens(const std::string &tmpl) {
    const auto open = tmpl.find('{');
    return open != std::string::npos && tmpl.find('}', open) != std::string::npos;
}

std::string expand_naming_template(
    const std::string &tmpl, const std::map<std::string, std::string> &tokens, int version) {

    std::string result;
    size_t pos = 0;
    while (pos < tmpl.size()) {
        const auto open = tmpl.find('{', pos);
        if (open == std::string::npos) {
            result += tmpl.substr(pos);
            break;
        }
        const auto close = tmpl.find('}', open);
        if (close == std::string::npos) {
            result += tmpl.substr(pos);
            break;
        }
        result += tmpl.substr(pos, open - pos);
        const std::string name = tmpl.substr(open + 1, close - open - 1);

        auto p = tokens.find(name);
        if (p != tokens.end()) {
            result += sanitise_token_value(p->second);
        } else if (name == "version") {
            result += fmt::format("v{:03d}", std::max(version, 0));
        } else {
            // unknown token: keep it so the user can see the mistake
            result += tmpl.substr(open, close - open + 1);
        }
        pos = close + 1;
    }
    return result;
}

std::string expand_naming_template_next_version(
    const std::string &tmpl,
    const std::map<std::string, std::string> &tokens,
    const std::function<bool(const std::string &)> &exists,
    int *version_used) {

    int v = 1;
    if (tmpl.find("{version}") != std::string::npos && !tokens.count("version") && exists) {
        // 9999 versions should be enough for anyone ...
        while (v < 10000 && exists(expand_naming_template(tmpl, tokens, v)))
            v++;
    }
    if (version_used)
        *version_used = v;
    return expand_naming_template(tmpl, tokens, v);
}

bool output_path_exists(const std::string &path) {

    try {
        const auto hash = path.find('#');
        if (hash == std::string::npos)
            return fs::exists(fs::path(path));

        // frame sequence: look for any file that starts with the part of the
        // file name before the frame number
        const fs::path p(path);
        const fs::path folder    = p.has_parent_path() ? p.parent_path() : fs::path(".");
        const std::string fname  = p.filename().string();
        const std::string prefix = fname.substr(0, fname.find('#'));
        if (!fs::is_directory(folder))
            return false;
        for (const auto &entry : fs::directory_iterator(folder)) {
            const auto n = entry.path().filename().string();
            if (n.size() > prefix.size() && n.compare(0, prefix.size(), prefix) == 0)
                return true;
        }
    } catch (...) {
    }
    return false;
}

} // namespace xstudio::video_render_plugin_1_0::export_profiles
