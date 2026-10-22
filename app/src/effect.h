#pragma once

// The tendril effect (S6 Task 4, design §7 D4/§8.2): the viewer's first
// world-space blended pass. Valus's death erupts black tendrils from the wound;
// the geometry here is a **labeled reconstruction** with the game's own
// textures (design D1: the original `.csdr` simulator is not ported).
//
// Everything but EffectRenderer is pure: the builder is a function of the frame
// (roots, age) and the camera's view-projection, so the same frame twice is
// byte-identical and the Task 5/6 stills are deterministic.
//
// Vertex order (EffectRenderer's static index buffer depends on it):
//   [0 .. 402*roots)        the ribbons, one 402-vertex strip per root
//   [.. + 96)               the 24 smoke quads (4 vertices each)
//   [.. + 32) / [.. + 32)   the 8 + 8 wisp quads, one atlas each
// A ribbon station's two vertices sit at center ± side * width / 2, so their
// midpoint is the centerline point the 0.25 m spacing is measured on.
//
// The `std::array<TextureCpu, 4>` order is:
//   [0] 8x8_wispysmoke02_1024_white_pfx   (8x8 atlas)
//   [1] 8x8_smoke_wispy_sharp_1024_pfx    (8x8 atlas)
//   [2] 2x2_smoke_tendrils_01_pfx         (2x2 atlas)
//   [3] death_tentacle_veins              (the ribbon's mask)

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "math3d.h"
#include "scene.h"

// One tendril root: the wound's world position, its outward direction (the
// curl's initial heading) and the deterministic noise seed. The app passes the
// root index as the seed, so the two tendrils never trace the same curl.
struct EffectRoot {
    Vec3 position;
    Vec3 normal;
    uint32_t seed;
};

// One frame of the burst. `age` is the death clock in seconds (the burst begins
// at kTendrilStartSeconds = the death machine's 0.4 s and the ribbon is fully
// grown at 1.6 s); `rootCount` is clamped to the two-element array. active=false
// is the no-op — the empty frame the renderer draws nothing for.
struct EffectFrame {
    std::array<EffectRoot, 2> roots{};
    int rootCount = 0;
    float age = 0.0f;
    bool active = false;
};

// One GPU vertex: world position, texture UV, RGBA tint (the smoke's premultiplied
// path uses rgb, the additive path emits rgb through the texture's alpha).
struct EffectVertex {
    float px, py, pz;
    float u, v;
    float r, g, b, a;
};
static_assert(sizeof(EffectVertex) == 36, "EffectVertex must match the pipeline vertex layout");

// The binary's geometry: 201 stations 0.25 m apart (200 segments, a 50 m path).
inline constexpr int kTendrilSegments = 200;
inline constexpr int kTendrilStations = kTendrilSegments + 1;
inline constexpr float kTendrilSpacing = 0.25f;
inline constexpr int kTendrilRibbonVertices = 2 * kTendrilStations;  // 402
// 24 smoke quads on the ribbon and 16 wisp puffs at the root, per frame.
inline constexpr int kTendrilSmokeQuads = 24;
inline constexpr int kTendrilWispQuads = 16;
inline constexpr int kTendrilBillboardVertices = (kTendrilSmokeQuads + kTendrilWispQuads) * 4;  // 160
inline constexpr int kTendrilRibbonIndices = kTendrilSegments * 6;  // 1200 per root
inline constexpr int kTendrilMaxVertices = 2 * kTendrilRibbonVertices + kTendrilBillboardVertices;

// The burst's window (design §8.1): the ribbon starts at 0.4 s, grows for 1.2 s
// and is steady until the window's end at 3.0 s. Kept equal to death.h's
// constants by a static_assert in effect.cpp.
inline constexpr float kTendrilStartSeconds = 0.4f;
inline constexpr float kTendrilGrowSeconds = 1.2f;
inline constexpr float kTendrilEndSeconds = 3.0f;

// The frame's world-space triangle list: two 402-vertex ribbons (when both roots
// are present) and 160 billboard vertices. An inactive or root-less frame is
// empty. Billboard orientation comes from the view-projection's camera rows, so
// the same frame with two different cameras is two different vertex sets; the
// same frame and camera twice is byte-identical.
std::vector<EffectVertex> build_tendril_vertices(const EffectFrame& frame, const Mat4& viewProj);

// The four shipped effect PNGs, decoded RGBA8 top-down (the glTF image origin,
// the same convention as asset_io.cpp). Throws std::runtime_error naming the
// path when a file is missing or undecodable.
std::array<TextureCpu, 4> load_effect_textures(const std::string& libraryRoot);

// The tendril pass's GPU state: two pipelines over one `tendril.metal` (a
// premultiplied-alpha smoke pipeline and an additive wisp/ribbon pipeline), two
// samplers (the clamped atlas one and the ribbon's v-wrapping mask one), four
// textures, a persistent dynamic vertex buffer and one static index buffer per
// root count (1 and 2; the billboard vertex base moves with the ribbon count, so
// the index values cannot be shared).
//
// create() is called once, after the device is up; it returns false (and prints
// one line) rather than throwing, so a visual pass can never abort the run. A
// not-created renderer's update/draw are no-ops.
class EffectRenderer {
public:
    EffectRenderer() = default;
    ~EffectRenderer();

    EffectRenderer(const EffectRenderer&) = delete;
    EffectRenderer& operator=(const EffectRenderer&) = delete;

    // `colorFormat` must be the render pipeline's color target format (the
    // renderer passes its swapchain format). The default is macOS's usual
    // B8G8R8A8_UNORM swapchain for a caller with no renderer at hand.
    //
    // Idempotent: a second call for the same device returns the live pass's
    // state instead of failing (a caller that re-runs startup must not disable
    // a working pass); a different device returns false.
    bool create(SDL_GPUDevice* device, const std::array<TextureCpu, 4>& textures,
                SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);

    // Builds this frame's vertices and uploads them (one copy submission and a
    // fence wait; the same pattern upload_pose uses). Passing the camera keeps
    // the billboards facing it; the one-argument form uses the camera of the
    // last draw().
    void update(const EffectFrame& frame, const Mat4& viewProj);
    void update(const EffectFrame& frame);

    // Draws the pass into an already-open render pass whose color and depth
    // targets are the renderer's (LESS_EQUAL depth test, no depth write, no
    // culling). A no-op until update() has produced geometry.
    void draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& viewProj);

    // Releases every GPU resource; idempotent (the destructor calls it).
    void release();

private:
    bool create_pipeline(SDL_GPUTextureFormat colorFormat);
    bool upload_textures(const std::array<TextureCpu, 4>& textures);
    bool upload_indices();
    bool upload_vertices(int byteCount);

    SDL_GPUDevice* device_ = nullptr;

    SDL_GPUShader* vertex_shader_ = nullptr;
    SDL_GPUShader* premultiplied_fragment_ = nullptr;
    SDL_GPUShader* additive_fragment_ = nullptr;
    SDL_GPUGraphicsPipeline* premultiplied_pipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* additive_pipeline_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    // The ribbon's mask sampler: u clamped, v wrapped (kVeinRepeats).
    SDL_GPUSampler* vein_sampler_ = nullptr;

    std::array<SDL_GPUTexture*, 4> textures_{};

    // Fixed capacity: the frame's worst case is two ribbons and all billboards
    // (964 vertices). update() copies only the used range.
    SDL_GPUBuffer* vertex_buffer_ = nullptr;
    SDL_GPUTransferBuffer* vertex_transfer_ = nullptr;
    // Index buffers for one and two roots; index 0 is the one-root layout.
    std::array<SDL_GPUBuffer*, 2> index_buffers_{};

    // The retained builder output (reused across frames, never touched by draw).
    std::vector<EffectVertex> vertices_;
    Mat4 view_proj_{};
    int roots_ = 0;
    int vertex_count_ = 0;
};
