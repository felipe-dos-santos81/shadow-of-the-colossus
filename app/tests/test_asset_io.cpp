#include <doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "asset_io.h"
#include "anim.h"
#include "heightmap.h"
#include "player.h"

namespace {

// Wander's S2-S6 clips, by name, in the order the app indexes them. Later
// milestones append (S7 adds the wake clips), so the pin is "at least these
// fifteen, first, in this order" rather than an exact count.
constexpr const char* kWanderS2S6Clips[15] = {
    "player_a_stand", "player_a_walk", "player_a_run", "player_a_grip_stand_crouch",
    "player_a_grip_loop", "player_a_grip_move_f1", "player_a_grip_move_f2",
    "player_a_grip_float_u_hi_attack", "player_a_atk1", "player_a_damage_m_fly2",
    "player_a_fall_damage_to_death", "player_a_damage_h_fly1", "player_a_damage_h_fly_end",
    "player_a_fall_damage_start", "player_a_fall_damage_end"};

template <typename Clips>
void check_wander_s2s6_clips(const Clips& clips) {
    REQUIRE(clips.size() >= 15);
    for (std::size_t i = 0; i < 15; ++i) CHECK(clips[i].name == kWanderS2S6Clips[i]);
}

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

// A scratch directory that deletes itself, holding a fixture library. `suffix`
// lets one test own two sibling directories (e.g. a texture outside the library).
class TempFixtureDir {
public:
    explicit TempFixtureDir(const std::string& suffix = "")
        : path_(std::filesystem::temp_directory_path() /
                ("sotc-asset-io-fixture-" + std::to_string(::getpid()) + suffix)) {
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
// no TEXCOORD_0, no indices) and one material whose base-color PNG is `image_uri`,
// relative to the model's directory. A non-empty `emissive_uri` adds a second
// image/texture and the material's `emissiveTexture` (the veins carrier).
std::string fixture_gltf_json(const std::string& image_uri = "missing.png",
                              const std::string& emissive_uri = "",
                              int emissive_index = 1) {
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
  "images": [{"uri": "%%IMAGE_URI%%"}%%IMAGES_MORE%%],
  "textures": [{"source": 0}%%TEXTURES_MORE%%],
  "materials": [{"name": "fixture",
                 "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}%%MATERIAL_MORE%%}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";
    const std::string buffer_placeholder = "%%BUFFER_URI%%";
    json.replace(json.find(buffer_placeholder), buffer_placeholder.size(), buffer_uri);
    const std::string image_placeholder = "%%IMAGE_URI%%";
    json.replace(json.find(image_placeholder), image_placeholder.size(), image_uri);

    // The optional second slot: image 1 + texture 1, bound as `emissiveTexture`.
    const std::string images_more = emissive_uri.empty()
                                        ? ""
                                        : R"(, {"uri": ")" + emissive_uri + R"("})";
    const std::string textures_more = emissive_uri.empty() ? "" : R"(, {"source": 1})";
    const std::string material_more =
        emissive_uri.empty()
            ? ""
            : R"(, "emissiveTexture": {"index": )" + std::to_string(emissive_index) + "}";
    json.replace(json.find("%%IMAGES_MORE%%"), std::string("%%IMAGES_MORE%%").size(), images_more);
    json.replace(json.find("%%TEXTURES_MORE%%"), std::string("%%TEXTURES_MORE%%").size(),
                 textures_more);
    json.replace(json.find("%%MATERIAL_MORE%%"), std::string("%%MATERIAL_MORE%%").size(),
                 material_more);
    return json;
}

// --- The skin/animation fixture ----------------------------------------------

// The fixture's binary buffer, assembled in code — the S1 pattern, through
// `base64_encode` above — so every value stays reviewable instead of hiding in
// a pasted blob. Layout (little-endian, like glTF; every offset 4-byte aligned):
//
//   offset  bytes  contents
//   0       36     POSITION  3 x float3  (0,2,0) (1,2,0) (0,1,0)
//   36      24     JOINTS_0  3 x ushort4 (1,0,0,0)
//   60      48     WEIGHTS_0 3 x float4  (1,0,0,0)
//   108     128    IBM       2 x mat4    translate(0,-1,0), translate(0,-2,0)
//   236     8      input     2 x float   [0, 1]
//   244     32     output    2 x float4  (0,0,0,1), (0,0,sin 45deg,cos 45deg)
//   276 total
std::string skin_fixture_buffer_uri() {
    std::vector<uint8_t> bytes;
    const auto append_floats = [&bytes](const float* values, std::size_t count) {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(values);
        bytes.insert(bytes.end(), raw, raw + count * sizeof(float));
    };
    const auto append_u16s = [&bytes](const uint16_t* values, std::size_t count) {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(values);
        bytes.insert(bytes.end(), raw, raw + count * sizeof(uint16_t));
    };

    const float positions[3][3] = {{0.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    append_floats(&positions[0][0], 9);
    const uint16_t joints[3][4] = {{1, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}};
    append_u16s(&joints[0][0], 12);
    const float weights[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}};
    append_floats(&weights[0][0], 12);
    // Inverse bind matrices: the inverse of each joint's bind world transform
    // ("Root" is identity, JointA sits at y = 1, JointB at y = 2).
    const float inverse_bind[2][16] = {
        {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f,
         0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -2.0f,
         0.0f, 1.0f}};
    append_floats(&inverse_bind[0][0], 32);
    const float input[2] = {0.0f, 1.0f};
    append_floats(input, 2);
    const float output[2][4] = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)}};  // 90 degrees about Z
    append_floats(&output[0][0], 8);

    REQUIRE(bytes.size() == 276);
    return "data:application/octet-stream;base64," + base64_encode(bytes.data(), bytes.size());
}

// The skin fixture: nodes Root -> JointA -> JointB (the joints carry their rest
// translation), a skin over [JointA, JointB] with inverse bind matrices, one
// triangle bound to joint 1, and an animation "turn" rotating JointA 90 degrees
// about Z over one second.
//
// Variants: `skinned = false` drops JOINTS_0/WEIGHTS_0 from the primitive (the
// loader must default them); `extra_channel` adds a channel targeting the
// non-joint node "Helper"; `interpolation` selects the sampler's interpolation;
// `matrix_joint` gives JointB an equivalent `matrix` node instead of TRS.
struct SkinFixtureOptions {
    std::string interpolation = "LINEAR";
    bool skinned = true;
    bool extra_channel = false;
    bool matrix_joint = false;
};
std::string skin_fixture_gltf_json(const SkinFixtureOptions& options = {}) {
    std::string json = R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "%%BUFFER_URI%%", "byteLength": 276}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 24},
    {"buffer": 0, "byteOffset": 60, "byteLength": 48},
    {"buffer": 0, "byteOffset": 108, "byteLength": 128},
    {"buffer": 0, "byteOffset": 236, "byteLength": 8},
    {"buffer": 0, "byteOffset": 244, "byteLength": 32}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0, 1, 0], "max": [1, 2, 0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "VEC4"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR"},
    {"bufferView": 5, "componentType": 5126, "count": 2, "type": "VEC4"}
  ],
  "meshes": [{"primitives": [{"attributes": %%ATTRIBUTES%%}]}],
  "skins": [{"joints": [1, 2], "inverseBindMatrices": 3}],
  "nodes": [
    {"name": "Root", "children": [1]},
    {"name": "JointA", "translation": [0, 1, 0], "children": [2]},
    %%JOINTB_NODE%%,
    {"name": "Helper"},
    {"name": "Skinned", "mesh": 0, "skin": 0}
  ],
  "scenes": [{"nodes": [0, 4]}],
  "scene": 0,
  "animations": [{"name": "turn", "channels": %%CHANNELS%%,
                  "samplers": [{"input": 4, "output": 5, "interpolation": "%%INTERPOLATION%%"}]}]
}
)";
    const auto replace_once = [&json](const std::string& placeholder, const std::string& value) {
        const std::size_t at = json.find(placeholder);
        REQUIRE(at != std::string::npos);
        json.replace(at, placeholder.size(), value);
    };

    replace_once("%%BUFFER_URI%%", skin_fixture_buffer_uri());
    replace_once("%%ATTRIBUTES%%",
                 options.skinned ? R"({"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2})"
                                 : R"({"POSITION": 0})");
    // JointB's node, either as TRS or as the equivalent column-major matrix
    // T(0,2,0) * R(90 deg Z) * S(2,2,2).
    replace_once(
        "%%JOINTB_NODE%%",
        options.matrix_joint
            ? R"({"name": "JointB", "matrix": [0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 1]})"
            : R"({"name": "JointB", "translation": [0, 1, 0]})");
    std::string channels = R"({"sampler": 0, "target": {"node": 1, "path": "rotation"}})";
    if (options.extra_channel) {
        channels += R"(, {"sampler": 0, "target": {"node": 3, "path": "translation"}})";
    }
    replace_once("%%CHANNELS%%", "[" + channels + "]");
    replace_once("%%INTERPOLATION%%", options.interpolation);
    return json;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    REQUIRE(out.good());
    out << text;
}

// A 1x1 red PNG (69 bytes), generated offline: valid enough for stb_image.
const unsigned char kPng1x1Red[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
    0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0xc9, 0xfe, 0x92, 0xef, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void write_binary(const std::filesystem::path& path, const unsigned char* data,
                  std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

// One placed instance with an identity rotation and unit scale, at `position`:
// the shape the compose tests place (W1.2).
ManifestInstance placed_at(const std::string& glbRelPath, Vec3 position) {
    ManifestInstance instance{};
    instance.glbRelPath = glbRelPath;
    instance.position = position;
    instance.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    instance.scale = Vec3{1.0f, 1.0f, 1.0f};
    return instance;
}

// The composed material at `composedIndex` must be the standalone glb's own
// material: same name and the same base-color/normal PNG paths. A wrong but
// in-range index offset passes a range check — this mapping check is what
// catches it (it is how the player material-offset bug was found).
void check_material_matches(const Scene& composed, int composedIndex, const Scene& source,
                            int sourceIndex) {
    // REQUIRE (not CHECK) so an out-of-range index aborts the case instead of
    // reading past the material list. One comparison per assertion: doctest
    // refuses compound expressions.
    REQUIRE(composedIndex >= 0);
    REQUIRE(static_cast<std::size_t>(composedIndex) < composed.materials.size());
    REQUIRE(sourceIndex >= 0);
    REQUIRE(static_cast<std::size_t>(sourceIndex) < source.materials.size());
    const MaterialCpu& composedMaterial =
        composed.materials[static_cast<std::size_t>(composedIndex)];
    const MaterialCpu& sourceMaterial = source.materials[static_cast<std::size_t>(sourceIndex)];
    CHECK(composedMaterial.name == sourceMaterial.name);

    const auto texture_path = [](const Scene& scene, int textureIndex) {
        if (textureIndex < 0) {
            return std::string("<none>");
        }
        if (static_cast<std::size_t>(textureIndex) >= scene.textures.size()) {
            return std::string("<out of range>");
        }
        return scene.textures[static_cast<std::size_t>(textureIndex)].sourceRelPath;
    };
    CHECK(texture_path(composed, composedMaterial.baseColorTexture) ==
          texture_path(source, sourceMaterial.baseColorTexture));
    CHECK(texture_path(composed, composedMaterial.normalTexture) ==
          texture_path(source, sourceMaterial.normalTexture));
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

TEST_CASE("image URIs that escape the library are rejected") {
    TempFixtureDir dir;
    TempFixtureDir outside("-outside");
    // A real, decodable PNG outside the fixture library: without the guard the
    // loader would read it and record a `..`-containing sourceRelPath, violating
    // the library-relative invariant and D-S4 (converted assets only).
    write_binary(outside.path() / "texture.png", kPng1x1Red, sizeof kPng1x1Red);
    const std::filesystem::path outside_png = outside.path() / "texture.png";

    for (const std::string& uri :
         {std::string("../" + outside.path().filename().string() + "/texture.png"),
          std::filesystem::weakly_canonical(outside_png).string()}) {
        {
            std::ofstream out(dir.path() / "fixture.gltf");
            REQUIRE(out.good());
            out << fixture_gltf_json(uri);
        }

        CAPTURE(uri);
        auto scene = load_scene(dir.path(), "fixture.gltf");
        CHECK(scene.textures.empty());
        REQUIRE(scene.materials.size() == 1);
        CHECK(scene.materials[0].baseColorTexture == -1);
    }
}

TEST_CASE("an emissive texture resolves to a scene texture") {
    TempFixtureDir dir;
    write_binary(dir.path() / "base.png", kPng1x1Red, sizeof kPng1x1Red);
    write_binary(dir.path() / "veins.png", kPng1x1Red, sizeof kPng1x1Red);
    write_text(dir.path() / "fixture.gltf", fixture_gltf_json("base.png", "veins.png"));

    auto scene = load_scene(dir.path(), "fixture.gltf");

    REQUIRE(scene.materials.size() == 1);
    REQUIRE(!scene.textures.empty());
    const MaterialCpu& material = scene.materials[0];
    REQUIRE(material.emissiveTexture >= 0);
    REQUIRE(static_cast<std::size_t>(material.emissiveTexture) < scene.textures.size());
    CHECK(scene.textures[static_cast<std::size_t>(material.emissiveTexture)].sourceRelPath ==
          "veins.png");
    // It is a separate slot from the base color (image 1, not image 0).
    CHECK(material.baseColorTexture != material.emissiveTexture);
}

TEST_CASE("a material without an emissive texture has no emissive slot") {
    TempFixtureDir dir;
    write_text(dir.path() / "fixture.gltf", fixture_gltf_json());

    auto scene = load_scene(dir.path(), "fixture.gltf");

    REQUIRE(scene.materials.size() == 1);
    CHECK(scene.materials[0].emissiveTexture == -1);
}

TEST_CASE("a dangling emissive image degrades to no emissive slot") {
    // The image is declared by the material but absent on disk (the converter's refused-texture
    // case): the base image still loads, the emissive resolves to -1, and the renderer's white
    // fallback keeps the draw total.
    TempFixtureDir dir;
    write_binary(dir.path() / "base.png", kPng1x1Red, sizeof kPng1x1Red);
    write_text(dir.path() / "fixture.gltf",
               fixture_gltf_json("base.png", "missing-veins.png"));

    auto scene = load_scene(dir.path(), "fixture.gltf");

    REQUIRE(scene.materials.size() == 1);
    CHECK(scene.materials[0].baseColorTexture >= 0);
    CHECK(scene.materials[0].emissiveTexture == -1);
    CHECK(scene.textures.size() == 1);
}

TEST_CASE("an emissive texture without an image degrades to no emissive slot") {
    // A valid texture view with no `source` (cgltf accepts it; only material texture *indexes*
    // are required to resolve): the loader's resolve_texture sees a null image and answers -1.
    TempFixtureDir dir;
    write_binary(dir.path() / "base.png", kPng1x1Red, sizeof kPng1x1Red);
    std::string json = fixture_gltf_json("base.png", "veins.png");
    const std::string sourced = R"({"source": 0}, {"source": 1})";
    const std::size_t at = json.find(sourced);
    REQUIRE(at != std::string::npos);
    json.replace(at, sourced.size(), R"({"source": 0}, {})");
    write_text(dir.path() / "fixture.gltf", json);

    auto scene = load_scene(dir.path(), "fixture.gltf");

    REQUIRE(scene.materials.size() == 1);
    CHECK(scene.materials[0].baseColorTexture >= 0);
    CHECK(scene.materials[0].emissiveTexture == -1);
}

TEST_CASE("an out-of-range emissive texture index is refused by the parser") {
    // cgltf requires every material texture index to resolve and refuses the whole document
    // ("invalid glTF") when it does not, so an out-of-range emissive index can never reach
    // MaterialCpu as a silent -1 or a stale index. The renderer's bounds fallback remains for
    // hand-built scenes; from a glb the failure is loud. (Found while hardening Task 3.)
    TempFixtureDir dir;
    write_binary(dir.path() / "base.png", kPng1x1Red, sizeof kPng1x1Red);
    write_binary(dir.path() / "veins.png", kPng1x1Red, sizeof kPng1x1Red);
    write_text(dir.path() / "fixture.gltf", fixture_gltf_json("base.png", "veins.png", 9));

    CHECK_THROWS_AS(load_scene(dir.path(), "fixture.gltf"), std::runtime_error);
}

TEST_CASE("wander carries its 348-joint skeleton") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    CHECK(player_rig(scene).skin.joints.size() == 348);
    CHECK(player_rig(scene).skin.joints[0].name == "Bone_root");
    // The S2 converter bakes the player's demo clips into the glb, named after
    // the source clip stem: the app plays these instead of the stand-in. S4.1
    // baked the climb set and S4.2 adds the fight set (15 clips total); the
    // clips are found by name — the way the app selects them.
    check_wander_s2s6_clips(player_rig(scene).animations);
    CHECK(player_rig(scene).animations[0].name == "player_a_stand");
    CHECK(player_rig(scene).animations[1].name == "player_a_walk");
    CHECK(player_rig(scene).animations[2].name == "player_a_run");
    CHECK(!player_rig(scene).animations[0].channels.empty());
    CHECK(!player_rig(scene).animations[1].channels.empty());
    CHECK(!player_rig(scene).animations[2].channels.empty());
    Playback playback;
    build_playback(scene, scene.playerRig, playback);
    CHECK(clip_index_named(playback, "player_a_grip_loop") >= 0);
    CHECK(clip_index_named(playback, "player_a_atk1") >= 0);
    // S4.2's fight set: the hurt reaction the combat player selects by name.
    CHECK(clip_index_named(playback, "player_a_damage_h_fly1") >= 0);
    const auto& v = scene.meshes[0].vertices[0];
    CHECK(v.j[0] == 102); CHECK(v.j[1] == 73); CHECK(v.j[2] == 0); CHECK(v.j[3] == 0);
    CHECK(v.w[0] == doctest::Approx(0.9094f).epsilon(1e-3));
    CHECK(v.w[1] == doctest::Approx(0.0906f).epsilon(1e-3));
}

TEST_CASE("agro carries its baked kick clip") {
    auto scene = load_scene(test_library(), "models/shadow/characters/agro/_cmn/agro.glb");
    REQUIRE(player_rig(scene).animations.size() >= 1);
    CHECK(player_rig(scene).animations[0].name == "horse_a_add_horse_kick");
    CHECK(!player_rig(scene).animations[0].channels.empty());
}

namespace {
// The 19 riding gaits/transitions, in the order the S8 bake appends them (Agro: after the kick).
constexpr const char* kRideStems[19] = {
    "stand1", "walk_start", "walk", "walk_stop", "run_n_start", "run_n", "run_n_stop",
    "run_n_from_walk", "run_start", "run", "run_from_run_n", "run_stop1", "run_stop2",
    "run_stop3", "back_start", "back", "back_stop", "neigh", "neigh_end"};
}

TEST_CASE("S8: agro carries the kick at 0 then the 19 riding clips, by name") {
    auto scene = load_scene(test_library(), "models/shadow/characters/agro/_cmn/agro.glb");
    const auto& clips = player_rig(scene).animations;
    REQUIRE(clips.size() == 20);
    CHECK(clips[0].name == "horse_a_add_horse_kick");
    for (int i = 0; i < 19; ++i) {
        CHECK(clips[1 + i].name == std::string("horse_a_uma_") + kRideStems[i]);
        CHECK(!clips[1 + i].channels.empty());
    }
}

TEST_CASE("S8/S9: wander carries the 26 ride clips at 17..42, then (S9 view) the six raise clips at 43..48") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    const auto& clips = player_rig(scene).animations;
    check_wander_s2s6_clips(clips);
    // The base library (main's bake) has 43; S9's library view has 49 — like the
    // audio slice case, accept either and pin the raise clips only where baked.
    const bool raised = clips.size() == 49;
    REQUIRE((clips.size() == 43 || raised));
    CHECK(clips[15].name == "player_a_home_boss01_cut04");
    CHECK(clips[16].name == "player_a_home_boss01_cut05");
    for (int i = 0; i < 19; ++i) {
        const std::string stem = i == 0 ? "stand1_player" : kRideStems[i];
        CHECK(clips[17 + i].name == "player_a_uma_" + stem);
    }
    const char* tail[7] = {"player_a_uma_stand_ride_l", "player_a_uma_stand_ride_r",
                           "player_a_uma_stand_getoff_l_start", "player_a_uma_stand_getoff_r_start",
                           "player_a_uma_stand_getoff_fall", "player_a_whistle",
                           "player_a_whistle_near"};
    for (int i = 0; i < 7; ++i) CHECK(clips[36 + i].name == tail[i]);
    // S9 (design D9): the raise clips are appended after S8's 43, indices kept.
    const char* raise_clips[6] = {"player_a_hoist_sword_start", "player_a_hoist_sword_loop",
                                  "player_a_hoist_sword_end", "player_a_uma_hoist_sword_start",
                                  "player_a_uma_hoist_sword_loop", "player_a_uma_hoist_sword_end"};
    if (raised) {
        for (int i = 0; i < 6; ++i) CHECK(clips[43 + i].name == raise_clips[i]);
    }
    for (std::size_t i = 17; i < clips.size(); ++i) CHECK(!clips[i].channels.empty());
}

TEST_CASE("S7: wander carries the wake clips after its S2-S6 set, mono her death loop") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    const auto& clips = player_rig(scene).animations;
    check_wander_s2s6_clips(clips);
    REQUIRE(clips.size() >= 17);
    CHECK(clips[15].name == "player_a_home_boss01_cut04");
    CHECK(clips[16].name == "player_a_home_boss01_cut05");
    CHECK(!clips[15].channels.empty());
    CHECK(!clips[16].channels.empty());

    auto mono = load_scene(test_library(), "models/shadow/characters/mono/_cmn/mono.glb");
    REQUIRE(player_rig(mono).animations.size() == 1);
    CHECK(player_rig(mono).animations[0].name == "girl_a_death_loop");
    CHECK(!player_rig(mono).animations[0].channels.empty());
}

TEST_CASE("wander's joint rest transforms decompose from their node matrices") {
    // The converter emits `matrix` nodes; the fixture below uses TRS nodes, so
    // this test is what exercises the matrix path on real data. The expected
    // values were derived outside this code (decomposing the glb's node
    // matrices independently).
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    REQUIRE(player_rig(scene).skin.joints.size() == 348);

    // Bone_root: an identity node matrix and no parent node at all.
    CHECK(player_rig(scene).skin.joints[0].parent == -1);
    CHECK(player_rig(scene).skin.joints[0].restT.y == doctest::Approx(0.0f));
    CHECK(player_rig(scene).skin.joints[0].restR.w == doctest::Approx(1.0f));
    CHECK(player_rig(scene).skin.joints[0].restS.x == doctest::Approx(1.0f));

    // Bone_HARA: a rotation about X (~80 degrees), parented to Bone_root.
    CHECK(player_rig(scene).skin.joints[1].name == "Bone_HARA");
    CHECK(player_rig(scene).skin.joints[1].parent == 0);
    CHECK(player_rig(scene).skin.joints[1].restR.x == doctest::Approx(0.6392f).epsilon(1e-3));
    CHECK(player_rig(scene).skin.joints[1].restR.y == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(player_rig(scene).skin.joints[1].restR.z == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(player_rig(scene).skin.joints[1].restR.w == doctest::Approx(0.7690f).epsilon(1e-3));

    // Bone_KOSI: a 180 degree rotation (w = 0), the branch that a trace-based
    // decomposition alone cannot recover.
    CHECK(player_rig(scene).skin.joints[2].name == "Bone_KOSI");
    CHECK(player_rig(scene).skin.joints[2].parent == 0);
    CHECK(player_rig(scene).skin.joints[2].restR.w == doctest::Approx(0.0f).epsilon(1e-3));

    // Bone_MOMO_L: a joint with a real translation, two levels down the tree.
    CHECK(player_rig(scene).skin.joints[20].name == "Bone_MOMO_L");
    CHECK(player_rig(scene).skin.joints[20].parent == 2);
    CHECK(player_rig(scene).skin.joints[20].restT.x == doctest::Approx(-0.06125f).epsilon(1e-3));
    CHECK(player_rig(scene).skin.joints[20].restT.y == doctest::Approx(0.018739f).epsilon(1e-3));
    CHECK(player_rig(scene).skin.joints[20].restT.z == doctest::Approx(-0.108982f).epsilon(1e-3));
}

TEST_CASE("a two-joint fixture parses skin and animation") {
    TempFixtureDir dir;
    write_text(dir.path() / "skin.gltf", skin_fixture_gltf_json());

    auto scene = load_scene(dir.path(), "skin.gltf");

    // The skin: "Root" is not a joint, so JointA is a root and JointB its child.
    // Both keep their local rest translation; the glTF defaults fill in the
    // rest rotation and scale.
    REQUIRE(player_rig(scene).skin.joints.size() == 2);
    CHECK(player_rig(scene).skin.joints[0].name == "JointA");
    CHECK(player_rig(scene).skin.joints[0].parent == -1);
    CHECK(player_rig(scene).skin.joints[1].name == "JointB");
    CHECK(player_rig(scene).skin.joints[1].parent == 0);
    CHECK(player_rig(scene).skin.joints[0].restT.y == doctest::Approx(1.0f));
    CHECK(player_rig(scene).skin.joints[0].restR.w == doctest::Approx(1.0f));
    CHECK(player_rig(scene).skin.joints[0].restS.y == doctest::Approx(1.0f));
    // Inverse bind matrices: the inverse of the joints' bind world transforms.
    CHECK(player_rig(scene).skin.joints[0].inverseBind.m[13] == doctest::Approx(-1.0f));
    CHECK(player_rig(scene).skin.joints[1].inverseBind.m[13] == doctest::Approx(-2.0f));
    // The bind world is composed parent-first from the rest chain (no matrix
    // inverse): JointA at y = 1, JointB one more unit up at y = 2.
    CHECK(player_rig(scene).skin.joints[0].bindWorld.m[13] == doctest::Approx(1.0f));
    CHECK(player_rig(scene).skin.joints[1].bindWorld.m[13] == doctest::Approx(2.0f));
    CHECK(player_rig(scene).skin.joints[1].bindWorld.m[12] == doctest::Approx(0.0f));
    // The rest chain and the inverse bind agree: bindWorld * inverseBind is the
    // identity, which is why palette * bindWorld is the posed joint.
    const Mat4 joint_identity =
        mat4_mul(player_rig(scene).skin.joints[1].bindWorld,
                 player_rig(scene).skin.joints[1].inverseBind);
    for (int k = 0; k < 16; ++k) {
        CAPTURE(k);
        CHECK(joint_identity.m[k] ==
              doctest::Approx((k % 5 == 0) ? 1.0f : 0.0f).epsilon(1e-5));
    }

    // The vertex carries its JOINTS_0/WEIGHTS_0 pair: joint 1, weight 1.
    REQUIRE(scene.meshes.size() == 1);
    REQUIRE(scene.meshes[0].vertices.size() == 3);
    CHECK(scene.meshes[0].vertices[0].j[0] == 1);
    CHECK(scene.meshes[0].vertices[0].j[1] == 0);
    CHECK(scene.meshes[0].vertices[0].w[0] == doctest::Approx(1.0f));
    CHECK(scene.meshes[0].vertices[0].w[1] == doctest::Approx(0.0f));

    // The clip: one LINEAR rotation channel on JointA, keys at 0 s and 1 s.
    REQUIRE(player_rig(scene).animations.size() == 1);
    const AnimationCpu& clip = player_rig(scene).animations[0];
    CHECK(clip.name == "turn");
    CHECK(clip.duration == doctest::Approx(1.0f));
    REQUIRE(clip.channels.size() == 1);
    const AnimationChannelCpu& channel = clip.channels[0];
    CHECK(channel.joint == 0);
    CHECK(channel.path == 1);
    CHECK(channel.interpolation == 0);
    const std::vector<float> expected_times = {0.0f, 1.0f};
    CHECK(channel.times == expected_times);
    CHECK(channel.values.size() == 8);
    // The second key's quaternion is 90 degrees about Z: (0, 0, sin 45, cos 45).
    CHECK(channel.values[6] == doctest::Approx(std::sin(kPi / 4.0f)).epsilon(1e-4));
    CHECK(channel.values[7] == doctest::Approx(std::cos(kPi / 4.0f)).epsilon(1e-4));
}

TEST_CASE("a joint node carrying a matrix decomposes into its rest TRS") {
    TempFixtureDir dir;
    write_text(dir.path() / "matrix.gltf", skin_fixture_gltf_json({.matrix_joint = true}));

    auto scene = load_scene(dir.path(), "matrix.gltf");

    // JointB's node is T(0,2,0) * R(90 degrees about Z) * S(2,2,2): the
    // translation is column 3, the scale the basis column lengths, the rotation
    // the normalized basis.
    REQUIRE(player_rig(scene).skin.joints.size() == 2);
    const JointCpu& joint = player_rig(scene).skin.joints[1];
    CHECK(joint.restT.x == doctest::Approx(0.0f));
    CHECK(joint.restT.y == doctest::Approx(2.0f));
    CHECK(joint.restT.z == doctest::Approx(0.0f));
    CHECK(joint.restS.x == doctest::Approx(2.0f));
    CHECK(joint.restS.y == doctest::Approx(2.0f));
    CHECK(joint.restS.z == doctest::Approx(2.0f));
    CHECK(joint.restR.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(joint.restR.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(joint.restR.z == doctest::Approx(std::sin(kPi / 4.0f)).epsilon(1e-4));
    CHECK(joint.restR.w == doctest::Approx(std::cos(kPi / 4.0f)).epsilon(1e-4));
    // The bind world composes that local matrix under JointA's T(0,1,0): the
    // child's bind origin is at (0,3,0), its basis columns scaled by 2.
    CHECK(joint.bindWorld.m[13] == doctest::Approx(3.0f).epsilon(1e-4));
    CHECK(joint.bindWorld.m[1] == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(joint.bindWorld.m[4] == doctest::Approx(-2.0f).epsilon(1e-4));
}

TEST_CASE("a primitive without JOINTS_0 defaults to zero weights (unskinned)") {
    TempFixtureDir dir;
    write_text(dir.path() / "unskinned.gltf", skin_fixture_gltf_json({.skinned = false}));

    auto scene = load_scene(dir.path(), "unskinned.gltf");

    // The model still has its skin — only the primitive lacks the attributes.
    // Zero weights are the "unskinned" signal the shader passes through
    // untransformed: full weight on joint 0 would instead select palette[0],
    // the player's animated root bone in a composed scene (F3).
    REQUIRE(player_rig(scene).skin.joints.size() == 2);
    REQUIRE(scene.meshes.size() == 1);
    REQUIRE(scene.meshes[0].vertices.size() == 3);
    for (const Vertex& vertex : scene.meshes[0].vertices) {
        for (int i = 0; i < 4; ++i) {
            CHECK(vertex.j[i] == 0);
            CHECK(vertex.w[i] == doctest::Approx(0.0f));
        }
    }
}

TEST_CASE("a channel targeting a non-joint node is skipped") {
    TempFixtureDir dir;
    write_text(dir.path() / "helper.gltf", skin_fixture_gltf_json({.extra_channel = true}));

    auto scene = load_scene(dir.path(), "helper.gltf");

    // "Helper" is not in skin.joints: its channel is dropped, the joint's stays.
    REQUIRE(player_rig(scene).animations.size() == 1);
    REQUIRE(player_rig(scene).animations[0].channels.size() == 1);
    CHECK(player_rig(scene).animations[0].channels[0].joint == 0);
    CHECK(player_rig(scene).animations[0].channels[0].path == 1);
}

TEST_CASE("a CUBICSPLINE sampler is refused loudly") {
    TempFixtureDir dir;
    write_text(dir.path() / "cubic.gltf", skin_fixture_gltf_json({.interpolation = "CUBICSPLINE"}));

    CHECK_THROWS_WITH_AS(load_scene(dir.path(), "cubic.gltf"),
                         doctest::Contains("CUBICSPLINE"), std::runtime_error);
}

TEST_CASE("a model that cannot be parsed throws") {
    const std::filesystem::path library = test_library();  // throws loudly when unset
    CHECK_THROWS_AS(load_scene(library, "models/objects/props/_cmn/no_such_model.glb"),
                    std::runtime_error);
}

// --- Composed scenes (W1.2) --------------------------------------------------
//
// The S3 fixtures: obj_plank_01.glb twice (at T(1,0,0) and T(0,0,2)) plus
// wander.glb as the player. The plank contributes 1 mesh / 1 material / 2
// textures, wander 12 / 12 / 21, so the composed totals below pin every index
// offset.

TEST_CASE("compose_scene places two plank instances and the player") {
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("models/objects/props/_cmn/obj_plank_01.glb",
                                           Vec3{1.0f, 0.0f, 0.0f}));
    manifest.instances.push_back(placed_at("models/objects/props/_cmn/obj_plank_01.glb",
                                           Vec3{0.0f, 0.0f, 2.0f}));

    const std::size_t instance_meshes = 2;
    std::size_t player_mesh_begin = 999;
    const Scene scene = compose_scene(test_library(), manifest,
                                      "models/shadow/characters/wander/_cmn/wander.glb",
                                      &player_mesh_begin);

    // 2 x plank (1 mesh each) + wander (12).
    REQUIRE(scene.meshes.size() == 14);

    // The player's meshes start after the instances' (W1.4's player-less
    // heightmap build splits on this index).
    CHECK(player_mesh_begin == instance_meshes);

    // Each instance mesh carries its instance's translation in column 3.
    CHECK(scene.meshes[0].model.m[12] == doctest::Approx(1.0f));
    CHECK(scene.meshes[0].model.m[13] == doctest::Approx(0.0f));
    CHECK(scene.meshes[0].model.m[14] == doctest::Approx(0.0f));
    CHECK(scene.meshes[1].model.m[12] == doctest::Approx(0.0f));
    CHECK(scene.meshes[1].model.m[13] == doctest::Approx(0.0f));
    CHECK(scene.meshes[1].model.m[14] == doctest::Approx(2.0f));

    // The player's meshes are appended with an identity model.
    for (std::size_t i = 2; i < scene.meshes.size(); ++i) {
        for (int k = 0; k < 16; ++k) {
            const float expected = (k % 5 == 0) ? 1.0f : 0.0f;
            CHECK(scene.meshes[i].model.m[k] == doctest::Approx(expected));
        }
    }

    // Materials and textures land at offset indices: every mesh's
    // materialIndex and every material's texture indices stay valid. Textures
    // are shared scene-wide by path (S7 task 11): the planks' 2 + wander's 21.
    CHECK(scene.materials.size() == 14);
    CHECK(scene.textures.size() == 23);
    for (const MeshCpu& mesh : scene.meshes) {
        REQUIRE(mesh.materialIndex >= 0);
        REQUIRE(static_cast<std::size_t>(mesh.materialIndex) < scene.materials.size());
        const MaterialCpu& material =
            scene.materials[static_cast<std::size_t>(mesh.materialIndex)];
        CHECK(material.baseColorTexture >= 0);
        CHECK(static_cast<std::size_t>(material.baseColorTexture) < scene.textures.size());
        CHECK(material.normalTexture >= -1);
        CHECK(material.normalTexture < static_cast<int>(scene.textures.size()));
    }

    // The mapping, not just the range: each mesh must resolve to its own glb's
    // material. The player's materials follow the instances', so a missing
    // player material offset would hand the player instance materials — an
    // in-range mistake only name/path equality can catch.
    const Scene plank = load_scene(test_library(), "models/objects/props/_cmn/obj_plank_01.glb");
    const Scene wander =
        load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    REQUIRE(plank.meshes.size() == 1);
    REQUIRE(wander.meshes.size() == 12);
    // The shared list is exactly the serial loads' textures, first-seen order,
    // with the same pixels.
    std::vector<const TextureCpu*> serial;
    for (const Scene* source : {&plank, &plank, &wander}) {
        for (const TextureCpu& texture : source->textures) {
            const bool seen = std::any_of(serial.begin(), serial.end(), [&](const TextureCpu* t) {
                return t->sourceRelPath == texture.sourceRelPath;
            });
            if (!seen) serial.push_back(&texture);
        }
    }
    REQUIRE(scene.textures.size() == serial.size());
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK(scene.textures[i].sourceRelPath == serial[i]->sourceRelPath);
        CHECK(scene.textures[i].rgba8 == serial[i]->rgba8);
    }
    CHECK(scene.meshes[0].materialIndex == 0);
    CHECK(scene.meshes[1].materialIndex == 1);
    check_material_matches(scene, 0, plank, plank.meshes[0].materialIndex);
    check_material_matches(scene, 1, plank, plank.meshes[0].materialIndex);
    for (std::size_t i = 0; i < wander.meshes.size(); ++i) {
        const int composed_index = scene.meshes[2 + i].materialIndex;
        CHECK(composed_index == 2 + wander.meshes[i].materialIndex);
        check_material_matches(scene, composed_index, wander, wander.meshes[i].materialIndex);
    }

    // The cull box is the raw vertex bounds, in the mesh's own space (the
    // translation lives in `model`, not in the box).
    for (const MeshCpu& mesh : scene.meshes) {
        REQUIRE_FALSE(mesh.vertices.empty());
        Vec3 lo{mesh.vertices[0].px, mesh.vertices[0].py, mesh.vertices[0].pz};
        Vec3 hi = lo;
        for (const Vertex& v : mesh.vertices) {
            lo = Vec3{std::min(lo.x, v.px), std::min(lo.y, v.py), std::min(lo.z, v.pz)};
            hi = Vec3{std::max(hi.x, v.px), std::max(hi.y, v.py), std::max(hi.z, v.pz)};
        }
        CHECK(mesh.localMin.x == lo.x);
        CHECK(mesh.localMin.y == lo.y);
        CHECK(mesh.localMin.z == lo.z);
        CHECK(mesh.localMax.x == hi.x);
        CHECK(mesh.localMax.y == hi.y);
        CHECK(mesh.localMax.z == hi.z);
    }
    CHECK(scene.meshes[0].localMin.x == scene.meshes[1].localMin.x);

    // The composed scene keeps the player's skeleton.
    CHECK(player_rig(scene).skin.joints.size() == 348);

    // Bounds are world-space: they cover both placed origins.
    CHECK(scene.boundsMin.x <= 1.0f);
    CHECK(scene.boundsMax.x >= 1.0f);
    CHECK(scene.boundsMin.z <= 2.0f);
    CHECK(scene.boundsMax.z >= 2.0f);
}

TEST_CASE("compose_scene bakes an instance's rotation and scale") {
    SceneManifest manifest{};
    ManifestInstance instance = placed_at("models/objects/props/_cmn/obj_plank_01.glb",
                                          Vec3{3.0f, 1.0f, 0.0f});
    instance.rotation = Quat{0.0f, std::sin(kPi / 4.0f), 0.0f, std::cos(kPi / 4.0f)};  // 90 deg Y
    instance.scale = Vec3{2.0f, 2.0f, 2.0f};
    manifest.instances.push_back(instance);

    const Scene scene = compose_scene(test_library(), manifest,
                                      "models/shadow/characters/wander/_cmn/wander.glb");

    REQUIRE(scene.meshes.size() == 13);
    const Mat4& model = scene.meshes[0].model;
    // mat4_from_trs: T(3,1,0) * R(90 deg about Y) * S(2,2,2), column-major.
    CHECK(model.m[0] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(model.m[2] == doctest::Approx(-2.0f).epsilon(1e-4));
    CHECK(model.m[5] == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(model.m[8] == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(model.m[10] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(model.m[12] == doctest::Approx(3.0f));
    CHECK(model.m[13] == doctest::Approx(1.0f));
    CHECK(model.m[14] == doctest::Approx(0.0f));

    // The bounds cover the placed vertices in world space, not the raw ones:
    // the plank's local Z extent, rotated onto X and doubled, sits at x = 3.
    const Scene plank = load_scene(test_library(), "models/objects/props/_cmn/obj_plank_01.glb");
    CHECK(scene.boundsMin.x <= doctest::Approx(3.0f + 2.0f * plank.boundsMin.z));
    CHECK(scene.boundsMax.x >= doctest::Approx(3.0f + 2.0f * plank.boundsMax.z));
}

TEST_CASE("compose_scene names the instance glb it cannot load") {
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("models/objects/props/_cmn/no_such_model.glb",
                                           Vec3{0.0f, 0.0f, 0.0f}));

    std::string message;
    try {
        compose_scene(test_library(), manifest,
                      "models/shadow/characters/wander/_cmn/wander.glb");
        FAIL("compose_scene did not throw for a missing instance glb");
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    CHECK(message.find("instance") != std::string::npos);
    CHECK(message.find("no_such_model.glb") != std::string::npos);
}

TEST_CASE("compose_scene with no instances composes the player alone") {
    SceneManifest manifest{};  // no instances: an empty loop, but a real path
    std::size_t player_mesh_begin = 999;
    const Scene scene = compose_scene(test_library(), manifest,
                                      "models/shadow/characters/wander/_cmn/wander.glb",
                                      &player_mesh_begin);
    const Scene wander =
        load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");

    // With no instances the player's meshes start at 0.
    CHECK(player_mesh_begin == 0);

    REQUIRE(scene.meshes.size() == wander.meshes.size());
    CHECK(scene.materials.size() == wander.materials.size());
    for (std::size_t i = 0; i < wander.meshes.size(); ++i) {
        CHECK(scene.meshes[i].materialIndex == wander.meshes[i].materialIndex);
        check_material_matches(scene, scene.meshes[i].materialIndex, wander,
                               wander.meshes[i].materialIndex);
        for (int k = 0; k < 16; ++k) {
            const float expected = (k % 5 == 0) ? 1.0f : 0.0f;
            CHECK(scene.meshes[i].model.m[k] == doctest::Approx(expected));
        }
    }
    CHECK(scene.boundsMin.x == doctest::Approx(wander.boundsMin.x));
    CHECK(scene.boundsMax.y == doctest::Approx(wander.boundsMax.y));
    CHECK(player_rig(scene).skin.joints.size() == 348);
}

TEST_CASE("a composed scene keeps the player's skeleton, not an instance's") {
    SceneManifest manifest{};
    // Wander placed as an instance: an instance is static, its skeleton and
    // clips are skipped (with a warning) while its meshes still compose.
    manifest.instances.push_back(placed_at("models/shadow/characters/wander/_cmn/wander.glb",
                                           Vec3{1.0f, 0.0f, 0.0f}));

    const Scene scene =
        compose_scene(test_library(), manifest, "models/objects/props/_cmn/obj_plank_01.glb");

    // The plank player is rig 0 — with no skeleton and no clips.
    REQUIRE(scene.rigs.size() == 1);
    CHECK(scene.rigs[0].skin.joints.empty());
    CHECK(scene.rigs[0].animations.empty());
    REQUIRE(scene.meshes.size() == 13);
    CHECK(scene.meshes[0].model.m[12] == doctest::Approx(1.0f));
}

// --- Entities: the second rig (S4) ------------------------------------------
//
// The encounter fixture: one plank instance (static ground), wander as the
// player, and the real colossus glb placed with the builder-pick transform from
// docs/notes/re4-placement.md §8, looping minotaur_a_stand. Library-gated like
// every other load test here.

namespace {

constexpr const char* kWanderGlb = "models/shadow/characters/wander/_cmn/wander.glb";
constexpr const char* kPlankGlb = "models/objects/props/_cmn/obj_plank_01.glb";
constexpr const char* kColossusGlb =
    "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb";
constexpr const char* kClubGlb =
    "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a_weapon.glb";

// Writes `text` as a manifest fixture and parses it, so the entity tests run
// through the real parser and the real composer, like the app does.
SceneManifest parsed_manifest(const TempFixtureDir& dir, const std::string& text) {
    const std::filesystem::path path = dir.path() / "encounter.manifest";
    write_text(path, text);
    return parse_manifest(path);
}

// The encounter manifest's entity line: the builder-pick transform, verbatim.
std::string colossus_entity_line() {
    return std::string("entity ") + kColossusGlb +
           " 382 28.369 370 0 -0.909104 0 0.416570 1 1 1 minotaur_a_stand\n";
}

}  // namespace

TEST_CASE("compose_scene loads an entity as a second rig") {
    TempFixtureDir dir;
    const SceneManifest manifest = parsed_manifest(
        dir, std::string("# encounter fixture\ninstance ") + kPlankGlb +
                 " 0 0 0 0 0 0 1 1 1 1\n" + colossus_entity_line());

    std::size_t player_mesh_begin = 999;
    const Scene scene = compose_scene(test_library(), manifest, kWanderGlb, &player_mesh_begin);

    // Load order: the static instance, then the player, then the entity. The
    // plank contributes 1 mesh, wander 12, the colossus 12 (S4 recon §1).
    REQUIRE(scene.meshes.size() == 25);
    REQUIRE(scene.rigs.size() == 2);
    CHECK(scene.playerRig == 0);
    CHECK(player_mesh_begin == 1);

    // The player's rig is rig 0 and dynamic; the entity's is rig 1.
    const RigCpu& player = scene.rigs[0];
    const RigCpu& entity = scene.rigs[1];
    CHECK(player.dynamic);
    CHECK_FALSE(entity.dynamic);
    CHECK(player.skin.joints.size() == 348);
    CHECK(entity.skin.joints.size() == 238);

    // The player keeps its own baked clips: the fifteen S2-S6 clips first
    // (S4.1's climb set plus S4.2's fight set), then S7's wake clips.
    check_wander_s2s6_clips(player.animations);
    CHECK(player.animations[0].name == "player_a_stand");
    // The entity carries Valus's eight baked clips (S4.2 adds the three attack
    // clips); the app selects the one the manifest names by name, never by
    // position.
    REQUIRE(entity.animations.size() == 8);
    Playback entity_by_name;
    build_playback(scene, 1, entity_by_name);
    const int idle_index = clip_index_named(entity_by_name, "minotaur_a_stand");
    REQUIRE(idle_index >= 0);
    CHECK(clip_index_named(entity_by_name, "minotaur_a_atk_leg_l") >= 0);
    CHECK(clip_index_named(entity_by_name, "minotaur_a_atk_leg_r") >= 0);
    CHECK(clip_index_named(entity_by_name, "minotaur_a_atk_weapon2_a") >= 0);
    const AnimationCpu& idle = entity.animations[idle_index];
    CHECK_FALSE(idle.channels.empty());
    CHECK(idle.duration > 0.0f);

    // Half-open, disjoint ranges: the static plank is rig -1, the player's 12
    // meshes are rig 0, the entity's 12 are rig 1.
    CHECK(player.meshBegin == 1);
    CHECK(player.meshEnd == 13);
    CHECK(entity.meshBegin == 13);
    CHECK(entity.meshEnd == 25);
    CHECK(player.meshEnd == entity.meshBegin);  // disjoint
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const int expected = i == 0 ? -1 : (i < 13 ? 0 : 1);
        CHECK(scene.meshes[i].rig == expected);
    }

    // The entity's meshes carry the manifest placement; the player's carry
    // identity (its own transform is applied by the app each frame).
    CHECK(scene.meshes[13].model.m[12] == doctest::Approx(382.0f));
    CHECK(scene.meshes[13].model.m[13] == doctest::Approx(28.369f));
    CHECK(scene.meshes[13].model.m[14] == doctest::Approx(370.0f));
    for (int k = 0; k < 16; ++k) {
        const float expected = (k % 5 == 0) ? 1.0f : 0.0f;
        CHECK(scene.meshes[1].model.m[k] == doctest::Approx(expected));
    }

    // The bounds cover the placed entity in world space.
    CHECK(scene.boundsMax.x >= 382.0f);
    CHECK(scene.boundsMax.z >= 370.0f);

    // The ground: the entity's meshes must contribute no sample. With the
    // entity's bind height (feet at local -8.739 → ~19.6 world) a leak would
    // raise every cell it covers; the static plank stays the only source.
    const Heightmap ground = build_heightmap(scene, 1.0f);
    const float under_entity = heightmap_sample(ground, Vec3{382.0f, 0.0f, 370.0f});
    CHECK(under_entity < 28.369f);
    CHECK(heightmap_sample(ground, Vec3{0.5f, 0.0f, 0.5f}) < 0.2f);  // the plank top

    // The same scene without the entity: the sample over the entity's column
    // is identical (both fall back to the nearest static ground), so the
    // entity changed no ground sample.
    SceneManifest plain_manifest{};
    plain_manifest.instances.push_back(placed_at(kPlankGlb, Vec3{0.0f, 0.0f, 0.0f}));
    const Scene plain = compose_scene(test_library(), plain_manifest, kWanderGlb);
    const Heightmap plain_ground = build_heightmap(plain, 1.0f);
    CHECK(heightmap_sample(plain_ground, Vec3{382.0f, 0.0f, 370.0f}) ==
          doctest::Approx(under_entity));

    // --- S4 task 4: per-rig palettes, playback and grounding ------------------

    // One playback per rig: the player keeps its own clips, the entity its
    // eight; each pose is sized by its own rig's joint count.
    Playback player_playback;
    Playback entity_playback;
    build_playback(scene, 0, player_playback);
    build_playback(scene, 1, entity_playback);
    check_wander_s2s6_clips(player_playback.clips);
    REQUIRE(entity_playback.clips.size() == 8);
    CHECK(player_playback.clips[static_cast<std::size_t>(player_playback.index)].name ==
          "player_a_stand");
    CHECK(entity_playback.clips[static_cast<std::size_t>(entity_playback.index)].name ==
          "minotaur_a_stand");
    CHECK_FALSE(player_playback.has_procedural);
    CHECK_FALSE(entity_playback.has_procedural);

    const std::vector<Mat4> player_palette =
        animation_pose(scene, 0, player_playback.clips[0], 0.0f);
    const std::vector<Mat4> entity_palette =
        animation_pose(scene, 1, entity_playback.clips[0], 0.8f);
    CHECK(player_palette.size() == 348);
    CHECK(entity_palette.size() == 238);

    // The entity's palette of its own idle at t = 0.8 has moved off its t = 0
    // pose (its root translation and 58 rotation tracks animate).
    const std::vector<Mat4> entity_at_rest = animation_pose(scene, 1, entity_playback.clips[0], 0.0f);
    bool moved = false;
    for (std::size_t i = 0; i < entity_palette.size() && !moved; ++i) {
        for (int k = 0; k < 16; ++k) {
            if (std::abs(entity_palette[i].m[k] - entity_at_rest[i].m[k]) > 1e-4f) {
                moved = true;
                break;
            }
        }
    }
    CHECK(moved);

    // Every mesh draws with its own rig's palette, and every joint index its
    // vertices carry stays inside it: the entity's indices can never reach the
    // player's buffer (nor the reverse).
    for (const MeshCpu& mesh : scene.meshes) {
        const std::size_t slot = mesh_palette_slot(scene, mesh);
        REQUIRE(slot < scene.rigs.size());
        CHECK((mesh.rig < 0 ? slot == 0 : static_cast<std::size_t>(mesh.rig) == slot));
        const std::size_t palette_size = scene.rigs[slot].skin.joints.size();
        std::size_t largest_joint = 0;
        for (const Vertex& vertex : mesh.vertices) {
            for (int k = 0; k < 4; ++k) {
                largest_joint = std::max(largest_joint, static_cast<std::size_t>(vertex.j[k]));
            }
        }
        CAPTURE(mesh.rig);  // -1 static, 0 player, 1 the entity
        CHECK(largest_joint < palette_size);
    }

    // Grounding: the idle carries the root translation series on Bone_root, so
    // the entity needs no lift; a root-less clip falls back to the rig's own
    // origin-to-feet lift — the colossus' measured 8.739 m bind offset. The
    // player's lift is its own 0.7596 m, untouched by the entity's range.
    CHECK(clip_has_root_translation(scene, 1, entity.animations[0]));
    CHECK(effective_rig_lift(scene, 1, entity.animations[0]) == 0.0f);
    const AnimationCpu rootless{};
    CHECK(effective_rig_lift(scene, 1, rootless) == doctest::Approx(8.739f).epsilon(1e-3));
    CHECK(rig_ground_offset(scene, entity) == doctest::Approx(8.739f).epsilon(1e-3));
    CHECK(rig_ground_offset(scene, player) == doctest::Approx(0.7596f).epsilon(1e-4));
    CHECK(effective_player_lift(scene, player.animations[0], player.meshBegin, player.meshEnd) ==
          0.0f);
}

TEST_CASE("compose_scene refuses an entity clip the model does not carry") {
    TempFixtureDir dir;
    // Wander as the entity with a clip it does not have: the load must fail
    // naming the glb path and the clip, never fall back to a rest pose.
    const SceneManifest manifest = parsed_manifest(
        dir, std::string("entity ") + kWanderGlb +
                 " 0 0 0 0 0 0 1 1 1 1 player_a_fly\n");

    std::string message;
    try {
        compose_scene(test_library(), manifest, kPlankGlb);
        FAIL("compose_scene did not throw for a missing entity clip");
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    CHECK(message.find("entity") != std::string::npos);
    CHECK(message.find(kWanderGlb) != std::string::npos);
    CHECK(message.find("player_a_fly") != std::string::npos);
}

TEST_CASE("compose_scene refuses an entity whose model has no clips") {
    TempFixtureDir dir;
    const SceneManifest manifest = parsed_manifest(
        dir, std::string("entity ") + kPlankGlb + " 0 0 0 0 0 0 1 1 1 1 idle\n");

    std::string message;
    try {
        compose_scene(test_library(), manifest, kWanderGlb);
        FAIL("compose_scene did not throw for an entity without clips");
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    CHECK(message.find("entity") != std::string::npos);
    CHECK(message.find(kPlankGlb) != std::string::npos);
    CHECK(message.find("idle") != std::string::npos);
    CHECK(message.find("no animation clips") != std::string::npos);
}

TEST_CASE("compose_scene names the entity glb it cannot load") {
    TempFixtureDir dir;
    const SceneManifest manifest =
        parsed_manifest(dir, "entity models/no_such_entity.glb 0 0 0 0 0 0 1 1 1 1 idle\n");

    std::string message;
    try {
        compose_scene(test_library(), manifest, kWanderGlb);
        FAIL("compose_scene did not throw for a missing entity glb");
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    CHECK(message.find("entity") != std::string::npos);
    CHECK(message.find("no_such_entity.glb") != std::string::npos);
}

// --- The club (S4.2 task 6) -------------------------------------------------
//
// The club is a separate, unskinned glb authored in OptBone_sword's bind
// frame. It is appended to a composed scene as static geometry (rig = -1) and
// placed each frame by club_model, which is what the app does. Library-gated.

TEST_CASE("the club glb loads as one static mesh with 9701 vertices") {
    Scene scene;
    const std::size_t begin = append_static_model(scene, test_library(), kClubGlb);

    // One node / one primitive / one material, no skin: the append adds no rig
    // record, and the mesh is static so the renderer binds palette slot 0.
    REQUIRE(scene.meshes.size() == 1);
    CHECK(begin == 0);
    CHECK(scene.meshes[0].rig == -1);
    CHECK(scene.meshes[0].vertices.size() == 9701);
    CHECK(scene.rigs.empty());
    REQUIRE(scene.materials.size() == 1);
    CHECK(scene.materials[0].name == "colossus_01_legs");
    // Its textures resolve through the library that already holds Valus's.
    CHECK_FALSE(scene.textures.empty());
    CHECK(std::filesystem::exists(test_library() / scene.textures[0].sourceRelPath));

    // Every face index stays inside the vertex array.
    uint32_t largest_index = 0;
    for (const uint32_t index : scene.meshes[0].indices) {
        largest_index = std::max(largest_index, index);
    }
    CHECK(largest_index < scene.meshes[0].vertices.size());
}

TEST_CASE("OptBone_sword resolves in the Valus rig and carries the club") {
    TempFixtureDir dir;
    const SceneManifest manifest = parsed_manifest(
        dir, std::string("# club fixture\ninstance ") + kPlankGlb +
                 " 0 0 0 0 0 0 1 1 1 1\n" + colossus_entity_line());
    Scene scene = compose_scene(test_library(), manifest, kWanderGlb);

    // The entity is rig 1 and its skeleton names the grip socket exactly. The
    // joint's bind world is the rest chain (not an inverse): it must agree with
    // the sequence's own inverse bind.
    REQUIRE(scene.rigs.size() == 2);
    const std::size_t entityRig = 1;
    std::size_t swordJoint = 0;
    bool found = false;
    for (std::size_t i = 0; i < scene.rigs[entityRig].skin.joints.size(); ++i) {
        if (scene.rigs[entityRig].skin.joints[i].name == "OptBone_sword") {
            swordJoint = i;
            found = true;
            break;
        }
    }
    REQUIRE(found);
    const JointCpu& sword = scene.rigs[entityRig].skin.joints[swordJoint];
    const Mat4 bind_identity = mat4_mul(sword.bindWorld, sword.inverseBind);
    for (int k = 0; k < 16; ++k) {
        CAPTURE(k);
        CHECK(bind_identity.m[k] == doctest::Approx((k % 5 == 0) ? 1.0f : 0.0f).epsilon(1e-4));
    }

    // Append the club exactly as the app does: at the entity's mesh placement.
    const std::size_t entityMeshBegin = scene.rigs[entityRig].meshBegin;
    REQUIRE(entityMeshBegin < scene.meshes.size());
    const Mat4 entityModel = scene.meshes[entityMeshBegin].model;
    const std::size_t clubBegin =
        append_static_model(scene, test_library(), kClubGlb, entityModel);
    REQUIRE(clubBegin == scene.meshes.size() - 1);
    CHECK(scene.meshes[clubBegin].rig == -1);

    // The idle clip at t = 0: the club model places the grip on the posed
    // OptBone_sword origin. The scene's own joint query reconstructs that
    // origin from the inverse bind, so the comparison checks the composed
    // bindWorld against an independent path.
    Playback entityPlayback;
    build_playback(scene, entityRig, entityPlayback);
    REQUIRE(entityPlayback.index >= 0);
    const AnimationCpu& idle =
        entityPlayback.clips[static_cast<std::size_t>(entityPlayback.index)];
    const std::vector<Mat4> palette = animation_pose(scene, entityRig, idle, 0.0f);
    REQUIRE(palette.size() == scene.rigs[entityRig].skin.joints.size());

    const Mat4 clubModel = club_model(scene, palette, swordJoint, entityModel);
    Vec3 swordWorld{};
    REQUIRE(posed_joint_world_position(scene, entityRig, palette, "OptBone_sword", swordWorld));
    CHECK(clubModel.m[12] == doctest::Approx(swordWorld.x).epsilon(1e-4));
    CHECK(clubModel.m[13] == doctest::Approx(swordWorld.y).epsilon(1e-4));
    CHECK(clubModel.m[14] == doctest::Approx(swordWorld.z).epsilon(1e-4));

    // A point 7 m down the club's authored -Z is its tip (OptBone_hit's local
    // (0, 0, -7) in the socket frame). The recon measured that tip 1.5 cm from
    // OptBone_hit under identity attachment; the bind world's rotation is what
    // carries it there, so a wrong socket frame fails this, not just a wrong
    // translation.
    const Vec3 clubTip{clubModel.m[8] * -7.0f + clubModel.m[12],
                       clubModel.m[9] * -7.0f + clubModel.m[13],
                       clubModel.m[10] * -7.0f + clubModel.m[14]};
    Vec3 hitWorld{};
    REQUIRE(posed_joint_world_position(scene, entityRig, palette, "OptBone_hit", hitWorld));
    const float tipDx = clubTip.x - hitWorld.x;
    const float tipDy = clubTip.y - hitWorld.y;
    const float tipDz = clubTip.z - hitWorld.z;
    CHECK(std::sqrt(tipDx * tipDx + tipDy * tipDy + tipDz * tipDz) < 0.02f);

    // The club's pose is finite and its lowest vertex is comfortably above the
    // entity's ground (the manifest y = 28.369): at the stand pose it hangs
    // from the right hand, not from the floor.
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    bool finite = true;
    for (const Vertex& vertex : scene.meshes[clubBegin].vertices) {
        const Vec3 p{vertex.px, vertex.py, vertex.pz};
        const Vec3 world{clubModel.m[0] * p.x + clubModel.m[4] * p.y + clubModel.m[8] * p.z +
                             clubModel.m[12],
                         clubModel.m[1] * p.x + clubModel.m[5] * p.y + clubModel.m[9] * p.z +
                             clubModel.m[13],
                         clubModel.m[2] * p.x + clubModel.m[6] * p.y + clubModel.m[10] * p.z +
                             clubModel.m[14]};
        finite = finite && std::isfinite(world.x) && std::isfinite(world.y) &&
                 std::isfinite(world.z);
        minY = std::min(minY, world.y);
        maxY = std::max(maxY, world.y);
    }
    CHECK(finite);
    CHECK(maxY > minY);
    CHECK(minY > 28.369f + 1.0f);
}

// --- `posed` static props (S7 task 5) ----------------------------------------

TEST_CASE("compose_scene skins a posed prop on the CPU into static meshes") {
    TempFixtureDir dir;
    write_text(dir.path() / "skin.gltf", skin_fixture_gltf_json());
    const SceneManifest manifest = parsed_manifest(
        dir, "posed skin.gltf 10 0 0 0 0 0 1 1 1 1 turn 1\n");

    // The fixture doubles as the player so the scene has its usual rig 0.
    const Scene scene = compose_scene(dir.path(), manifest, "skin.gltf");

    // The player's rig is the only rig: the posed prop keeps none and is static.
    REQUIRE(scene.rigs.size() == 1);
    REQUIRE(scene.meshes.size() == 2);
    const MeshCpu& posed = scene.meshes[1];
    CHECK(posed.rig == -1);
    REQUIRE(posed.vertices.size() == 3);

    // At t = 1 JointA is turned 90 degrees about Z around (0, 1, 0), so joint B
    // (weight 1 for every vertex) sits at (-1, 1, 0): the bind positions
    // (0,2,0) (1,2,0) (0,1,0) land at (-1,1,0) (-1,2,0) (0,1,0).
    const float expected[3][3] = {{-1.0f, 1.0f, 0.0f}, {-1.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    for (int v = 0; v < 3; ++v) {
        CAPTURE(v);
        CHECK(posed.vertices[v].px == doctest::Approx(expected[v][0]).epsilon(1e-4));
        CHECK(posed.vertices[v].py == doctest::Approx(expected[v][1]).epsilon(1e-4));
        CHECK(posed.vertices[v].pz == doctest::Approx(expected[v][2]).epsilon(1e-4));
        // The default +Y normal turns with the joint to -X.
        CHECK(posed.vertices[v].nx == doctest::Approx(-1.0f).epsilon(1e-4));
        CHECK(posed.vertices[v].ny == doctest::Approx(0.0f).epsilon(1e-4));
    }
    // Posed for good: no weight or joint survives, or the shader would re-skin the
    // vertex with the player's live palette (static meshes bind slot 0).
    for (const Vertex& vertex : posed.vertices) {
        for (int i = 0; i < 4; ++i) {
            CHECK(vertex.w[i] == 0.0f);
            CHECK(vertex.j[i] == 0);
        }
    }
    // The cull box is of the posed vertices; the TRS lives in `model`.
    CHECK(posed.localMin.x == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(posed.localMin.y == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(posed.localMax.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(posed.localMax.y == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(posed.model.m[12] == doctest::Approx(10.0f));
    // The scene bounds cover the posed, placed prop (x in [9, 10]).
    CHECK(scene.boundsMin.x <= 9.0f + 1e-4f);
    CHECK(scene.boundsMax.x >= 10.0f - 1e-4f);

    // Clamped like a pinned clock: t = 99 samples the last key, same as t = 1.
    const SceneManifest late = parsed_manifest(
        dir, "posed skin.gltf 10 0 0 0 0 0 1 1 1 1 turn 99\n");
    const Scene late_scene = compose_scene(dir.path(), late, "skin.gltf");
    CHECK(late_scene.meshes[1].vertices[0].px == doctest::Approx(-1.0f).epsilon(1e-4));
}

TEST_CASE("compose_scene names the glb and clip of a posed prop it cannot pose") {
    TempFixtureDir dir;
    write_text(dir.path() / "skin.gltf", skin_fixture_gltf_json());

    const auto message_for = [&](const std::string& line) {
        try {
            compose_scene(dir.path(), parsed_manifest(dir, line), "skin.gltf");
        } catch (const std::runtime_error& error) {
            return std::string(error.what());
        }
        return std::string();
    };

    const std::string missing_clip =
        message_for("posed skin.gltf 0 0 0 0 0 0 1 1 1 1 no_such_clip 0\n");
    CHECK(missing_clip.find("posed") != std::string::npos);
    CHECK(missing_clip.find("skin.gltf") != std::string::npos);
    CHECK(missing_clip.find("no_such_clip") != std::string::npos);

    const std::string missing_glb =
        message_for("posed no_such_prop.glb 0 0 0 0 0 0 1 1 1 1 turn 0\n");
    CHECK(missing_glb.find("posed") != std::string::npos);
    CHECK(missing_glb.find("no_such_prop.glb") != std::string::npos);
}

TEST_CASE("mono's posed death loop composes to finite static vertices") {
    TempFixtureDir dir;
    const SceneManifest manifest = parsed_manifest(
        dir, "posed models/shadow/characters/mono/_cmn/mono.glb 0 0 0 0 0 0 1 1 1 1 "
             "girl_a_death_loop 0\n");
    const Scene scene = compose_scene(test_library(), manifest, kWanderGlb);
    std::size_t posed_meshes = 0;
    for (const MeshCpu& mesh : scene.meshes) {
        if (mesh.rig != -1) continue;
        ++posed_meshes;
        for (const Vertex& vertex : mesh.vertices) {
            REQUIRE(std::isfinite(vertex.px + vertex.py + vertex.pz));
            REQUIRE(std::isfinite(vertex.nx + vertex.ny + vertex.nz));
        }
    }
    CHECK(posed_meshes > 0);
    CHECK(scene.rigs.size() == 1);
}

TEST_CASE("compose_scene keeps the scene bounds of two posed props") {
    TempFixtureDir dir;
    write_text(dir.path() / "skin.gltf", skin_fixture_gltf_json());
    const Scene scene = compose_scene(
        dir.path(),
        parsed_manifest(dir, "posed skin.gltf 10 0 0 0 0 0 1 1 1 1 turn 1\n"
                             "posed skin.gltf -20 5 0 0 0 0 1 1 1 1 turn 1\n"),
        "skin.gltf");
    REQUIRE(scene.meshes.size() == 3);
    REQUIRE(scene.rigs.size() == 1);
    CHECK(scene.meshes[2].rig == -1);
    // Posed x in [-1, 0]: props at +10 and -20 cover x in [-21, 10]; y in [1, 7].
    CHECK(scene.boundsMin.x == doctest::Approx(-21.0f).epsilon(1e-4));
    CHECK(scene.boundsMax.x == doctest::Approx(10.0f).epsilon(1e-4));
    CHECK(scene.boundsMax.y == doctest::Approx(7.0f).epsilon(1e-4));
}

// --- Scene-wide texture sharing (S7 task 11) -----------------------------------

TEST_CASE("two glbs sharing a PNG compose to one texture shared by both materials") {
    TempFixtureDir dir;
    write_binary(dir.path() / "shared.png", kPng1x1Red, sizeof kPng1x1Red);
    std::filesystem::create_directories(dir.path() / "sub");
    // Two different URIs, one library path: the key is the resolved path.
    write_text(dir.path() / "sub" / "a.gltf", fixture_gltf_json("../shared.png"));
    write_text(dir.path() / "b.gltf", fixture_gltf_json("shared.png"));
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("sub/a.gltf", Vec3{0.0f, 0.0f, 0.0f}));

    const Scene scene = compose_scene(dir.path(), manifest, "b.gltf");

    REQUIRE(scene.textures.size() == 1);
    CHECK(scene.textures[0].sourceRelPath == "shared.png");
    REQUIRE(scene.materials.size() == 2);
    CHECK(scene.materials[0].baseColorTexture == 0);
    CHECK(scene.materials[1].baseColorTexture == 0);
}

TEST_CASE("composed textures keep the serial first-seen order") {
    TempFixtureDir dir;
    for (const char* name : {"p1.png", "p2.png", "p3.png"}) {
        write_binary(dir.path() / name, kPng1x1Red, sizeof kPng1x1Red);
    }
    write_text(dir.path() / "a.gltf", fixture_gltf_json("p1.png", "p2.png"));
    write_text(dir.path() / "b.gltf", fixture_gltf_json("p2.png", "p3.png"));
    write_text(dir.path() / "c.gltf", fixture_gltf_json("p3.png", "p1.png"));
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("a.gltf", Vec3{0.0f, 0.0f, 0.0f}));
    manifest.instances.push_back(placed_at("b.gltf", Vec3{0.0f, 0.0f, 0.0f}));

    // Decoding runs in parallel; the indices must not depend on who finishes first.
    for (int run = 0; run < 3; ++run) {
        CAPTURE(run);
        const Scene scene = compose_scene(dir.path(), manifest, "c.gltf");
        REQUIRE(scene.textures.size() == 3);
        CHECK(scene.textures[0].sourceRelPath == "p1.png");
        CHECK(scene.textures[1].sourceRelPath == "p2.png");
        CHECK(scene.textures[2].sourceRelPath == "p3.png");
        REQUIRE(scene.materials.size() == 3);
        const int expected[3][2] = {{0, 1}, {1, 2}, {2, 0}};
        for (std::size_t m = 0; m < 3; ++m) {
            CHECK(scene.materials[m].baseColorTexture == expected[m][0]);
            CHECK(scene.materials[m].emissiveTexture == expected[m][1]);
        }
    }
}

TEST_CASE("a missing PNG shared by two glbs degrades to -1 in both") {
    TempFixtureDir dir;
    write_binary(dir.path() / "ok.png", kPng1x1Red, sizeof kPng1x1Red);
    write_text(dir.path() / "a.gltf", fixture_gltf_json("missing.png", "ok.png"));
    write_text(dir.path() / "b.gltf", fixture_gltf_json("missing.png"));
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("a.gltf", Vec3{0.0f, 0.0f, 0.0f}));

    Scene scene;
    REQUIRE_NOTHROW(scene = compose_scene(dir.path(), manifest, "b.gltf"));

    REQUIRE(scene.materials.size() == 2);
    CHECK(scene.materials[0].baseColorTexture == -1);
    CHECK(scene.materials[0].emissiveTexture == 0);
    CHECK(scene.materials[1].baseColorTexture == -1);
    REQUIRE(scene.textures.size() == 1);
    CHECK(scene.textures[0].sourceRelPath == "ok.png");
}

TEST_CASE("compose_scene reports progress before each glb it loads") {
    TempFixtureDir dir;
    write_binary(dir.path() / "shared.png", kPng1x1Red, sizeof kPng1x1Red);
    write_text(dir.path() / "a.gltf", fixture_gltf_json("shared.png"));
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("a.gltf", Vec3{0.0f, 0.0f, 0.0f}));
    manifest.instances.push_back(placed_at("a.gltf", Vec3{1.0f, 0.0f, 0.0f}));

    int calls = 0;
    compose_scene(dir.path(), manifest, "a.gltf", nullptr, [&calls] { ++calls; });

    // Two instances + the player, plus at most one per PNG this thread decoded.
    CHECK(calls >= 3);
    CHECK(calls <= 4);
}

TEST_CASE("compose_scene loads a horse as its own rig after the entities") {
    TempFixtureDir dir;
    const std::string agro = "models/shadow/characters/agro/_cmn/agro.glb";
    const SceneManifest manifest = parsed_manifest(
        dir, std::string("instance ") + kPlankGlb + " 0 0 0 0 0 0 1 1 1 1\n" +
                 colossus_entity_line() + "horse " + agro + " 5 0 0 0 0 0 1 1 1 1\n");

    const Scene scene = compose_scene(test_library(), manifest, kWanderGlb);

    // Rigs: the player, the entity, then the horse.
    REQUIRE(scene.rigs.size() == 3);
    CHECK(scene.horseRig == 2);
    const RigCpu& horse = scene.rigs[2];
    CHECK_FALSE(horse.dynamic);
    REQUIRE(horse.animations.size() >= 1);
    CHECK(horse.animations[0].name == "horse_a_add_horse_kick");
    REQUIRE(horse.meshEnd > horse.meshBegin);
    for (std::size_t m = horse.meshBegin; m < horse.meshEnd; ++m) {
        CHECK(scene.meshes[m].rig == 2);
        CHECK(scene.meshes[m].ground);
    }

    // The same manifest without a horse has none, and the entity keeps rig 1.
    const SceneManifest plain = parsed_manifest(
        dir, std::string("instance ") + kPlankGlb + " 0 0 0 0 0 0 1 1 1 1\n" +
                 colossus_entity_line());
    const Scene without = compose_scene(test_library(), plain, kWanderGlb);
    CHECK(without.horseRig == -1);
    CHECK(without.rigs.size() == 2);
    for (const MeshCpu& mesh : without.meshes) {
        CHECK(mesh.ground);
    }
}

TEST_CASE("compose_scene loads a backdrop as static, non-ground meshes outside the bounds") {
    TempFixtureDir dir;
    const Scene base = compose_scene(
        test_library(),
        parsed_manifest(dir, std::string("instance ") + kPlankGlb + " 0 0 0 0 0 0 1 1 1 1\n"),
        kWanderGlb);

    const SceneManifest manifest = parsed_manifest(
        dir, std::string("instance ") + kPlankGlb + " 0 0 0 0 0 0 1 1 1 1\nbackdrop " + kPlankGlb +
                 " 300 0.071 -300 0 0 0 1 1 1 1\n");
    const Scene scene = compose_scene(test_library(), manifest, kWanderGlb);

    // Load order: the instance, the player (12), then the backdrop's one mesh.
    REQUIRE(scene.meshes.size() == base.meshes.size() + 1);
    CHECK(scene.horseRig == -1);
    CHECK(scene.rigs.size() == 1);
    const MeshCpu& backdrop = scene.meshes.back();
    CHECK(backdrop.rig == -1);
    CHECK_FALSE(backdrop.ground);
    CHECK(backdrop.model.m[12] == doctest::Approx(300.0f));
    CHECK(backdrop.model.m[14] == doctest::Approx(-300.0f));
    // The backdrop's far corner does not stretch the scene (and so the heightmap) bounds.
    CHECK(scene.boundsMin.x == doctest::Approx(base.boundsMin.x));
    CHECK(scene.boundsMax.x == doctest::Approx(base.boundsMax.x));
    CHECK(scene.boundsMin.z == doctest::Approx(base.boundsMin.z));
    CHECK(scene.boundsMax.z == doctest::Approx(base.boundsMax.z));
    // Everything else stays ground.
    CHECK(scene.meshes.front().ground);
}

TEST_CASE("compose_scene loads a floor line as floors-only ground after the instances (S13)") {
    SceneManifest manifest{};
    manifest.instances.push_back(placed_at("models/objects/props/_cmn/obj_plank_01.glb",
                                           Vec3{1.0f, 0.0f, 0.0f}));
    manifest.floors.push_back(placed_at("models/objects/props/_cmn/obj_plank_01.glb",
                                        Vec3{0.0f, 0.0f, 50.0f}));
    std::size_t player_mesh_begin = 999;
    const Scene scene = compose_scene(test_library(), manifest,
                                      "models/shadow/characters/wander/_cmn/wander.glb",
                                      &player_mesh_begin);
    REQUIRE(scene.meshes.size() == 14);  // plank, floor plank, wander's 12
    CHECK(player_mesh_begin == 2);
    CHECK_FALSE(scene.meshes[0].floorsOnly);
    CHECK(scene.meshes[1].floorsOnly);
    CHECK(scene.meshes[1].ground);
    CHECK(scene.meshes[1].rig == -1);
    CHECK(scene.meshes[1].model.m[14] == doctest::Approx(50.0f));
    CHECK(scene.boundsMax.z >= 50.0f);  // a floor is scene extent, unlike a backdrop
    for (std::size_t i = 2; i < scene.meshes.size(); ++i) CHECK_FALSE(scene.meshes[i].floorsOnly);
}
