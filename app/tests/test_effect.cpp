// The tendril effect (S6 Task 4, design §7 D4/§8.2): the CPU geometry builder,
// the four shipped effect textures and the seeded determinism. Every case here
// is pure — no window, no GPU and no clock — so the pass's numbers are tested
// headless; the look is the Task 5/6 still gate.
//
// The vertex order is the contract the renderer's static index buffer assumes:
//   [ 0 .. 402*roots)              the ribbons, one 402-vertex strip per root
//   [402*roots .. +96)             the 24 smoke quads (4 vertices each)
//   [.. +32) / [.. +32)            the 8 + 8 wisp quads, one atlas each
// A station's two vertices sit at center ± side * width / 2, so their midpoint
// is the centerline point the 0.25 m spacing is measured on.
#include <doctest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "effect.h"

namespace {

// The converted-asset library lives outside the repo, so the environment must
// name it; a relative value is the documented `<repo>/out/library` form,
// resolved against the repo root (ctest runs from `app/build`).
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        library = repo_root / library;
    }
    return library;
}

// A 60-degree camera 12 m in front of the origin, looking at it: the
// view-projection's row 0 is the camera's world right (+X) and its row 1 the
// camera's up (+Y), which is what the billboards orient by.
Mat4 test_view_proj() {
    return mat4_mul(mat4_perspective(60.0f, 16.0f / 9.0f, 0.1f, 200.0f),
                    mat4_look_at(Vec3{0.0f, 0.0f, 12.0f}, Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 1.0f, 0.0f}));
}

EffectFrame two_roots(float age) {
    EffectFrame frame{};
    frame.active = true;
    frame.rootCount = 2;
    frame.age = age;
    frame.roots[0] = EffectRoot{Vec3{1.0f, 2.0f, 3.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0};
    frame.roots[1] = EffectRoot{Vec3{-1.0f, 2.0f, 3.0f}, Vec3{0.0f, 1.0f, 0.0f}, 1};
    return frame;
}

Vec3 station_center(const std::vector<EffectVertex>& vertices, int station) {
    const EffectVertex& a = vertices[static_cast<std::size_t>(station) * 2];
    const EffectVertex& b = vertices[static_cast<std::size_t>(station) * 2 + 1];
    return Vec3{(a.px + b.px) * 0.5f, (a.py + b.py) * 0.5f, (a.pz + b.pz) * 0.5f};
}

float distance(Vec3 a, Vec3 b) {
    return std::sqrt(vec3_dot(Vec3{a.x - b.x, a.y - b.y, a.z - b.z},
                             Vec3{a.x - b.x, a.y - b.y, a.z - b.z}));
}

bool all_finite(const std::vector<EffectVertex>& vertices) {
    for (const EffectVertex& v : vertices) {
        if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz) ||
            !std::isfinite(v.u) || !std::isfinite(v.v) || !std::isfinite(v.r) ||
            !std::isfinite(v.g) || !std::isfinite(v.b) || !std::isfinite(v.a)) {
            return false;
        }
    }
    return true;
}

Vec3 vertex_position(const EffectVertex& v) { return Vec3{v.px, v.py, v.pz}; }

// A quad's centre: the four billboard corners are the centre ± right/up, so
// their average is the centre whatever the camera.
Vec3 quad_center(const std::vector<EffectVertex>& vertices, int quad) {
    const std::size_t base = 2 * 402 + static_cast<std::size_t>(quad) * 4;
    Vec3 sum{0.0f, 0.0f, 0.0f};
    for (int corner = 0; corner < 4; ++corner) {
        const Vec3 p = vertex_position(vertices[base + static_cast<std::size_t>(corner)]);
        sum = Vec3{sum.x + p.x, sum.y + p.y, sum.z + p.z};
    }
    return Vec3{sum.x * 0.25f, sum.y * 0.25f, sum.z * 0.25f};
}

float quad_width(const std::vector<EffectVertex>& vertices, int quad) {
    const std::size_t base = 2 * 402 + static_cast<std::size_t>(quad) * 4;
    return distance(vertex_position(vertices[base]), vertex_position(vertices[base + 1]));
}

}  // namespace

TEST_CASE("two roots yield 2x402 ribbon plus 160 billboard vertices") {
    const std::vector<EffectVertex> vertices = build_tendril_vertices(two_roots(1.4f), test_view_proj());
    CHECK(vertices.size() == 2 * 402 + 160);
    CHECK(kTendrilRibbonVertices == 402);
    CHECK(kTendrilBillboardVertices == 160);

    // One root draws one ribbon and the same 40 billboards (24 smoke + 16 wisp),
    // so the billboard count is per frame, not per root.
    EffectFrame single = two_roots(1.4f);
    single.rootCount = 1;
    CHECK(build_tendril_vertices(single, test_view_proj()).size() == 402 + 160);

    // rootCount is clamped to the two-element roots array.
    EffectFrame oversized = two_roots(1.4f);
    oversized.rootCount = 3;
    CHECK(build_tendril_vertices(oversized, test_view_proj()).size() == 2 * 402 + 160);
}

TEST_CASE("the ribbon starts at the root and steps 0.25 m per station") {
    const EffectFrame frame = two_roots(2.0f);  // fully grown
    const std::vector<EffectVertex> vertices = build_tendril_vertices(frame, test_view_proj());

    // The first station's midpoint is the root, and its two vertices straddle it
    // by the root half-width (0.6 / 2).
    const Vec3 first = station_center(vertices, 0);
    CHECK(distance(first, frame.roots[0].position) < 1e-4f);
    CHECK(std::fabs(distance(Vec3{vertices[0].px, vertices[0].py, vertices[0].pz},
                             frame.roots[0].position) - 0.3f) < 1e-3f);

    // 201 stations, 200 intervals of exactly one segment each — the binary's
    // 0.25 m.
    CHECK(kTendrilStations == 201);
    for (int i = 0; i < kTendrilSegments; ++i) {
        CHECK(distance(station_center(vertices, i), station_center(vertices, i + 1)) ==
              doctest::Approx(kTendrilSpacing).epsilon(1e-3));
    }

    // The second root's ribbon is its own path, starting at its own root.
    const Vec3 second_root_center =
        Vec3{(vertices[402].px + vertices[403].px) * 0.5f,
             (vertices[402].py + vertices[403].py) * 0.5f,
             (vertices[402].pz + vertices[403].pz) * 0.5f};
    CHECK(distance(second_root_center, frame.roots[1].position) < 1e-4f);
}

TEST_CASE("the same frame and age build byte-identical vertices") {
    const Mat4 view_proj = test_view_proj();
    const std::vector<EffectVertex> first = build_tendril_vertices(two_roots(1.1f), view_proj);
    const std::vector<EffectVertex> second = build_tendril_vertices(two_roots(1.1f), view_proj);
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(EffectVertex)) == 0);

    // A different age is a different frame.
    const std::vector<EffectVertex> later = build_tendril_vertices(two_roots(2.2f), view_proj);
    CHECK(std::memcmp(first.data(), later.data(), first.size() * sizeof(EffectVertex)) != 0);
}

TEST_CASE("an inactive or empty effect frame builds nothing") {
    EffectFrame inactive = two_roots(1.4f);
    inactive.active = false;
    CHECK(build_tendril_vertices(inactive, test_view_proj()).empty());

    EffectFrame no_roots = two_roots(1.4f);
    no_roots.rootCount = 0;
    CHECK(build_tendril_vertices(no_roots, test_view_proj()).empty());

    EffectFrame negative = two_roots(1.4f);
    negative.rootCount = -1;
    CHECK(build_tendril_vertices(negative, test_view_proj()).empty());

    // A default-constructed frame is the no-op the renderer's nullptr contract
    // stands for.
    CHECK(build_tendril_vertices(EffectFrame{}, Mat4{}).empty());
}

TEST_CASE("a zero root and a root at the origin stay finite") {
    // A zeroed root: no position, no normal, no seed. The builder falls back to
    // a world up heading instead of normalizing a zero vector into NaN.
    EffectFrame zero{};
    zero.active = true;
    zero.rootCount = 1;
    zero.age = 1.4f;
    const std::vector<EffectVertex> zero_vertices = build_tendril_vertices(zero, test_view_proj());
    REQUIRE(zero_vertices.size() == 562);
    CHECK(all_finite(zero_vertices));

    // An origin root with a real normal, and an all-zero view-projection (the
    // billboard basis then falls back to world axes rather than zero rows).
    EffectFrame origin{};
    origin.active = true;
    origin.rootCount = 1;
    origin.age = 1.4f;
    origin.roots[0] = EffectRoot{Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 7};
    const std::vector<EffectVertex> origin_vertices =
        build_tendril_vertices(origin, Mat4{});
    REQUIRE(origin_vertices.size() == 562);
    CHECK(all_finite(origin_vertices));
}

TEST_CASE("the ribbon grows between 0.4 s and 1.2 s and is steady by 3.0 s") {
    const Mat4 view_proj = test_view_proj();
    const Vec3 root = two_roots(0.0f).roots[0].position;

    const auto tip_distance = [&](float age) {
        const std::vector<EffectVertex> vertices =
            build_tendril_vertices(two_roots(age), view_proj);
        return distance(station_center(vertices, kTendrilSegments), root);
    };

    const float at_start = tip_distance(kTendrilStartSeconds);
    const float at_half = tip_distance(kTendrilStartSeconds + 0.5f * kTendrilGrowSeconds);
    const float at_full = tip_distance(kTendrilStartSeconds + kTendrilGrowSeconds);
    CHECK(at_start < 1e-3f);          // not yet grown: the ribbon is the root
    CHECK(at_half > at_start + 10.0f);  // 0.6 of 50 m
    CHECK(at_full > at_half + 10.0f);
    // The straight 50 m path curls, so the displacement is long but under 50 m.
    CHECK(at_full > 20.0f);
    CHECK(at_full < 50.0f);

    // Steady after the growth: the envelope does not exceed the 201 segments.
    const float at_end = tip_distance(kTendrilEndSeconds);
    CHECK(at_end == doctest::Approx(at_full).epsilon(1e-5));
}

TEST_CASE("the ribbon tapers from the root width to the tip width") {
    const std::vector<EffectVertex> vertices = build_tendril_vertices(two_roots(2.0f), test_view_proj());
    const auto station_width = [&](int station) {
        return distance(vertex_position(vertices[static_cast<std::size_t>(station) * 2]),
                        vertex_position(vertices[static_cast<std::size_t>(station) * 2 + 1]));
    };

    CHECK(station_width(0) == doctest::Approx(0.6f).epsilon(1e-3));    // the wound
    CHECK(station_width(kTendrilSegments) == doctest::Approx(0.05f).epsilon(1e-3));  // the tip
    // Monotone in between: the envelope never widens back toward the tip.
    for (int station = 1; station <= kTendrilSegments; ++station) {
        CHECK(station_width(station) <= station_width(station - 1) + 1e-6f);
    }
}

TEST_CASE("the billboards face the camera") {
    // A camera off to one side, looking at the origin: its view direction is
    // not a world axis, so a quad built on the world axes would fail this.
    const Vec3 eye{3.0f, 4.0f, 5.0f};
    const Mat4 view_proj = mat4_mul(mat4_perspective(50.0f, 1.0f, 0.1f, 200.0f),
                                    mat4_look_at(eye, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 view_dir = vec3_normalize(Vec3{-eye.x, -eye.y, -eye.z});

    const std::vector<EffectVertex> vertices = build_tendril_vertices(two_roots(1.6f), view_proj);
    REQUIRE(vertices.size() == 964);

    for (int quad = 0; quad < 40; ++quad) {
        const std::size_t base = 2 * 402 + static_cast<std::size_t>(quad) * 4;
        const Vec3 corner = vertex_position(vertices[base]);
        const Vec3 right_edge = vertex_position(vertices[base + 1]);
        const Vec3 up_edge = vertex_position(vertices[base + 3]);
        const Vec3 edge_right{right_edge.x - corner.x, right_edge.y - corner.y,
                              right_edge.z - corner.z};
        const Vec3 edge_up{up_edge.x - corner.x, up_edge.y - corner.y, up_edge.z - corner.z};

        // Both edges lie in the image plane: perpendicular to the view direction.
        CHECK(std::fabs(vec3_dot(vec3_normalize(edge_right), view_dir)) < 1e-3f);
        CHECK(std::fabs(vec3_dot(vec3_normalize(edge_up), view_dir)) < 1e-3f);
        // And the quad's normal is the view axis (either sign).
        const Vec3 normal = vec3_normalize(vec3_cross(edge_right, edge_up));
        CHECK(std::fabs(std::fabs(vec3_dot(normal, view_dir)) - 1.0f) < 1e-3f);
    }
}

TEST_CASE("every billboard samples inside one atlas tile") {
    const std::vector<EffectVertex> vertices = build_tendril_vertices(two_roots(1.6f), test_view_proj());

    // Each quad's four UVs must land in one cell of its atlas: a UV that
    // crossed a cell boundary would sample the neighbouring tile (or, at the
    // texture edge, the clamp).
    const auto cell_of = [&](int quad, int columns, int rows, int& cell_u, int& cell_v) {
        const std::size_t base = 2 * 402 + static_cast<std::size_t>(quad) * 4;
        cell_u = -1;
        cell_v = -1;
        for (int corner = 0; corner < 4; ++corner) {
            const EffectVertex& v = vertices[base + static_cast<std::size_t>(corner)];
            CHECK(v.u >= 0.0f);
            CHECK(v.u <= 1.0f);
            CHECK(v.v >= 0.0f);
            CHECK(v.v <= 1.0f);
            const int u = std::min(static_cast<int>(v.u * static_cast<float>(columns)), columns - 1);
            const int c = std::min(static_cast<int>(v.v * static_cast<float>(rows)), rows - 1);
            if (cell_u < 0) {
                cell_u = u;
                cell_v = c;
            }
            CHECK(u == cell_u);
            CHECK(c == cell_v);
        }
    };

    // The 24 smoke quads: the 2x2 atlas, quad i in tile i % 4 (u = i % 2,
    // v = i / 2).
    for (int quad = 0; quad < 24; ++quad) {
        int cell_u = -1;
        int cell_v = -1;
        cell_of(quad, 2, 2, cell_u, cell_v);
        CHECK(cell_u == (quad % 4) % 2);
        CHECK(cell_v == (quad % 4) / 2);
    }
    // The 16 wisp quads: the 8x8 atlases, tiles within the grid.
    for (int quad = 24; quad < 40; ++quad) {
        int cell_u = -1;
        int cell_v = -1;
        cell_of(quad, 8, 8, cell_u, cell_v);
        CHECK(cell_u >= 0);
        CHECK(cell_v >= 0);
    }
}

TEST_CASE("the smoke quads start at the wound and spread up the ribbon") {
    const EffectFrame frame = two_roots(2.0f);
    const std::vector<EffectVertex> vertices = build_tendril_vertices(frame, test_view_proj());
    REQUIRE(vertices.size() == 964);

    // Quad i hangs off root i % 2 (the smoke spread alternates roots).
    const auto wound_distance = [&](int quad) {
        return distance(quad_center(vertices, quad), frame.roots[quad % 2].position);
    };

    // The squared-fraction bias: the first quad is at the wound (its lateral
    // and lift jitter stay under a metre), not 7 m up the path, and the spread
    // grows with the quad index up to the grown tip.
    CHECK(wound_distance(0) < 1.0f);
    CHECK(wound_distance(6) > 3.0f);
    CHECK(wound_distance(12) > 10.0f);
    CHECK(wound_distance(23) > 20.0f);
    CHECK(wound_distance(0) < wound_distance(23));

    // Every quad has a real extent (a size of at least the 0.4 m minimum).
    for (int quad = 0; quad < 40; ++quad) {
        CHECK(quad_width(vertices, quad) >= 0.4f - 1e-3f);
    }
}

TEST_CASE("the four effect textures load from the library at their pinned sizes") {
    const std::array<TextureCpu, 4> textures = load_effect_textures(test_library().string());
    REQUIRE(textures[0].width == 1024);
    CHECK(textures[0].height == 1024);  // 8x8_wispysmoke02_1024_white_pfx
    REQUIRE(textures[1].width == 1024);
    CHECK(textures[1].height == 1024);  // 8x8_smoke_wispy_sharp_1024_pfx
    REQUIRE(textures[2].width == 512);
    CHECK(textures[2].height == 512);   // 2x2_smoke_tendrils_01_pfx
    REQUIRE(textures[3].width == 1024);
    CHECK(textures[3].height == 256);   // death_tentacle_veins

    // Every texture is RGBA8 and names its library-relative source.
    for (const TextureCpu& texture : textures) {
        CHECK(texture.rgba8.size() ==
              static_cast<std::size_t>(texture.width) * texture.height * 4);
        CHECK(texture.sourceRelPath.starts_with("textures/"));
    }
    CHECK(textures[2].sourceRelPath == "textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png");
    CHECK(textures[3].sourceRelPath == "textures/shadow/characters/common/_ps4/death_tentacle_veins.png");

    // The ribbon repeats the veins mask along v and its sampler wraps that axis,
    // so the seam must be mild: the first and last rows' alpha means are close
    // (measured 144.5 vs 145.2 of 255). u is clamped, so no such constraint
    // applies across the width.
    const TextureCpu& veins = textures[3];
    const auto row_alpha_mean = [&](int row) {
        double sum = 0.0;
        for (int x = 0; x < veins.width; ++x) {
            sum += veins.rgba8[static_cast<std::size_t>(row) * static_cast<std::size_t>(veins.width) * 4 +
                               static_cast<std::size_t>(x) * 4 + 3];
        }
        return sum / veins.width;
    };
    CHECK(std::fabs(row_alpha_mean(0) - row_alpha_mean(veins.height - 1)) < 8.0);
}

TEST_CASE("a missing effect texture throws and names the path") {
    const std::string missing = (test_library() / "no-such-s6-library").string();
    CHECK_THROWS_AS(load_effect_textures(missing), std::runtime_error);

    try {
        load_effect_textures(missing);
        FAIL("load_effect_textures did not throw for a missing library");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CHECK(message.find("textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png") !=
              std::string::npos);
        CHECK(message.find(missing) != std::string::npos);
    }
}
