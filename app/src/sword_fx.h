#pragma once

// The sword's light pass (S9 Task 5, design D7): the fan of eight beam lines
// from the sword's tip, a flare sprite at the tip and a hit sprite where a
// locked ray lands. It reuses `tendril.metal`'s additive pipeline and
// `EffectVertex` but owns its own small buffers and textures, so the S6
// tendril pass (`effect.h/.cpp`) is untouched.
//
// The builder is pure: the same frame and camera twice is byte-identical.
//
// Vertex order (SwordFxRenderer's static index buffer depends on it):
//   [0, 32)   the 8 line quads, tip -> tip + line * length, camera-facing,
//             `width` wide (edges at the axis ± side * width / 2)
//   [32, 36)  the flare at the tip (flareSize wide)
//   [36, 40)  the hit sprite at hitPoint (hitSize wide), only when `hit`
// An inactive frame or alpha <= 0 is empty.
//
// The `std::array<TextureCpu, 4>` order is:
//   [0] vertical_solid_beams_01_pfx   (the lines' base, atlas frame 0)
//   [1] vertical_beams_wave_01_pfx    (the lines' overlay, atlas frame 0)
//   [2] soft_glow_01_falloff_pfx      (the flare)
//   [3] sword_reaction's texture      (the hit)

#include <array>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "effect.h"
#include "math3d.h"
#include "scene.h"

struct SwordFxFrame {
    bool active = false;
    Vec3 tip{};
    std::array<Vec3, 8> lines{};  // the fan's directions (sword.h's beam)
    float length = 0, width = 0, alpha = 0;  // alpha = sword_intensity
    float flareSize = 0.6f;  // source: authored
    bool hit = false;
    Vec3 hitPoint{};
    float hitSize = 2.0f;  // source: authored
    int atlasCols = 1, atlasRows = 1;  // road_points.h's kSwordAtlasGrid; the lines sample frame 0
};

inline constexpr int kSwordLineQuads = 8;
inline constexpr int kSwordMaxVertices = (kSwordLineQuads + 2) * 4;  // 40

std::vector<EffectVertex> build_sword_vertices(const SwordFxFrame& frame, const Mat4& viewProj);

// The four PNGs at the caller's library-relative paths (Task 8 passes
// road_points.h's kSwordTextures), decoded RGBA8 top-down. Throws
// std::runtime_error naming the path when one is missing or undecodable.
std::array<TextureCpu, 4> load_sword_textures(const std::string& libraryRoot,
                                              const std::array<std::string, 4>& rel);

// The pass's GPU state: tendril.metal's additive pipeline, one clamped
// sampler, four textures, a 40-vertex dynamic buffer and one static index
// buffer. create() returns false (one stderr line) rather than throwing; a
// not-created renderer's update/draw are no-ops. draw() runs inside the
// renderer's scene pass (LESS_EQUAL depth, no depth write).
class SwordFxRenderer {
public:
    SwordFxRenderer() = default;
    ~SwordFxRenderer();
    SwordFxRenderer(const SwordFxRenderer&) = delete;
    SwordFxRenderer& operator=(const SwordFxRenderer&) = delete;

    bool create(SDL_GPUDevice* device, const std::array<TextureCpu, 4>& textures,
                SDL_GPUTextureFormat colorFormat);
    void update(const SwordFxFrame& frame, const Mat4& viewProj);
    void draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& viewProj);
    void release();

private:
    bool create_pipeline(SDL_GPUTextureFormat colorFormat);
    bool upload(const void* data, uint32_t bytes, SDL_GPUBuffer* buffer);
    bool upload_textures(const std::array<TextureCpu, 4>& textures);

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShader* vertex_shader_ = nullptr;
    SDL_GPUShader* fragment_shader_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    std::array<SDL_GPUTexture*, 4> textures_{};
    SDL_GPUBuffer* vertex_buffer_ = nullptr;
    SDL_GPUBuffer* index_buffer_ = nullptr;
    int vertex_count_ = 0;
};
