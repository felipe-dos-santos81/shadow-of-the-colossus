#include <doctest.h>

#include <unistd.h>

#include <cmath>
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
#include "anim.h"
#include "heightmap.h"
#include "player.h"

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
// relative to the model's directory.
std::string fixture_gltf_json(const std::string& image_uri = "missing.png") {
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
  "images": [{"uri": "%%IMAGE_URI%%"}],
  "textures": [{"source": 0}],
  "materials": [{"name": "fixture", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";
    const std::string buffer_placeholder = "%%BUFFER_URI%%";
    json.replace(json.find(buffer_placeholder), buffer_placeholder.size(), buffer_uri);
    const std::string image_placeholder = "%%IMAGE_URI%%";
    json.replace(json.find(image_placeholder), image_placeholder.size(), image_uri);
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

TEST_CASE("wander carries its 348-joint skeleton") {
    auto scene = load_scene(test_library(), "models/shadow/characters/wander/_cmn/wander.glb");
    CHECK(player_rig(scene).skin.joints.size() == 348);
    CHECK(player_rig(scene).skin.joints[0].name == "Bone_root");
    // The S2 converter bakes the player's demo clips into the glb, named after
    // the source clip stem: the app plays these instead of the stand-in. S4.1
    // bakes the full climb/fight set, so the count is pinned at 11 and the new
    // clips are found by name — the way the app selects them.
    REQUIRE(player_rig(scene).animations.size() == 11);
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
    const auto& v = scene.meshes[0].vertices[0];
    CHECK(v.j[0] == 102); CHECK(v.j[1] == 73); CHECK(v.j[2] == 0); CHECK(v.j[3] == 0);
    CHECK(v.w[0] == doctest::Approx(0.9094f).epsilon(1e-3));
    CHECK(v.w[1] == doctest::Approx(0.0906f).epsilon(1e-3));
}

TEST_CASE("agro carries its baked kick clip") {
    auto scene = load_scene(test_library(), "models/shadow/characters/agro/_cmn/agro.glb");
    REQUIRE(player_rig(scene).animations.size() == 1);
    CHECK(player_rig(scene).animations[0].name == "horse_a_add_horse_kick");
    CHECK(!player_rig(scene).animations[0].channels.empty());
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
    // materialIndex and every material's texture indices stay valid. Each
    // instance load appends its own textures (2 planks x 2 + wander's 21).
    CHECK(scene.materials.size() == 14);
    CHECK(scene.textures.size() == 25);
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
    CHECK(scene.meshes[0].materialIndex == 0);
    CHECK(scene.meshes[1].materialIndex == 1);
    check_material_matches(scene, 0, plank, plank.meshes[0].materialIndex);
    check_material_matches(scene, 1, plank, plank.meshes[0].materialIndex);
    for (std::size_t i = 0; i < wander.meshes.size(); ++i) {
        const int composed_index = scene.meshes[2 + i].materialIndex;
        CHECK(composed_index == 2 + wander.meshes[i].materialIndex);
        check_material_matches(scene, composed_index, wander, wander.meshes[i].materialIndex);
    }

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

    // The player keeps its own eleven baked clips (the S4.1 climb/fight set).
    REQUIRE(player.animations.size() == 11);
    CHECK(player.animations[0].name == "player_a_stand");
    // The entity carries Valus's five baked clips; the app selects the one the
    // manifest names by name, never by position.
    REQUIRE(entity.animations.size() == 5);
    Playback entity_by_name;
    build_playback(scene, 1, entity_by_name);
    const int idle_index = clip_index_named(entity_by_name, "minotaur_a_stand");
    REQUIRE(idle_index >= 0);
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

    // One playback per rig: the player keeps its eleven clips, the entity its
    // five; each pose is sized by its own rig's joint count.
    Playback player_playback;
    Playback entity_playback;
    build_playback(scene, 0, player_playback);
    build_playback(scene, 1, entity_playback);
    REQUIRE(player_playback.clips.size() == 11);
    REQUIRE(entity_playback.clips.size() == 5);
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
