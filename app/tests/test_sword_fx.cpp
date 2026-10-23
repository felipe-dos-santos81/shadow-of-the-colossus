// The sword's light pass (S9 Task 5, design D7): the pure vertex builder and
// the texture loader. Headless: no window, no GPU.
#include <doctest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "sword_fx.h"

namespace {

std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        library = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / library;
    }
    return library;
}

Mat4 test_view_proj() {
    return mat4_mul(mat4_perspective(60.0f, 16.0f / 9.0f, 0.1f, 200.0f),
                    mat4_look_at(Vec3{0.0f, 0.0f, 12.0f}, Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 1.0f, 0.0f}));
}

// A fan of eight unit lines around +Y, leaning 30 degrees outward.
SwordFxFrame fan_frame() {
    SwordFxFrame frame{};
    frame.active = true;
    frame.tip = Vec3{1.0f, 2.0f, 3.0f};
    for (int i = 0; i < 8; ++i) {
        const float yaw = static_cast<float>(i) * 45.0f * kDegToRad;
        const float lean = 30.0f * kDegToRad;
        frame.lines[i] = Vec3{std::sin(lean) * std::cos(yaw), std::cos(lean), std::sin(lean) * std::sin(yaw)};
    }
    frame.length = 20.0f;
    frame.width = 0.75f;
    frame.alpha = 0.6f;
    return frame;
}

Vec3 pos(const EffectVertex& v) { return Vec3{v.px, v.py, v.pz}; }

float dist(Vec3 a, Vec3 b) {
    const Vec3 d{a.x - b.x, a.y - b.y, a.z - b.z};
    return std::sqrt(vec3_dot(d, d));
}

Vec3 mid(Vec3 a, Vec3 b) { return Vec3{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f}; }

}  // namespace

TEST_CASE("an inactive or alpha-0 sword frame builds nothing") {
    SwordFxFrame frame = fan_frame();
    frame.active = false;
    CHECK(build_sword_vertices(frame, test_view_proj()).empty());
    frame = fan_frame();
    frame.alpha = 0.0f;
    CHECK(build_sword_vertices(frame, test_view_proj()).empty());
}

TEST_CASE("an active frame builds 36 vertices, 40 with a hit") {
    SwordFxFrame frame = fan_frame();
    CHECK(build_sword_vertices(frame, test_view_proj()).size() == 36);
    frame.hit = true;
    frame.hitPoint = Vec3{5.0f, 0.0f, -40.0f};
    const std::vector<EffectVertex> v = build_sword_vertices(frame, test_view_proj());
    REQUIRE(v.size() == 40);
    // The flare is centred on the tip, the hit sprite on the hit point.
    CHECK(dist(mid(pos(v[32]), pos(v[34])), frame.tip) < 1e-4f);
    CHECK(dist(mid(pos(v[36]), pos(v[38])), frame.hitPoint) < 1e-4f);
    CHECK(dist(pos(v[32]), pos(v[33])) == doctest::Approx(frame.flareSize).epsilon(1e-4));
    CHECK(dist(pos(v[36]), pos(v[37])) == doctest::Approx(frame.hitSize).epsilon(1e-4));
}

TEST_CASE("each line quad runs from the tip to tip + line * length, width wide") {
    const SwordFxFrame frame = fan_frame();
    const std::vector<EffectVertex> v = build_sword_vertices(frame, test_view_proj());
    REQUIRE(v.size() == 36);
    for (int q = 0; q < 8; ++q) {
        const EffectVertex* quad = &v[q * 4];
        const Vec3 far{frame.tip.x + frame.lines[q].x * frame.length,
                       frame.tip.y + frame.lines[q].y * frame.length,
                       frame.tip.z + frame.lines[q].z * frame.length};
        CHECK(dist(mid(pos(quad[0]), pos(quad[1])), frame.tip) < 1e-4f);
        CHECK(dist(mid(pos(quad[2]), pos(quad[3])), far) < 1e-4f);
        CHECK(dist(pos(quad[0]), pos(quad[1])) == doctest::Approx(frame.width).epsilon(1e-4));
        CHECK(dist(pos(quad[2]), pos(quad[3])) == doctest::Approx(frame.width).epsilon(1e-4));
    }
}

TEST_CASE("every sword vertex carries the frame's alpha") {
    SwordFxFrame frame = fan_frame();
    frame.hit = true;
    for (const EffectVertex& vertex : build_sword_vertices(frame, test_view_proj())) {
        CHECK(vertex.a == frame.alpha);
    }
}

TEST_CASE("the same sword frame and camera twice are byte-identical") {
    SwordFxFrame frame = fan_frame();
    frame.hit = true;
    const std::vector<EffectVertex> a = build_sword_vertices(frame, test_view_proj());
    const std::vector<EffectVertex> b = build_sword_vertices(frame, test_view_proj());
    REQUIRE(a.size() == b.size());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(EffectVertex)) == 0);
}

TEST_CASE("a 2x2 atlas grid maps the line quads onto frame 0") {
    SwordFxFrame frame = fan_frame();
    frame.atlasCols = 2;
    frame.atlasRows = 2;
    const std::vector<EffectVertex> v = build_sword_vertices(frame, test_view_proj());
    REQUIRE(v.size() == 36);
    for (int i = 0; i < 32; ++i) {
        CHECK(v[i].u >= 0.0f);
        CHECK(v[i].u <= 0.5f);
        CHECK(v[i].v >= 0.0f);
        CHECK(v[i].v <= 0.5f);
    }
    // The flare is not an atlas: it spans the whole texture.
    CHECK(v[34].u == 1.0f);
}

TEST_CASE("load_sword_textures reads exactly the given paths") {
    // Any four library PNGs stand in for the sword's (Task 8 passes the real paths).
    const std::array<std::string, 4> rel = {
        "textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png",
        "textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.png",
        "textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png",
        "textures/shadow/characters/common/_ps4/death_tentacle_veins.png",
    };
    const std::array<TextureCpu, 4> textures = load_sword_textures(test_library().string(), rel);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(textures[i].sourceRelPath == rel[i]);
        CHECK(textures[i].width > 0);
        CHECK(textures[i].height > 0);
        CHECK(textures[i].rgba8.size() ==
              static_cast<std::size_t>(textures[i].width) * textures[i].height * 4);
    }

    std::array<std::string, 4> missing = rel;
    missing[2] = "textures/vfx/no_such_sword_glow.png";
    try {
        load_sword_textures(test_library().string(), missing);
        FAIL("a missing sword texture must throw");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()).find(missing[2]) != std::string::npos);
    }
}
