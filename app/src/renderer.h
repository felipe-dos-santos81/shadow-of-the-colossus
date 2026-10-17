#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

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
    // this call (the app loads one model and keeps it).
    void upload(const Scene& scene);

    // Renders the offscreen target (clear + one draw per mesh, pushing the
    // view-projection once and the per-mesh model + material uniforms), then —
    // when acquire_swapchain — copies it to the swapchain in the same submit.
    // A frame whose swapchain cannot be acquired (too many frames in flight,
    // minimized window) is skipped, not an error.
    void render_frame(const Scene& scene, const Mat4& viewProj, bool acquire_swapchain);

    // The last rendered frame as RGBA8, row 0 = top. Only valid after a
    // render_frame; throws if nothing has been rendered yet.
    std::vector<uint8_t> read_pixels();

private:
    struct MeshGpu {
        SDL_GPUBuffer* vertex_buffer;
        SDL_GPUBuffer* index_buffer;
        uint32_t index_count;
    };

    void create_pipeline();
    SDL_GPUShader* create_shader(SDL_GPUShaderStage stage, const char* entry_point,
                                 uint32_t num_uniform_buffers, uint32_t num_samplers,
                                 const uint8_t* source, size_t source_size);
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

    std::vector<MeshGpu> meshes_;
    std::vector<SDL_GPUTexture*> textures_;
    bool uploaded_ = false;
};
