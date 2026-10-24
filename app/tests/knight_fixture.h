#pragma once

// KNIGHT's exported graph as a test fixture (S12 Tasks 5 and 9): tools/re/scr/graph_export.py
// runs once per test run into the build directory from out/re/eboot.elf (a case fails, never
// skips, without it); nothing derived is in git.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_reader.h"

namespace knight_fixture {

namespace fs = std::filesystem;

inline const fs::path kRepoRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();

inline std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// KNIGHT's export, generated once per run into the build directory.
inline const std::string& knight_json() {
    static const std::string json = [] {
        const fs::path out = fs::path(SOTC_TEST_BUILD_DIR) / "fixtures" / "KNIGHT.graph.json";
        fs::create_directories(out.parent_path());
        const std::string cmd = "python3 -I '" + (kRepoRoot / "tools/re/scr/graph_export.py").string() +
                                "' export KNIGHT '" + out.string() + "'";
        if (std::system(cmd.c_str()) != 0) {
            throw std::runtime_error("graph_export.py failed (is out/re/eboot.elf there?): " + cmd);
        }
        return read_file(out);
    }();
    return json;
}

// Every clip stem the export names (motion-graph ends and {clip} arguments), sorted.
inline std::vector<std::string> export_clips(const std::string& json) {
    std::set<std::string> names;
    std::function<void(const Json&)> walk = [&](const Json& v) {
        if (v.kind == Json::Object) {
            for (const auto& [key, child] : v.object) {
                if ((key == "clip" || key == "from" || key == "to") && child.kind == Json::String) {
                    names.insert(child.string);
                }
                walk(child);
            }
        } else if (v.kind == Json::Array) {
            for (const Json& child : v.array) walk(child);
        }
    };
    walk(json_parse(json));
    return {names.begin(), names.end()};
}

}  // namespace knight_fixture
