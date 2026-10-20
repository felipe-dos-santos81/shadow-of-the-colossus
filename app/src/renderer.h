#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "hud.h"
#include "math3d.h"
#include "scene.h"

// Draws a loaded Scene with SDL_GPU/Metal into an offscreen color target, and
// reads that target back as RGBA8 pixels — the same content that is copied to
// the swapchain, so `--screenshot` shows exactly what the window shows.
//
// Lifecycle: construct once (creates the pipeline, the shader, and the
// sampler), call upload(scene) once for the scene to draw, then render_frame
// per frame. Every setup failure throws std::runtime_error carrying the SDL or
// Metal error text; the app reports it and exits non-zero.
class Renderer {
public:
    Renderer(SDL_GPUDevice* device, SDL_Window* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Buffers and textures for `scene` on the GPU. The handle arrays mirror
    // scene.meshes / scene.textures, so the scene layout must not change after
    // this call (the app loads one model and keeps it). Also creates one joint
    // palette buffer per scene rig, each sized by that rig's own joint count (a
    // single identity matrix when the rig has no skin, and one identity fallback
    // buffer when the scene has no rigs at all).
    void upload(const Scene& scene);

    // Copies `palette` (one matrix per joint of rig `rigIndex`, in that rig's
    // joint order) into that rig's own palette buffer — never another rig's.
    // Called once per frame per rig with animation_pose()'s result. No-op when
    // the rig has no skin; throws when `rigIndex` is no rig of the uploaded
    // scene or the size does not match that rig's uploaded joint count.
    void upload_pose(std::size_t rigIndex, const std::vector<Mat4>& palette);

    // Renders the offscreen target (clear + one draw per mesh, pushing the
    // view-projection once and the per-mesh model + material uniforms), then —
    // when acquire_swapchain — copies it to the swapchain in the same submit.
    // A frame whose swapchain cannot be acquired (too many frames in flight,
    // minimized window) is skipped, not an error.
    void render_frame(const Scene& scene, const Mat4& viewProj, bool acquire_swapchain);

    // The last rendered frame as RGBA8, row 0 = top. Only valid after a
    // render_frame; throws if nothing has been rendered yet.
    std::vector<uint8_t> read_pixels();

    // Stores the overlay state drawn at the end of every render_frame: the two
    // stamina/health bars and, when SDL_ttf initialized, the "STAMINA <n>"
    // label. visible=false draws nothing (the default). Never throws: a text
    // failure degrades to bars only.
    void set_hud(const HudState& state);

private:
    struct MeshGpu {
        SDL_GPUBuffer* vertex_buffer;
        SDL_GPUBuffer* index_buffer;
        uint32_t index_count;
    };

    // One rig's joint palette (vertex storage buffer). `joint_count` is the
    // rig's own skin joint count — the palette size upload_pose expects for this
    // rig (0 = the rig has no skin, and the single identity matrix stands). The
    // buffer always holds at least one matrix, so every draw has a valid
    // binding.
    struct PoseGpu {
        SDL_GPUBuffer* buffer = nullptr;
        uint32_t joint_count = 0;
    };

    void create_pipeline();
    // HUD pipeline plus the unit quad's vertex/index buffers. Called once from
    // the constructor; throws on a GPU failure (a HUD is required, unlike the
    // optional text).
    void create_hud_pipeline();
    // (Re)uploads the stamina label when its rounded percentage changed. A
    // font/render failure prints one stderr line and leaves hud_text_texture_
    // null, so the bars keep drawing. Never throws.
    void update_hud_text();
    // Closes the font and quits SDL_ttf, idempotently (also on a half-built
    // constructor).
    void release_font();
    SDL_GPUShader* create_shader(SDL_GPUShaderStage stage, const char* entry_point,
                                 uint32_t num_uniform_buffers, uint32_t num_storage_buffers,
                                 uint32_t num_samplers, const uint8_t* source, size_t source_size);
    void release_gpu_resources();

    // (Re)creates color + depth targets when the drawable size changed.
    void ensure_targets(uint32_t width, uint32_t height);
    void release_targets();

    SDL_GPUDevice* device_;
    SDL_Window* window_;
    SDL_GPUTextureFormat color_format_;

    SDL_GPUShader* vertex_shader_ = nullptr;
    SDL_GPUShader* fragment_shader_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;

    SDL_GPUTexture* color_target_ = nullptr;
    SDL_GPUTexture* depth_target_ = nullptr;
    uint32_t target_width_ = 0;
    uint32_t target_height_ = 0;

    // 1x1 opaque white, used for materials without a base-color texture (a
    // draw always binds a texture, so no material can have "none").
    SDL_GPUTexture* white_texture_ = nullptr;

    // HUD: a second, depth-less pipeline drawing alpha-blended textured quads
    // over the scene pass. The quad geometry is one static unit quad; the
    // per-draw transform/UVs/color arrive as a push uniform.
    SDL_GPUShader* hud_vertex_shader_ = nullptr;
    SDL_GPUShader* hud_fragment_shader_ = nullptr;
    SDL_GPUGraphicsPipeline* hud_pipeline_ = nullptr;
    SDL_GPUSampler* hud_sampler_ = nullptr;
    SDL_GPUBuffer* hud_vertex_buffer_ = nullptr;
    SDL_GPUBuffer* hud_index_buffer_ = nullptr;
    HudState hud_{};

    // Font and label texture. The label is re-rendered only when its text
    // changes (i.e. when the rounded percentage changes). A failed font leaves
    // hud_font_ null and text drawing off; the bars keep drawing.
    TTF_Font* hud_font_ = nullptr;
    bool ttf_initialized_ = false;
    bool hud_text_unavailable_ = false;
    char hud_text_label_[32] = {};
    SDL_GPUTexture* hud_text_texture_ = nullptr;
    float hud_text_width_ = 0.0f;
    float hud_text_height_ = 0.0f;

    std::vector<MeshGpu> meshes_;
    std::vector<SDL_GPUTexture*> textures_;
    bool uploaded_ = false;

    // One palette buffer per rig (index = Scene::rigs index), each sized by
    // that rig's own joint count; a scene with no rigs keeps one identity
    // fallback entry. render_frame binds a mesh's own entry before its draw.
    std::vector<PoseGpu> pose_buffers_;
};
