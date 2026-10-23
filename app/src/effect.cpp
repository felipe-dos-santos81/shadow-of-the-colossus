#include "effect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "encounters/valus_desc.h"
#include "stb_image.h"

namespace {

// The burst window is the death machine's, not a second copy: a change to
// Valus's death timings must not silently desynchronise the effect. (The
// ribbon's envelope is still Valus's window; another encounter's would need it
// passed in the EffectFrame.)
static_assert(kTendrilStartSeconds == encounters::kValus.death.tendrilStartSeconds,
              "the tendril start must match Valus's death bundle");
static_assert(kTendrilEndSeconds == encounters::kValus.death.tendrilEndSeconds,
              "the tendril end must match Valus's death bundle");

// The ribbon's authored envelope (design §8.2): 0.6 m at the wound, tapering to
// 0.05 m at the tip.
constexpr float kRootWidth = 0.6f;
constexpr float kTipWidth = 0.05f;

// The billboards' 1.2 s life. The vein mask repeats kVeinRepeats times along
// the path: the ribbon's own sampler wraps `v` (u stays clamped, so the
// width's two edges cannot bleed into each other). Wrapping is what makes the
// repeat real — a clamped v would smear the texture's bottom row over every
// station past the first repeat.
constexpr float kBillboardLifeSeconds = 1.2f;
constexpr float kVeinRepeats = 4.0f;

// The pinned atlas sizes (Task 0's gate): a half-texel inset keeps bilinear
// sampling from bleeding into the neighbouring tile. Both values are in
// whole-texture UV.
constexpr float kWispTexel = 1.0f / 1024.0f;   // the two 8x8 atlases
constexpr float kSmokeTexel = 1.0f / 512.0f;   // the 2x2 atlas

// Authored display-space tints (the pass writes the target's display-space
// UNORM, like mesh.metal's final encode): a near-black violet smoke veil, a
// faint violet haze, and a cold glow through the veins mask. The ribbon's
// vertices carry the glow pre-scaled by kVeinAlpha in rgb and the growth fade
// in alpha: its premultiplied pass reads the mask's black rgb at full coverage
// (the black tendril), and its additive pass then adds tint * 0.3 * coverage.
constexpr float kSmokeTint[3] = {0.055f, 0.045f, 0.075f};
constexpr float kWispTint[3] = {0.120f, 0.090f, 0.160f};
constexpr float kVeinTint[3] = {0.420f, 0.360f, 0.520f};
constexpr float kVeinAlpha = 0.30f;
constexpr float kVeinGlow[3] = {kVeinTint[0] * kVeinAlpha, kVeinTint[1] * kVeinAlpha,
                                kVeinTint[2] * kVeinAlpha};

float clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

float clamp01(float value) { return clampf(value, 0.0f, 1.0f); }

// A 32-bit integer finalizer (the murmur3 mix): the whole deterministic seed
// machinery. It is a pure function of its input on every platform.
uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A uniform value in [0, 1).
float unit_of(uint32_t x) {
    return static_cast<float>(hash_u32(x) & 0x00ffffffU) / 16777216.0f;
}

float signed_unit_of(uint32_t x) { return unit_of(x) * 2.0f - 1.0f; }

// A seed-uniform point on the unit sphere (z = cos(polar angle)).
Vec3 unit_sphere(uint32_t a, uint32_t b) {
    const float theta = unit_of(a) * 2.0f * kPi;
    const float z = signed_unit_of(hash_u32(b));
    const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return Vec3{radius * std::cos(theta), z, radius * std::sin(theta)};
}

// A unit vector perpendicular to `t`; the reference axis never parallels `t`.
Vec3 any_perpendicular(Vec3 t) {
    const Vec3 reference = std::fabs(t.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    return vec3_normalize(vec3_cross(reference, t));
}

// One root's 201-station curl, in world space.
struct Ribbon {
    std::array<Vec3, kTendrilStations> points{};
    std::array<Vec3, kTendrilStations> sides{};
    // The grown arc length in segments, [0, kTendrilSegments]; stations beyond
    // it collapse to `tip`, so an ungrown strip is a set of degenerate quads
    // instead of a short strip that would have to change length.
    float horizon = 0.0f;
    Vec3 tip{};
};

// The seed's curl: start along the wound normal with a seeded lean, then turn a
// seeded two-octave 3-D curvature every 0.25 m. Integrating a direction (instead
// of sampling a curve) keeps every station exactly one kTendrilSpacing from its
// neighbour; the per-station twist rolls the ribbon's side vector around it.
Ribbon build_ribbon(const EffectRoot& root) {
    Ribbon ribbon;

    const uint32_t seed = root.seed;
    const float phases[6] = {
        signed_unit_of(seed * 8u + 1u) * kPi, signed_unit_of(seed * 8u + 2u) * kPi,
        signed_unit_of(seed * 8u + 3u) * kPi, signed_unit_of(seed * 8u + 4u) * kPi,
        signed_unit_of(seed * 8u + 5u) * kPi, signed_unit_of(seed * 8u + 6u) * kPi,
    };

    Vec3 heading = vec3_normalize(root.normal);
    if (vec3_dot(heading, heading) < 0.5f) {
        heading = Vec3{0.0f, 1.0f, 0.0f};  // a zero normal erupts upward
    }
    // A seeded lean off the normal: the two roots separate immediately instead
    // of sharing their first metres.
    heading = vec3_normalize(Vec3{
        heading.x + 0.30f * signed_unit_of(seed * 8u + 11u),
        heading.y + 0.30f * signed_unit_of(seed * 8u + 12u),
        heading.z + 0.30f * signed_unit_of(seed * 8u + 13u),
    });
    if (vec3_dot(heading, heading) < 0.5f) {
        heading = Vec3{0.0f, 1.0f, 0.0f};
    }

    Vec3 side = any_perpendicular(heading);
    Vec3 point = root.position;
    const float twist = 0.09f + 0.07f * unit_of(seed * 8u + 7u);  // rad per station

    for (int i = 0; i < kTendrilStations; ++i) {
        ribbon.points[static_cast<std::size_t>(i)] = point;
        ribbon.sides[static_cast<std::size_t>(i)] = side;
        if (i == kTendrilSegments) {
            break;
        }

        const float s = static_cast<float>(i) * kTendrilSpacing;
        const Vec3 bend{
            0.060f * std::sin(0.21f * s + phases[0]) + 0.030f * std::sin(0.63f * s + phases[3]),
            0.055f * std::sin(0.17f * s + phases[1]) + 0.025f * std::sin(0.71f * s + phases[4]),
            0.060f * std::sin(0.19f * s + phases[2]) + 0.030f * std::sin(0.67f * s + phases[5]),
        };
        Vec3 next = vec3_normalize(Vec3{
            heading.x + bend.x * kTendrilSpacing,
            heading.y + bend.y * kTendrilSpacing,
            heading.z + bend.z * kTendrilSpacing,
        });
        if (vec3_dot(next, next) < 0.5f) {
            next = heading;  // never reachable for finite inputs; keeps the loop total
        }

        // Parallel transport the side vector, then roll it by the station's
        // twist: the ribbon's face turns as the tendril climbs.
        Vec3 carried{side.x - next.x * vec3_dot(side, next),
                     side.y - next.y * vec3_dot(side, next),
                     side.z - next.z * vec3_dot(side, next)};
        if (vec3_dot(carried, carried) < 1e-8f) {
            carried = any_perpendicular(next);
        }
        carried = vec3_normalize(carried);
        const Vec3 rolled = vec3_cross(next, carried);
        const float c = std::cos(twist);
        const float t = std::sin(twist);
        side = vec3_normalize(Vec3{
            carried.x * c + rolled.x * t,
            carried.y * c + rolled.y * t,
            carried.z * c + rolled.z * t,
        });

        point = Vec3{point.x + next.x * kTendrilSpacing, point.y + next.y * kTendrilSpacing,
                     point.z + next.z * kTendrilSpacing};
        heading = next;
    }

    return ribbon;
}

// Sets the growth horizon and the point every uncollapsed station sits on.
void apply_growth(Ribbon& ribbon, float age) {
    const float grown = clamp01((age - kTendrilStartSeconds) / kTendrilGrowSeconds);
    ribbon.horizon = grown * static_cast<float>(kTendrilSegments);

    const int whole = std::min(static_cast<int>(ribbon.horizon), kTendrilSegments);
    if (whole >= kTendrilSegments) {
        ribbon.tip = ribbon.points[kTendrilSegments];
        return;
    }
    // The partially grown segment's interpolation, so the tip advances smoothly
    // between stations.
    const float fraction = ribbon.horizon - static_cast<float>(whole);
    const Vec3 a = ribbon.points[static_cast<std::size_t>(whole)];
    const Vec3 b = ribbon.points[static_cast<std::size_t>(whole) + 1];
    ribbon.tip = Vec3{a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction,
                      a.z + (b.z - a.z) * fraction};
}

bool is_grown(const Ribbon& ribbon, int station) {
    return static_cast<float>(station) <= ribbon.horizon;
}

Vec3 station_point(const Ribbon& ribbon, int station) {
    return is_grown(ribbon, station) ? ribbon.points[static_cast<std::size_t>(station)]
                                     : ribbon.tip;
}

// Half the station's 0.6 -> 0.05 m taper.
float station_half_width(int station) {
    const float t = static_cast<float>(station) / static_cast<float>(kTendrilSegments);
    return 0.5f * (kRootWidth + (kTipWidth - kRootWidth) * t);
}

struct PathSample {
    Vec3 point;
    Vec3 side;
};

// The grown path at a fractional station (used to hang the smoke quads along it).
PathSample sample_path(const Ribbon& ribbon, float station) {
    const float clamped = clampf(station, 0.0f, static_cast<float>(kTendrilSegments));
    const int index = std::min(static_cast<int>(clamped), kTendrilSegments - 1);
    const float fraction = clamped - static_cast<float>(index);
    const Vec3 a = ribbon.points[static_cast<std::size_t>(index)];
    const Vec3 b = ribbon.points[static_cast<std::size_t>(index) + 1];
    const Vec3 side_a = ribbon.sides[static_cast<std::size_t>(index)];
    const Vec3 side_b = ribbon.sides[static_cast<std::size_t>(index) + 1];
    const Vec3 side = vec3_normalize(Vec3{side_a.x + (side_b.x - side_a.x) * fraction,
                                          side_a.y + (side_b.y - side_a.y) * fraction,
                                          side_a.z + (side_b.z - side_a.z) * fraction});
    const bool have_side = vec3_dot(side, side) > 0.5f;
    return PathSample{
        Vec3{a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction,
             a.z + (b.z - a.z) * fraction},
        have_side ? side : side_a,
    };
}

// A 1.2 s emitter life, phase-offset per billboard and seeded so a frame is a
// function of its age alone (fract(age / life + offset)).
float life_phase(uint32_t seed, uint32_t slot, float age) {
    float phase = age / kBillboardLifeSeconds + unit_of(hash_u32(seed * 977u + slot));
    phase -= std::floor(phase);
    return phase;
}

struct TileUv {
    float u0, u1, v0, v1;
};

// One tile of a cols x rows atlas, inset by half a texel.
TileUv atlas_tile(int columns, int rows, int tile, float texel) {
    const int column = tile % columns;
    const int row = tile / columns;
    const float du = 1.0f / static_cast<float>(columns);
    const float dv = 1.0f / static_cast<float>(rows);
    TileUv uv;
    uv.u0 = static_cast<float>(column) * du + 0.5f * texel;
    uv.u1 = static_cast<float>(column + 1) * du - 0.5f * texel;
    uv.v0 = static_cast<float>(row) * dv + 0.5f * texel;
    uv.v1 = static_cast<float>(row + 1) * dv - 0.5f * texel;
    return uv;
}

// One camera-facing quad: `center` plus the camera's right/up, four vertices in
// the corner order (bottom-left, bottom-right, top-right, top-left) the index
// buffer's (0,1,2), (0,2,3) triangles expect.
void push_quad(std::vector<EffectVertex>& out, Vec3 center, Vec3 right, Vec3 up, float half_size,
               const TileUv& uv, const float tint[3], float alpha) {
    const Vec3 rx{right.x * half_size, right.y * half_size, right.z * half_size};
    const Vec3 uy{up.x * half_size, up.y * half_size, up.z * half_size};
    const Vec3 corners[4] = {
        Vec3{center.x - rx.x - uy.x, center.y - rx.y - uy.y, center.z - rx.z - uy.z},
        Vec3{center.x + rx.x - uy.x, center.y + rx.y - uy.y, center.z + rx.z - uy.z},
        Vec3{center.x + rx.x + uy.x, center.y + rx.y + uy.y, center.z + rx.z + uy.z},
        Vec3{center.x - rx.x + uy.x, center.y - rx.y + uy.y, center.z - rx.z + uy.z},
    };
    // v grows downward (the textures are top-down): bottom-left is (u0, v1).
    const float u[4] = {uv.u0, uv.u1, uv.u1, uv.u0};
    const float v[4] = {uv.v1, uv.v1, uv.v0, uv.v0};
    for (int corner = 0; corner < 4; ++corner) {
        out.push_back(EffectVertex{corners[corner].x, corners[corner].y, corners[corner].z,
                                   u[corner], v[corner], tint[0], tint[1], tint[2], alpha});
    }
}

// The pass starts dark and fades out before the 3.0 s window closes.
float burst_fade(float age) {
    const float rise = clamp01((age - kTendrilStartSeconds) / 0.15f);
    const float fall = clamp01((kTendrilEndSeconds - age) / 0.35f);
    return rise * fall;
}

// The camera's world right/up from the view-projection. A perspective matrix's
// rows 0 and 1 are the camera rotation's rows scaled by the focal terms, so
// normalizing them recovers the basis the billboards face the camera with.
void camera_basis(const Mat4& viewProj, Vec3& right, Vec3& up) {
    right = vec3_normalize(Vec3{viewProj.m[0], viewProj.m[4], viewProj.m[8]});
    up = vec3_normalize(Vec3{viewProj.m[1], viewProj.m[5], viewProj.m[9]});
    if (vec3_dot(right, right) < 0.5f) {
        right = Vec3{1.0f, 0.0f, 0.0f};  // an all-zero view-projection still draws finite quads
    }
    if (vec3_dot(up, up) < 0.5f) {
        up = Vec3{0.0f, 1.0f, 0.0f};
    }
}

// The renderer's tendril.metal, read from beside the binary (plan ruling R2: SDL
// compiles the MSL at runtime). A missing shader leaves the pass inert; the
// caller prints one line rather than aborting a purely visual feature.
bool read_shader_source(const char* file_name, std::vector<uint8_t>& source) {
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        std::fprintf(stderr, "effect: SDL_GetBasePath failed\n");
        return false;
    }
    const std::string path = std::string(base) + file_name;
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        std::fprintf(stderr, "effect: cannot read %s: %s\n", path.c_str(), SDL_GetError());
        return false;
    }
    source.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    SDL_free(data);
    return true;
}

void log_sdl(const char* what) {
    const char* error = SDL_GetError();
    std::fprintf(stderr, "effect: %s failed%s%s\n", what, (error != nullptr && error[0] != '\0') ? ": " : "",
                 (error != nullptr && error[0] != '\0') ? error : "");
}

SDL_GPUShader* create_effect_shader(SDL_GPUDevice* device, SDL_GPUShaderStage stage,
                                    const char* entry_point, uint32_t num_uniform_buffers,
                                    uint32_t num_samplers, const std::vector<uint8_t>& source) {
    SDL_GPUShaderCreateInfo info{};
    info.code_size = source.size();
    info.code = source.data();
    info.entrypoint = entry_point;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.stage = stage;
    info.num_samplers = num_samplers;
    info.num_storage_buffers = 0;
    info.num_uniform_buffers = num_uniform_buffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (shader == nullptr) {
        // SDL's Metal backend logs the compiler error through SDL_LogError and
        // does not always set SDL's error string, so name both places.
        std::fprintf(stderr,
                     "effect: SDL_CreateGPUShader(%s) failed; a Metal compiler error, if any, is "
                     "the SDL log line above\n",
                     entry_point);
    }
    return shader;
}

// Owns the transfer buffers of one upload for the duration of the call; a
// failure between mapping and the fence wait must not leak them.
struct TransferBufferGuard {
    SDL_GPUDevice* device;
    std::vector<SDL_GPUTransferBuffer*>& buffers;
    ~TransferBufferGuard() {
        for (SDL_GPUTransferBuffer* buffer : buffers) {
            SDL_ReleaseGPUTransferBuffer(device, buffer);
        }
    }
};

// Waits for a submission's copy pass to finish, then releases the fence: the
// transfer buffers may only be freed once the copy has run (renderer.cpp's
// upload pattern).
bool wait_for_fence(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd) {
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        log_sdl("SDL_SubmitGPUCommandBufferAndAcquireFence");
        return false;
    }
    const bool waited = SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    if (!waited) {
        log_sdl("SDL_WaitForGPUFences");
        return false;
    }
    return true;
}

// The static index list for one root count: ribbons, smoke, wisp atlas A, wisp
// atlas B — the four ranges draw() binds the premultiplied or additive pipeline
// for. Index values are 16-bit: the largest vertex is 963.
std::vector<uint16_t> build_indices(int roots) {
    std::vector<uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(kTendrilRibbonIndices) * roots +
                    (kTendrilSmokeQuads + kTendrilWispQuads) * 6);

    for (int root = 0; root < roots; ++root) {
        const uint16_t base = static_cast<uint16_t>(root * kTendrilRibbonVertices);
        for (int segment = 0; segment < kTendrilSegments; ++segment) {
            const uint16_t a = static_cast<uint16_t>(base + segment * 2);
            const uint16_t b = static_cast<uint16_t>(a + 1);
            const uint16_t c = static_cast<uint16_t>(a + 2);
            const uint16_t d = static_cast<uint16_t>(a + 3);
            const uint16_t quad[6] = {a, b, c, b, d, c};
            indices.insert(indices.end(), quad, quad + 6);
        }
    }

    const uint16_t billboards = static_cast<uint16_t>(roots * kTendrilRibbonVertices);
    const auto push_quads = [&indices](uint16_t first, int count) {
        for (int quad = 0; quad < count; ++quad) {
            const uint16_t base = static_cast<uint16_t>(first + quad * 4);
            const uint16_t corners[6] = {base, static_cast<uint16_t>(base + 1),
                                         static_cast<uint16_t>(base + 2), base,
                                         static_cast<uint16_t>(base + 2),
                                         static_cast<uint16_t>(base + 3)};
            indices.insert(indices.end(), corners, corners + 6);
        }
    };
    push_quads(billboards, kTendrilSmokeQuads);
    push_quads(static_cast<uint16_t>(billboards + kTendrilSmokeQuads * 4), kTendrilWispQuads / 2);
    push_quads(static_cast<uint16_t>(billboards + (kTendrilSmokeQuads + kTendrilWispQuads / 2) * 4),
               kTendrilWispQuads / 2);
    return indices;
}

}  // namespace

std::vector<EffectVertex> build_tendril_vertices(const EffectFrame& frame, const Mat4& viewProj) {
    std::vector<EffectVertex> vertices;
    if (!frame.active) {
        return vertices;
    }
    const int root_count = std::clamp(frame.rootCount, 0, 2);
    if (root_count == 0) {
        return vertices;
    }

    vertices.reserve(kTendrilMaxVertices);
    Vec3 right;
    Vec3 up;
    camera_basis(viewProj, right, up);

    // A non-finite clock is the empty burst, never NaN vertices.
    const float age = std::isfinite(frame.age) ? std::max(frame.age, 0.0f) : 0.0f;
    const float fade = burst_fade(age);

    std::array<Ribbon, 2> ribbons{};
    for (int root = 0; root < root_count; ++root) {
        ribbons[static_cast<std::size_t>(root)] = build_ribbon(frame.roots[root]);
        apply_growth(ribbons[static_cast<std::size_t>(root)], age);
    }

    // 1. The ribbons: 201 stations each, a triangle list forming the strip.
    for (int root = 0; root < root_count; ++root) {
        const Ribbon& ribbon = ribbons[static_cast<std::size_t>(root)];
        for (int station = 0; station < kTendrilStations; ++station) {
            const Vec3 center = station_point(ribbon, station);
            const Vec3 side = ribbon.sides[static_cast<std::size_t>(station)];
            // Collapsed (ungrown) stations are a single point: no width, so the
            // quads beyond the growth horizon are degenerate.
            const float half_width = is_grown(ribbon, station) ? station_half_width(station) : 0.0f;
            // v runs along the path, so the mask repeats kVeinRepeats times (the
            // veins sampler wraps v); u runs across the width exactly once.
            const float v = kVeinRepeats * static_cast<float>(station) /
                            static_cast<float>(kTendrilSegments);
            const Vec3 left{center.x - side.x * half_width, center.y - side.y * half_width,
                            center.z - side.z * half_width};
            const Vec3 right_point{center.x + side.x * half_width,
                                   center.y + side.y * half_width,
                                   center.z + side.z * half_width};
            vertices.push_back(EffectVertex{left.x, left.y, left.z, 0.0f, v, kVeinGlow[0],
                                            kVeinGlow[1], kVeinGlow[2], fade});
            vertices.push_back(EffectVertex{right_point.x, right_point.y, right_point.z, 1.0f, v,
                                            kVeinGlow[0], kVeinGlow[1], kVeinGlow[2], fade});
        }
    }

    // 2. The 24 smoke quads along the grown ribbon (the 2x2 tendril atlas). The
    //    squared fraction biases the spread toward the wound (the first quad
    //    sits at station 0.09, not 7 m up the path) while the last still reaches
    //    the grown tip.
    for (int quad = 0; quad < kTendrilSmokeQuads; ++quad) {
        const int root = quad % root_count;
        const EffectRoot& effect_root = frame.roots[root];
        const Ribbon& ribbon = ribbons[static_cast<std::size_t>(root)];
        const uint32_t slot = effect_root.seed * 4099u + static_cast<uint32_t>(quad) * 131u;

        const float fraction = (static_cast<float>(quad) + 0.5f) /
                               static_cast<float>(kTendrilSmokeQuads);
        const PathSample sample = sample_path(ribbon, fraction * fraction * ribbon.horizon);
        const float lateral = 0.45f * signed_unit_of(slot + 1u);
        const float lift = 0.25f * signed_unit_of(slot + 2u);
        const Vec3 center{sample.point.x + sample.side.x * lateral,
                          sample.point.y + sample.side.y * lateral + lift,
                          sample.point.z + sample.side.z * lateral};

        const float phase = life_phase(effect_root.seed, static_cast<uint32_t>(quad), age);
        const float envelope = std::sin(kPi * phase);
        const float size = 0.4f + 0.8f * envelope;  // 0.4 .. 1.2 m
        const float alpha = 0.55f * envelope * fade;
        const TileUv uv = atlas_tile(2, 2, quad % 4, kSmokeTexel);
        push_quad(vertices, center, right, up, 0.5f * size, uv, kSmokeTint, alpha);
    }

    // 3. The 16 wisp puffs at the root, split by atlas so each draw binds one
    //    texture: the even quads use 8x8_wispysmoke02, the odd ones the sharp
    //    variant.
    for (int group = 0; group < 2; ++group) {
        for (int quad = group; quad < kTendrilWispQuads; quad += 2) {
            const int root = quad % root_count;
            const EffectRoot& effect_root = frame.roots[root];
            const uint32_t slot = effect_root.seed * 7919u + static_cast<uint32_t>(quad) * 37u;

            Vec3 normal = vec3_normalize(effect_root.normal);
            if (vec3_dot(normal, normal) < 0.5f) {
                normal = Vec3{0.0f, 1.0f, 0.0f};
            }
            const Vec3 radial = unit_sphere(slot + 1u, slot + 2u);
            const float radius = 0.15f + 0.55f * unit_of(slot + 3u);
            const float lift = 0.10f + 0.30f * unit_of(slot + 4u);
            const Vec3 center{effect_root.position.x + radial.x * radius + normal.x * lift,
                              effect_root.position.y + radial.y * radius + normal.y * lift,
                              effect_root.position.z + radial.z * radius + normal.z * lift};

            const float phase = life_phase(effect_root.seed, static_cast<uint32_t>(quad) + 100u, age);
            const float envelope = std::sin(kPi * phase);
            const float size = 0.6f + 1.4f * envelope;  // 0.6 .. 2.0 m
            const float alpha = 0.45f * envelope * fade;
            const TileUv uv = atlas_tile(8, 8, (quad * 5) % 64, kWispTexel);
            push_quad(vertices, center, right, up, 0.5f * size, uv, kWispTint, alpha);
        }
    }

    return vertices;
}

std::array<TextureCpu, 4> load_effect_textures(const std::string& libraryRoot) {
    // Task 0's converted, library-relative PNGs; the order is effect.h's.
    static constexpr std::array<const char*, 4> kPaths = {
        "textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png",
        "textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.png",
        "textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png",
        "textures/shadow/characters/common/_ps4/death_tentacle_veins.png",
    };

    std::array<TextureCpu, 4> textures{};
    for (std::size_t index = 0; index < kPaths.size(); ++index) {
        const std::string path =
            (std::filesystem::path(libraryRoot) / kPaths[index]).generic_string();
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            // The path names the library root too: a wrong SOTC_LIB and a missing
            // file read differently.
            throw std::runtime_error("effect: cannot load texture " + path + " (" +
                                     kPaths[index] + "): " + stbi_failure_reason());
        }

        TextureCpu& texture = textures[index];
        texture.width = width;
        texture.height = height;
        // stb_image's rows are top-down, the TextureCpu/glTF convention
        // (asset_io.cpp); no flip.
        const std::size_t pixel_bytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        texture.rgba8.assign(pixels, pixels + pixel_bytes);
        texture.sourceRelPath = kPaths[index];
        stbi_image_free(pixels);
    }
    return textures;
}

EffectRenderer::~EffectRenderer() { release(); }

void EffectRenderer::release() {
    if (device_ == nullptr) {
        return;
    }
    if (vertex_transfer_ != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, vertex_transfer_);
        vertex_transfer_ = nullptr;
    }
    if (vertex_buffer_ != nullptr) {
        SDL_ReleaseGPUBuffer(device_, vertex_buffer_);
        vertex_buffer_ = nullptr;
    }
    for (SDL_GPUBuffer*& buffer : index_buffers_) {
        if (buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device_, buffer);
            buffer = nullptr;
        }
    }
    for (SDL_GPUTexture*& texture : textures_) {
        if (texture != nullptr) {
            SDL_ReleaseGPUTexture(device_, texture);
            texture = nullptr;
        }
    }
    if (sampler_ != nullptr) {
        SDL_ReleaseGPUSampler(device_, sampler_);
        sampler_ = nullptr;
    }
    if (vein_sampler_ != nullptr) {
        SDL_ReleaseGPUSampler(device_, vein_sampler_);
        vein_sampler_ = nullptr;
    }
    if (premultiplied_pipeline_ != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device_, premultiplied_pipeline_);
        premultiplied_pipeline_ = nullptr;
    }
    if (additive_pipeline_ != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device_, additive_pipeline_);
        additive_pipeline_ = nullptr;
    }
    if (vertex_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, vertex_shader_);
        vertex_shader_ = nullptr;
    }
    if (premultiplied_fragment_ != nullptr) {
        SDL_ReleaseGPUShader(device_, premultiplied_fragment_);
        premultiplied_fragment_ = nullptr;
    }
    if (additive_fragment_ != nullptr) {
        SDL_ReleaseGPUShader(device_, additive_fragment_);
        additive_fragment_ = nullptr;
    }
    device_ = nullptr;
}

bool EffectRenderer::create(SDL_GPUDevice* device, const std::array<TextureCpu, 4>& textures,
                            SDL_GPUTextureFormat colorFormat) {
    if (device == nullptr) {
        return false;
    }
    if (device_ != nullptr) {
        // Already created: report the live pass rather than failing a second
        // call. Only a different device is an error (the object holds one
        // device's resources).
        return device_ == device;
    }
    device_ = device;

    if (!create_pipeline(colorFormat) || !upload_textures(textures) || !upload_indices()) {
        release();
        return false;
    }

    SDL_GPUBufferCreateInfo buffer_info{};
    buffer_info.size = static_cast<uint32_t>(kTendrilMaxVertices * sizeof(EffectVertex));
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertex_buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
    if (vertex_buffer_ == nullptr) {
        log_sdl("SDL_CreateGPUBuffer(tendril vertices)");
        release();
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = buffer_info.size;
    vertex_transfer_ = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (vertex_transfer_ == nullptr) {
        log_sdl("SDL_CreateGPUTransferBuffer(tendril vertices)");
        release();
        return false;
    }

    return true;
}

bool EffectRenderer::create_pipeline(SDL_GPUTextureFormat colorFormat) {
    std::vector<uint8_t> source;
    if (!read_shader_source("tendril.metal", source)) {
        return false;
    }

    // Vertex: one view-projection push. Fragment: the sampled texture and the
    // pass's tint, in one of two entry points (premultiplied / additive).
    vertex_shader_ =
        create_effect_shader(device_, SDL_GPU_SHADERSTAGE_VERTEX, "tendril_vertex", 1, 0, source);
    premultiplied_fragment_ = create_effect_shader(
        device_, SDL_GPU_SHADERSTAGE_FRAGMENT, "tendril_fragment_premultiplied", 0, 1, source);
    additive_fragment_ = create_effect_shader(
        device_, SDL_GPU_SHADERSTAGE_FRAGMENT, "tendril_fragment_additive", 0, 1, source);
    if (vertex_shader_ == nullptr || premultiplied_fragment_ == nullptr ||
        additive_fragment_ == nullptr) {
        return false;
    }

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    // The atlas sampler clamps both axes: the tile UVs are inset by half a
    // texel, and no tile may sample its neighbour's edge. The ribbon's mask
    // repeats along its length and gets the wrapping sampler below.
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.max_anisotropy = 1.0f;
    sampler_info.max_lod = 0.0f;  // every effect texture has one mip level
    sampler_ = SDL_CreateGPUSampler(device_, &sampler_info);
    if (sampler_ == nullptr) {
        log_sdl("SDL_CreateGPUSampler(tendril)");
        return false;
    }

    // The ribbon's vein mask repeats along `v`, so that axis wraps. `u` stays
    // clamped: it runs exactly 0..1 across the width, where a wrap would pull
    // the texture's opposite edge column into the ribbon's right edge.
    SDL_GPUSamplerCreateInfo vein_sampler_info = sampler_info;
    vein_sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    vein_sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    vein_sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    vein_sampler_ = SDL_CreateGPUSampler(device_, &vein_sampler_info);
    if (vein_sampler_ == nullptr) {
        log_sdl("SDL_CreateGPUSampler(tendril veins)");
        return false;
    }

    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(EffectVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(EffectVertex, px)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(EffectVertex, u)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(EffectVertex, r)};

    SDL_GPUVertexInputState vertex_input{};
    vertex_input.vertex_buffer_descriptions = &vertex_buffer;
    vertex_input.num_vertex_buffers = 1;
    vertex_input.vertex_attributes = attributes;
    vertex_input.num_vertex_attributes = 3;

    SDL_GPUDepthStencilState depth_stencil{};
    depth_stencil.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    depth_stencil.enable_depth_test = true;
    depth_stencil.enable_depth_write = false;  // the effect never occludes itself or the scene

    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
    rasterizer.cull_mode = SDL_GPU_CULLMODE_NONE;  // quads and a twisting ribbon: both faces are in
    rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizer.enable_depth_clip = true;

    SDL_GPUMultisampleState multisample{};
    multisample.sample_count = SDL_GPU_SAMPLECOUNT_1;

    // Premultiplied alpha: the smoke's rgb arrives already multiplied by its
    // coverage, so it darkens the scene by (1 - a) without a second scale.
    SDL_GPUColorTargetBlendState premultiplied{};
    premultiplied.enable_blend = true;
    premultiplied.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    premultiplied.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    premultiplied.color_blend_op = SDL_GPU_BLENDOP_ADD;
    premultiplied.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    premultiplied.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    premultiplied.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    // Additive: the wisp haze and the vein glow add light, masked by the sampled
    // alpha.
    SDL_GPUColorTargetBlendState additive{};
    additive.enable_blend = true;
    additive.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    additive.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    additive.color_blend_op = SDL_GPU_BLENDOP_ADD;
    additive.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    additive.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    additive.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUShader* const fragments[2] = {premultiplied_fragment_, additive_fragment_};
    const SDL_GPUColorTargetBlendState blends[2] = {premultiplied, additive};
    SDL_GPUGraphicsPipeline** pipelines[2] = {&premultiplied_pipeline_, &additive_pipeline_};

    for (int variant = 0; variant < 2; ++variant) {
        SDL_GPUColorTargetDescription target{};
        target.format = colorFormat;
        target.blend_state = blends[variant];

        SDL_GPUGraphicsPipelineTargetInfo info{};
        info.color_target_descriptions = &target;
        info.num_color_targets = 1;
        info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.has_depth_stencil_target = true;

        SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.vertex_shader = vertex_shader_;
        pipeline_info.fragment_shader = fragments[variant];
        pipeline_info.vertex_input_state = vertex_input;
        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline_info.rasterizer_state = rasterizer;
        pipeline_info.multisample_state = multisample;
        pipeline_info.depth_stencil_state = depth_stencil;
        pipeline_info.target_info = info;

        *pipelines[variant] = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
        if (*pipelines[variant] == nullptr) {
            log_sdl(variant == 0 ? "SDL_CreateGPUGraphicsPipeline(tendril premultiplied)"
                                 : "SDL_CreateGPUGraphicsPipeline(tendril additive)");
            return false;
        }
    }
    return true;
}

bool EffectRenderer::upload_textures(const std::array<TextureCpu, 4>& source_textures) {
    for (const TextureCpu& texture : source_textures) {
        if (texture.width <= 0 || texture.height <= 0 ||
            texture.rgba8.size() !=
                static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height) * 4) {
            std::fprintf(stderr, "effect: texture %s has bad dimensions\n",
                         texture.sourceRelPath.c_str());
            return false;
        }
    }

    std::vector<SDL_GPUTransferBuffer*> transfer_buffers;
    TransferBufferGuard transfer_guard{device_, transfer_buffers};

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        log_sdl("SDL_AcquireGPUCommandBuffer");
        return false;
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (copy_pass == nullptr) {
        SDL_CancelGPUCommandBuffer(cmd);
        log_sdl("SDL_BeginGPUCopyPass(tendril textures)");
        return false;
    }

    for (std::size_t index = 0; index < source_textures.size(); ++index) {
        const TextureCpu& texture = source_textures[index];
        SDL_GPUTextureCreateInfo texture_info{};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = static_cast<uint32_t>(texture.width);
        texture_info.height = static_cast<uint32_t>(texture.height);
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        textures_[index] = SDL_CreateGPUTexture(device_, &texture_info);
        if (textures_[index] == nullptr) {
            SDL_CancelGPUCommandBuffer(cmd);
            log_sdl("SDL_CreateGPUTexture(tendril)");
            return false;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<uint32_t>(texture.rgba8.size());
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            SDL_CancelGPUCommandBuffer(cmd);
            log_sdl("SDL_CreateGPUTransferBuffer(tendril texture)");
            return false;
        }
        transfer_buffers.push_back(transfer);

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            SDL_CancelGPUCommandBuffer(cmd);
            log_sdl("SDL_MapGPUTransferBuffer(tendril texture)");
            return false;
        }
        std::memcpy(mapped, texture.rgba8.data(), texture.rgba8.size());
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<uint32_t>(texture.width);
        source.rows_per_layer = static_cast<uint32_t>(texture.height);
        SDL_GPUTextureRegion destination{};
        destination.texture = textures_[index];
        destination.w = static_cast<uint32_t>(texture.width);
        destination.h = static_cast<uint32_t>(texture.height);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }

    SDL_EndGPUCopyPass(copy_pass);
    return wait_for_fence(device_, cmd);
}

bool EffectRenderer::upload_indices() {
    const std::vector<uint16_t> one_root = build_indices(1);
    const std::vector<uint16_t> two_roots = build_indices(2);
    const uint32_t one_bytes = static_cast<uint32_t>(one_root.size() * sizeof(uint16_t));
    const uint32_t two_bytes = static_cast<uint32_t>(two_roots.size() * sizeof(uint16_t));
    // The second region 16-byte aligned, the copy's offset requirement.
    const uint32_t two_offset = (one_bytes + 15u) & ~15u;

    std::vector<SDL_GPUTransferBuffer*> transfer_buffers;
    TransferBufferGuard transfer_guard{device_, transfer_buffers};

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = two_offset + two_bytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (transfer == nullptr) {
        log_sdl("SDL_CreateGPUTransferBuffer(tendril indices)");
        return false;
    }
    transfer_buffers.push_back(transfer);

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (mapped == nullptr) {
        log_sdl("SDL_MapGPUTransferBuffer(tendril indices)");
        return false;
    }
    std::memcpy(mapped, one_root.data(), one_bytes);
    std::memcpy(static_cast<uint8_t*>(mapped) + two_offset, two_roots.data(), two_bytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUBufferCreateInfo buffer_info{};
    buffer_info.size = one_bytes;
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    index_buffers_[0] = SDL_CreateGPUBuffer(device_, &buffer_info);
    buffer_info.size = two_bytes;
    index_buffers_[1] = SDL_CreateGPUBuffer(device_, &buffer_info);
    if (index_buffers_[0] == nullptr || index_buffers_[1] == nullptr) {
        log_sdl("SDL_CreateGPUBuffer(tendril indices)");
        return false;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        log_sdl("SDL_AcquireGPUCommandBuffer");
        return false;
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (copy_pass == nullptr) {
        SDL_CancelGPUCommandBuffer(cmd);
        log_sdl("SDL_BeginGPUCopyPass(tendril indices)");
        return false;
    }
    SDL_GPUTransferBufferLocation source{transfer, 0};
    SDL_GPUBufferRegion destination{index_buffers_[0], 0, one_bytes};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    source.offset = two_offset;
    destination.buffer = index_buffers_[1];
    destination.size = two_bytes;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    return wait_for_fence(device_, cmd);
}

bool EffectRenderer::upload_vertices(int byteCount) {
    void* mapped = SDL_MapGPUTransferBuffer(device_, vertex_transfer_, false);
    if (mapped == nullptr) {
        log_sdl("SDL_MapGPUTransferBuffer(tendril vertices)");
        return false;
    }
    std::memcpy(mapped, vertices_.data(), static_cast<std::size_t>(byteCount));
    SDL_UnmapGPUTransferBuffer(device_, vertex_transfer_);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        log_sdl("SDL_AcquireGPUCommandBuffer");
        return false;
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (copy_pass == nullptr) {
        SDL_CancelGPUCommandBuffer(cmd);
        log_sdl("SDL_BeginGPUCopyPass(tendril vertices)");
        return false;
    }
    const SDL_GPUTransferBufferLocation source{vertex_transfer_, 0};
    const SDL_GPUBufferRegion destination{vertex_buffer_, 0, static_cast<uint32_t>(byteCount)};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    // The copy is its own submission, ordered before the frame's render pass on
    // the same queue; the fence wait lets the next frame reuse the transfer
    // buffer. A ring of vertex buffers would remove the wait if profiling asks.
    return wait_for_fence(device_, cmd);
}

void EffectRenderer::update(const EffectFrame& frame, const Mat4& viewProj) {
    view_proj_ = viewProj;
    roots_ = 0;
    vertex_count_ = 0;
    if (device_ == nullptr) {
        return;
    }

    vertices_ = build_tendril_vertices(frame, view_proj_);
    if (vertices_.empty()) {
        return;
    }

    const int byte_count = static_cast<int>(vertices_.size() * sizeof(EffectVertex));
    if (!upload_vertices(byte_count)) {
        return;  // draw() stays a no-op until an upload succeeds
    }
    vertex_count_ = static_cast<int>(vertices_.size());
    roots_ = std::clamp(frame.rootCount, 0, 2);
}

void EffectRenderer::update(const EffectFrame& frame) { update(frame, view_proj_); }

void EffectRenderer::draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& viewProj) {
    view_proj_ = viewProj;
    if (device_ == nullptr || vertex_count_ == 0 || roots_ < 1 || roots_ > 2) {
        return;
    }

    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(Mat4));

    const SDL_GPUBufferBinding vertex_binding{vertex_buffer_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    const SDL_GPUBufferBinding index_binding{index_buffers_[static_cast<std::size_t>(roots_ - 1)], 0};
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // The static index layout (build_indices): ribbons, smoke, wisp A, wisp B.
    const uint32_t ribbon_first = 0;
    const uint32_t ribbon_count = static_cast<uint32_t>(kTendrilRibbonIndices) * static_cast<uint32_t>(roots_);
    const uint32_t smoke_first = static_cast<uint32_t>(kTendrilRibbonIndices) * static_cast<uint32_t>(roots_);
    const uint32_t smoke_count = static_cast<uint32_t>(kTendrilSmokeQuads * 6);
    const uint32_t wisp_a_first = smoke_first + smoke_count;
    const uint32_t wisp_b_first = wisp_a_first + static_cast<uint32_t>(kTendrilWispQuads / 2 * 6);
    const uint32_t wisp_count = static_cast<uint32_t>(kTendrilWispQuads / 2 * 6);

    // The dark layer first — the ribbons' black veins and the smoke column —
    // then the additive glow over it.
    const SDL_GPUTextureSamplerBinding vein_binding{textures_[3], vein_sampler_};
    SDL_BindGPUGraphicsPipeline(pass, premultiplied_pipeline_);
    SDL_BindGPUFragmentSamplers(pass, 0, &vein_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, ribbon_count, 1, ribbon_first, 0, 0);

    const SDL_GPUTextureSamplerBinding smoke_binding{textures_[2], sampler_};
    SDL_BindGPUFragmentSamplers(pass, 0, &smoke_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, smoke_count, 1, smoke_first, 0, 0);

    SDL_BindGPUGraphicsPipeline(pass, additive_pipeline_);
    const SDL_GPUTextureSamplerBinding wisp_a_binding{textures_[0], sampler_};
    SDL_BindGPUFragmentSamplers(pass, 0, &wisp_a_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, wisp_count, 1, wisp_a_first, 0, 0);

    const SDL_GPUTextureSamplerBinding wisp_b_binding{textures_[1], sampler_};
    SDL_BindGPUFragmentSamplers(pass, 0, &wisp_b_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, wisp_count, 1, wisp_b_first, 0, 0);

    SDL_BindGPUFragmentSamplers(pass, 0, &vein_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, ribbon_count, 1, ribbon_first, 0, 0);
}
