#include <doctest.h>

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "asset_io.h"

namespace {

// The converted-asset library lives outside the repo, so the environment must
// name it; tests fail with this message rather than skipping silently. A
// relative value is the documented `<repo>/out/library` form, resolved against
// the repo root — ctest runs the tests from `app/build`, not the repo root.
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        // __FILE__ is <repo>/app/tests/test_asset_io.cpp (CMake passes absolute paths).
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        library = repo_root / library;
    }
    return library;
}

// A scratch directory that deletes itself, holding the missing-PNG fixture.
class TempFixtureDir {
public:
    TempFixtureDir()
        : path_(std::filesystem::temp_directory_path() /
                ("sotc-asset-io-fixture-" + std::to_string(::getpid()))) {
        std::error_code ec;
        std::filesystem::create_directories(path_, ec);
        if (ec) {
            throw std::runtime_error("cannot create fixture dir " + path_.string() + ": " +
                                     ec.message());
        }
    }
    ~TempFixtureDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);  // best effort
    }
    TempFixtureDir(const TempFixtureDir&) = delete;
    TempFixtureDir& operator=(const TempFixtureDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string base64_encode(const uint8_t* data, std::size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0;
        const uint32_t b2 = (i + 2 < size) ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < size ? kAlphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < size ? kAlphabet[triple & 0x3F] : '=');
    }
    return out;
}

// A minimal glTF: one triangle in a data-URI buffer (POSITION only — no NORMAL,
// no TEXCOORD_0, no indices) and one material whose base-color PNG is absent.
std::string fixture_gltf_json() {
    const float positions[3][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    const std::string buffer_uri =
        "data:application/octet-stream;base64," +
        base64_encode(reinterpret_cast<const uint8_t*>(positions), sizeof positions);

    std::string json = R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "%%BUFFER_URI%%", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                 "min": [0, 0, 0], "max": [1, 1, 0]}],
  "images": [{"uri": "missing.png"}],
  "textures": [{"source": 0}],
  "materials": [{"name": "fixture", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";
    const std::string placeholder = "%%BUFFER_URI%%";
    json.replace(json.find(placeholder), placeholder.size(), buffer_uri);
    return json;
}

}  // namespace

TEST_CASE("wander glb matches the converter catalog") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    CHECK(scene.meshes.size() == 12);
    long verts = 0;
    for (auto& m : scene.meshes) verts += (long)m.vertices.size();
    CHECK(verts == 62466);
    CHECK(scene.materials.size() == 12);
    CHECK(scene.textures.size() == 21);
    float extentY = scene.boundsMax.y - scene.boundsMin.y;
    CHECK(extentY > 1.0f);
    CHECK(extentY < 1.5f);  // adapter notes: wander ≈ 1.26 tall in Y
}

TEST_CASE("every texture referenced by wander resolves on disk") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    for (auto& t : scene.textures)
        CHECK(std::filesystem::exists(test_library() / t.sourceRelPath));
}

TEST_CASE("plank converts to the pinned counts") {
    auto scene = load_scene(test_library(), "models/objects/props/_cmn/obj_plank_01.glb");
    CHECK(scene.meshes.size() == 1);
    CHECK(scene.meshes[0].vertices.size() == 980);
}

TEST_CASE("a missing base-color PNG degrades to factor-only") {
    TempFixtureDir dir;
    {
        std::ofstream out(dir.path() / "fixture.gltf");
        REQUIRE(out.good());
        out << fixture_gltf_json();
    }

    auto scene = load_scene(dir.path(), "fixture.gltf");

    // The absent PNG is skipped; the material falls back to its factors.
    CHECK(scene.textures.empty());
    REQUIRE(scene.materials.size() == 1);
    CHECK(scene.materials[0].baseColorTexture == -1);

    // The primitive has no NORMAL, no TEXCOORD_0 and no indices: the loader
    // defaults them (Y-up normal, zero UV) and generates 0..n-1 indices.
    REQUIRE(scene.meshes.size() == 1);
    REQUIRE(scene.meshes[0].vertices.size() == 3);
    const std::vector<uint32_t> expected_indices = {0, 1, 2};
    CHECK(scene.meshes[0].indices == expected_indices);
    CHECK(scene.meshes[0].vertices[0].ny == doctest::Approx(1.0f));
    CHECK(scene.meshes[0].vertices[0].u == doctest::Approx(0.0f));
    CHECK(scene.meshes[0].vertices[0].v == doctest::Approx(0.0f));
    CHECK(scene.meshes[0].vertices[2].py == doctest::Approx(1.0f));
}

TEST_CASE("a model that cannot be parsed throws") {
    const std::filesystem::path library = test_library();  // throws loudly when unset
    CHECK_THROWS_AS(load_scene(library, "models/objects/props/_cmn/no_such_model.glb"),
                    std::runtime_error);
}
