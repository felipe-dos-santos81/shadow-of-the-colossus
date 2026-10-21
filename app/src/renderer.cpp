#include "renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// Per-mesh fragment uniform: std140 layout, so both fields are vec4 (the
// shader's `emissive.w` is an unused pad).
struct MaterialUniforms {
    float base_color[4];
    float emissive[4];
};
static_assert(sizeof(MaterialUniforms) == 32, "Material uniform size changed");

[[noreturn]] void throw_sdl_error(const std::string& what) {
    const char* error = SDL_GetError();
    if (error != nullptr && error[0] != '\0') {
        throw std::runtime_error(what + ": " + error);
    }
    throw std::runtime_error(what);
}

// Owns transfer buffers for the duration of one upload: released on scope
// exit, so an error between mapping and the fence wait cannot leak them.
struct TransferBufferGuard {
    SDL_GPUDevice* device;
    std::vector<SDL_GPUTransferBuffer*>* buffers;
    ~TransferBufferGuard() {
        for (SDL_GPUTransferBuffer* buffer : *buffers) {
            SDL_ReleaseGPUTransferBuffer(device, buffer);
        }
    }
};

// Reads the shader source that CMake copied next to the executable. SDL
// compiles it at runtime (plan ruling R2), so the file must be present beside
// the binary — a missing file is a hard startup failure, not a fallback.
std::vector<uint8_t> read_shader_source(const char* file_name) {
    // SDL owns (and caches) the base path; do not free it.
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        throw_sdl_error("renderer: SDL_GetBasePath");
    }
    const std::string path = std::string(base) + file_name;

    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        throw_sdl_error("renderer: cannot read " + path);
    }
    std::vector<uint8_t> source(static_cast<const uint8_t*>(data),
                                static_cast<const uint8_t*>(data) + size);
    SDL_free(data);
    return source;
}

// What a mesh without a material (materialIndex -1) draws with: opaque white
// factors and the renderer's 1x1 white fallback texture.
const float kDefaultBaseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
const float kDefaultEmissive[3] = {0.0f, 0.0f, 0.0f};

// HUD design-space reference frame (the one hud.cpp authors its rects on): the
// overlay is mapped to NDC by these constants, so it keeps its proportions on
// a resized window instead of pinning to one resolution.
constexpr float kHudDesignWidth = 1280.0f;
constexpr float kHudDesignHeight = 720.0f;

// The HUD label font. A missing font is not fatal: the bars still draw.
constexpr const char* kHudFontPath = "/System/Library/Fonts/Supplemental/Arial.ttf";
constexpr float kHudFontPointSize = 16.0f;
// Gap between the label's bottom edge and the stamina bar's top edge.
constexpr float kHudLabelGap = 4.0f;

// Overlay colors, in the display-space values the mesh shader writes (it
// encodes its output to sRGB itself, so no decode happens there either).
constexpr float kStaminaBarColor[4] = {0.30f, 0.85f, 0.35f, 0.85f};
constexpr float kHealthBarColor[4] = {0.85f, 0.20f, 0.16f, 0.90f};
// Wander's health: amber, above the stamina bar, so it reads as the player's
// own pool rather than Valus's red top-edge bar.
constexpr float kPlayerHealthBarColor[4] = {1.00f, 0.65f, 0.10f, 0.90f};
constexpr float kHudTextColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

// Unit quad vertex: position in [0,1]^2 with y down, plus matching UVs.
struct HudVertex {
    float x, y;
    float u, v;
};
static_assert(sizeof(HudVertex) == 16, "HUD vertex layout changed");

// Per-draw HUD uniform: two vec4s (std140). Mirrors `Draw` in hud.metal.
struct HudDrawUniforms {
    float xform[4];  // xy = NDC origin (left, top), zw = NDC size
    float color[4];  // RGBA tint multiplied into the sampled texel
};
static_assert(sizeof(HudDrawUniforms) == 32, "HUD uniform size changed");

// One textured, alpha-blended HUD quad. An empty rect (zero fraction) draws
// nothing; a null texture skips the draw rather than binding nothing.
void draw_hud_quad(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const HudRect& rect,
                   SDL_GPUTexture* texture, SDL_GPUSampler* sampler, const float color[4]) {
    if (!(rect.w > 0.0f) || !(rect.h > 0.0f) || texture == nullptr) {
        return;
    }

    HudDrawUniforms draw{};
    draw.xform[0] = rect.x / kHudDesignWidth * 2.0f - 1.0f;
    draw.xform[1] = 1.0f - rect.y / kHudDesignHeight * 2.0f;
    draw.xform[2] = rect.w / kHudDesignWidth * 2.0f;
    draw.xform[3] = rect.h / kHudDesignHeight * 2.0f;
    std::memcpy(draw.color, color, sizeof(draw.color));

    // Both stages read the same struct: the vertex stage places the quad, the
    // fragment stage tints it.
    SDL_PushGPUVertexUniformData(cmd, 0, &draw, sizeof(draw));
    SDL_PushGPUFragmentUniformData(cmd, 0, &draw, sizeof(draw));

    const SDL_GPUTextureSamplerBinding binding{texture, sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
}

}  // namespace

Renderer::Renderer(SDL_GPUDevice* device, SDL_Window* window)
    : device_(device), window_(window) {
    // The offscreen target shares the swapchain's format so the copy is
    // format-exact (and `read_pixels` sees exactly what the window shows).
    color_format_ = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    if (color_format_ == SDL_GPU_TEXTUREFORMAT_INVALID) {
        throw_sdl_error("renderer: SDL_GetGPUSwapchainTextureFormat");
    }

    try {
        create_pipeline();
        create_hud_pipeline();

        SDL_GPUSamplerCreateInfo sampler_info{};
        sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.max_anisotropy = 1.0f;
        sampler_info.min_lod = 0.0f;
        sampler_info.max_lod = 0.0f;  // every texture has a single mip level
        sampler_ = SDL_CreateGPUSampler(device_, &sampler_info);
        if (sampler_ == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUSampler");
        }

        // The HUD quad is always axis-aligned and UV-exact, so clamp instead of
        // repeating: at u/v = 1.0 the sampler must not wrap into the opposite
        // edge's texels.
        SDL_GPUSamplerCreateInfo hud_sampler_info = sampler_info;
        hud_sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        hud_sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        hud_sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        hud_sampler_ = SDL_CreateGPUSampler(device_, &hud_sampler_info);
        if (hud_sampler_ == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUSampler(hud)");
        }

        // The label is optional: a missing SDL_ttf or font prints one line and
        // the HUD degrades to bars only, never failing the run.
        if (!TTF_Init()) {
            std::fprintf(stderr,
                         "renderer: SDL_ttf init failed (%s); drawing the HUD bars only\n",
                         SDL_GetError());
        } else {
            ttf_initialized_ = true;
            hud_font_ = TTF_OpenFont(kHudFontPath, kHudFontPointSize);
            if (hud_font_ == nullptr) {
                std::fprintf(stderr,
                             "renderer: cannot open HUD font %s (%s); drawing the HUD bars only\n",
                             kHudFontPath, SDL_GetError());
            }
        }
    } catch (...) {
        // The half-built object's destructor will not run: release here.
        release_gpu_resources();
        release_font();
        throw;
    }
}

Renderer::~Renderer() {
    release_targets();
    release_gpu_resources();
    release_font();
}

void Renderer::create_pipeline() {
    const std::vector<uint8_t> source = read_shader_source("mesh.metal");

    // Vertex stage: uniforms 0 (viewProj) and 1 (model) plus the palette
    // storage buffer. Fragment stage: slot 0 (material) plus the base-color
    // texture and its sampler.
    vertex_shader_ = create_shader(SDL_GPU_SHADERSTAGE_VERTEX, "mesh_vertex", 2, 1, 0,
                                   source.data(), source.size());
    fragment_shader_ = create_shader(SDL_GPU_SHADERSTAGE_FRAGMENT, "mesh_fragment", 1, 0, 1,
                                     source.data(), source.size());

    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(Vertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[5]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, px)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, nx)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, u)};
    attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, offsetof(Vertex, j)};
    attributes[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, w)};

    SDL_GPUVertexInputState vertex_input{};
    vertex_input.vertex_buffer_descriptions = &vertex_buffer;
    vertex_input.num_vertex_buffers = 1;
    vertex_input.vertex_attributes = attributes;
    vertex_input.num_vertex_attributes = 5;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = color_format_;

    SDL_GPUGraphicsPipelineTargetInfo target_info{};
    target_info.color_target_descriptions = &color_target;
    target_info.num_color_targets = 1;
    target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    target_info.has_depth_stencil_target = true;

    SDL_GPUDepthStencilState depth_stencil{};
    depth_stencil.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    depth_stencil.enable_depth_test = true;
    depth_stencil.enable_depth_write = true;

    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
    // No culling yet: the converted meshes have not been winding-checked, and a
    // wrong winding would silently erase the geometry.
    rasterizer.cull_mode = SDL_GPU_CULLMODE_NONE;
    rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizer.enable_depth_clip = true;

    SDL_GPUMultisampleState multisample{};
    multisample.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.vertex_shader = vertex_shader_;
    pipeline_info.fragment_shader = fragment_shader_;
    pipeline_info.vertex_input_state = vertex_input;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state = rasterizer;
    pipeline_info.multisample_state = multisample;
    pipeline_info.depth_stencil_state = depth_stencil;
    pipeline_info.target_info = target_info;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
    if (pipeline_ == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUGraphicsPipeline");
    }
}

void Renderer::create_hud_pipeline() {
    const std::vector<uint8_t> source = read_shader_source("hud.metal");

    // Vertex stage: one uniform (the per-draw transform). Fragment stage: the
    // same uniform (tint) plus the quad's texture and sampler.
    hud_vertex_shader_ = create_shader(SDL_GPU_SHADERSTAGE_VERTEX, "hud_vertex", 1, 0, 0,
                                       source.data(), source.size());
    hud_fragment_shader_ = create_shader(SDL_GPU_SHADERSTAGE_FRAGMENT, "hud_fragment", 1, 0, 1,
                                         source.data(), source.size());

    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(HudVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[2]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(HudVertex, x)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(HudVertex, u)};

    SDL_GPUVertexInputState vertex_input{};
    vertex_input.vertex_buffer_descriptions = &vertex_buffer;
    vertex_input.num_vertex_buffers = 1;
    vertex_input.vertex_attributes = attributes;
    vertex_input.num_vertex_attributes = 2;

    // Straight alpha blending: the bars and the anti-aliased label glyphs
    // composite over the scene.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = color_format_;
    color_target.blend_state = blend;

    // No depth: the HUD is a later pass over the already-rendered color target,
    // so the pipeline and that pass agree on having no depth attachment.
    SDL_GPUGraphicsPipelineTargetInfo target_info{};
    target_info.color_target_descriptions = &color_target;
    target_info.num_color_targets = 1;
    target_info.has_depth_stencil_target = false;

    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
    rasterizer.cull_mode = SDL_GPU_CULLMODE_NONE;
    rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizer.enable_depth_clip = false;

    SDL_GPUMultisampleState multisample{};
    multisample.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.vertex_shader = hud_vertex_shader_;
    pipeline_info.fragment_shader = hud_fragment_shader_;
    pipeline_info.vertex_input_state = vertex_input;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state = rasterizer;
    pipeline_info.multisample_state = multisample;
    pipeline_info.target_info = target_info;

    hud_pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
    if (hud_pipeline_ == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUGraphicsPipeline(hud)");
    }

    // The one quad every HUD draw uses: unit square with y down, UV-mapped
    // 1:1. Its transform and tint come from the push uniform.
    const HudVertex quad[4] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

    std::vector<SDL_GPUTransferBuffer*> transfer_buffers;
    TransferBufferGuard transfer_guard{device_, &transfer_buffers};

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = sizeof(quad) + sizeof(indices);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (transfer == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(hud quad)");
    }
    transfer_buffers.push_back(transfer);

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (mapped == nullptr) {
        throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(hud quad)");
    }
    std::memcpy(mapped, quad, sizeof(quad));
    std::memcpy(static_cast<uint8_t*>(mapped) + sizeof(quad), indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUBufferCreateInfo buffer_info{};
    buffer_info.size = sizeof(quad);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    hud_vertex_buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
    if (hud_vertex_buffer_ == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUBuffer(hud vertices)");
    }
    buffer_info.size = sizeof(indices);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    hud_index_buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
    if (hud_index_buffer_ == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUBuffer(hud indices)");
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        throw_sdl_error("renderer: SDL_AcquireGPUCommandBuffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation source_location{};
    source_location.transfer_buffer = transfer;
    source_location.offset = 0;
    SDL_GPUBufferRegion vertex_region{};
    vertex_region.buffer = hud_vertex_buffer_;
    vertex_region.size = sizeof(quad);
    SDL_UploadToGPUBuffer(copy_pass, &source_location, &vertex_region, false);

    source_location.offset = sizeof(quad);
    SDL_GPUBufferRegion index_region{};
    index_region.buffer = hud_index_buffer_;
    index_region.size = sizeof(indices);
    SDL_UploadToGPUBuffer(copy_pass, &source_location, &index_region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        throw_sdl_error("renderer: SDL_SubmitGPUCommandBufferAndAcquireFence");
    }
    const bool waited = SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
    if (!waited) {
        throw_sdl_error("renderer: SDL_WaitForGPUFences");
    }
}

SDL_GPUShader* Renderer::create_shader(SDL_GPUShaderStage stage, const char* entry_point,
                                       uint32_t num_uniform_buffers, uint32_t num_storage_buffers,
                                       uint32_t num_samplers, const uint8_t* source,
                                       size_t source_size) {
    SDL_GPUShaderCreateInfo info{};
    info.code_size = source_size;
    info.code = source;
    info.entrypoint = entry_point;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.stage = stage;
    info.num_samplers = num_samplers;
    info.num_storage_buffers = num_storage_buffers;
    info.num_uniform_buffers = num_uniform_buffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &info);
    if (shader == nullptr) {
        // SDL's Metal backend logs the compiler error through SDL_LogError and
        // does not always set SDL's error string, so name both places.
        const char* error = SDL_GetError();
        std::string message = std::string("renderer: SDL_CreateGPUShader(") + entry_point + ")";
        if (error != nullptr && error[0] != '\0') {
            message += ": ";
            message += error;
        } else {
            message += " failed; the Metal compiler error is the SDL log line above";
        }
        throw std::runtime_error(message);
    }
    return shader;
}

void Renderer::upload(const Scene& scene) {
    // Guards on the scene layout: render_frame indexes the GPU handles with the
    // scene's mesh indices, so a mismatch would read the wrong draw.
    if (uploaded_) {
        throw std::runtime_error("renderer: upload called twice");
    }

    std::vector<SDL_GPUTransferBuffer*> transfer_buffers;
    TransferBufferGuard transfer_guard{device_, &transfer_buffers};

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        throw_sdl_error("renderer: SDL_AcquireGPUCommandBuffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

    // One RGBA8 texture per decoded PNG.
    textures_.resize(scene.textures.size(), nullptr);
    for (size_t i = 0; i < scene.textures.size(); ++i) {
        const TextureCpu& texture = scene.textures[i];
        if (texture.width <= 0 || texture.height <= 0 ||
            texture.rgba8.size() != static_cast<size_t>(texture.width) * texture.height * 4) {
            throw std::runtime_error("renderer: texture " + texture.sourceRelPath + " has bad dimensions");
        }

        SDL_GPUTextureCreateInfo texture_info{};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = static_cast<uint32_t>(texture.width);
        texture_info.height = static_cast<uint32_t>(texture.height);
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        textures_[i] = SDL_CreateGPUTexture(device_, &texture_info);
        if (textures_[i] == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUTexture(" + texture.sourceRelPath + ")");
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<uint32_t>(texture.rgba8.size());
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(texture)");
        }
        transfer_buffers.push_back(transfer);

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(texture)");
        }
        std::memcpy(mapped, texture.rgba8.data(), texture.rgba8.size());
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<uint32_t>(texture.width);
        source.rows_per_layer = static_cast<uint32_t>(texture.height);
        SDL_GPUTextureRegion destination{};
        destination.texture = textures_[i];
        destination.w = static_cast<uint32_t>(texture.width);
        destination.h = static_cast<uint32_t>(texture.height);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }

    // 1x1 white fallback for materials without a base-color texture.
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        SDL_GPUTextureCreateInfo texture_info{};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = 1;
        texture_info.height = 1;
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        white_texture_ = SDL_CreateGPUTexture(device_, &texture_info);
        if (white_texture_ == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUTexture(fallback)");
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = sizeof(white);
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(fallback)");
        }
        transfer_buffers.push_back(transfer);

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(fallback)");
        }
        std::memcpy(mapped, white, sizeof(white));
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = 1;
        source.rows_per_layer = 1;
        SDL_GPUTextureRegion destination{};
        destination.texture = white_texture_;
        destination.w = 1;
        destination.h = 1;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }

    // Geometry: vertices for every mesh, then indices for every mesh, in one
    // transfer buffer (vertex records are 56 bytes and indices 4, so every
    // region stays 4-byte aligned).
    uint32_t vertex_bytes = 0;
    uint32_t index_bytes = 0;
    for (const MeshCpu& mesh : scene.meshes) {
        vertex_bytes += static_cast<uint32_t>(mesh.vertices.size() * sizeof(Vertex));
        index_bytes += static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));
    }

    meshes_.resize(scene.meshes.size());
    if (vertex_bytes > 0 && index_bytes > 0) {
        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = vertex_bytes + index_bytes;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(geometry)");
        }
        transfer_buffers.push_back(transfer);

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(geometry)");
        }
        uint8_t* base = static_cast<uint8_t*>(mapped);
        uint32_t offset = 0;
        for (const MeshCpu& mesh : scene.meshes) {
            const size_t bytes = mesh.vertices.size() * sizeof(Vertex);
            if (bytes > 0) {
                std::memcpy(base + offset, mesh.vertices.data(), bytes);
                offset += static_cast<uint32_t>(bytes);
            }
        }
        offset = vertex_bytes;
        for (const MeshCpu& mesh : scene.meshes) {
            const size_t bytes = mesh.indices.size() * sizeof(uint32_t);
            if (bytes > 0) {
                std::memcpy(base + offset, mesh.indices.data(), bytes);
                offset += static_cast<uint32_t>(bytes);
            }
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        uint32_t vertex_offset = 0;
        uint32_t index_offset = vertex_bytes;
        for (size_t i = 0; i < scene.meshes.size(); ++i) {
            const MeshCpu& mesh = scene.meshes[i];
            MeshGpu& gpu = meshes_[i];
            gpu.vertex_buffer = nullptr;
            gpu.index_buffer = nullptr;
            gpu.index_count = static_cast<uint32_t>(mesh.indices.size());
            if (gpu.index_count == 0 || mesh.vertices.empty()) {
                continue;
            }

            const uint32_t vertex_size = static_cast<uint32_t>(mesh.vertices.size() * sizeof(Vertex));
            const uint32_t index_size = static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));

            SDL_GPUBufferCreateInfo buffer_info{};
            buffer_info.size = vertex_size;
            buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            gpu.vertex_buffer = SDL_CreateGPUBuffer(device_, &buffer_info);
            if (gpu.vertex_buffer == nullptr) {
                throw_sdl_error("renderer: SDL_CreateGPUBuffer(vertices)");
            }

            buffer_info.size = index_size;
            buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            gpu.index_buffer = SDL_CreateGPUBuffer(device_, &buffer_info);
            if (gpu.index_buffer == nullptr) {
                throw_sdl_error("renderer: SDL_CreateGPUBuffer(indices)");
            }

            SDL_GPUTransferBufferLocation source{};
            source.transfer_buffer = transfer;
            SDL_GPUBufferRegion destination{};
            destination.offset = 0;

            source.offset = vertex_offset;
            destination.buffer = gpu.vertex_buffer;
            destination.size = vertex_size;
            SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
            vertex_offset += vertex_size;

            source.offset = index_offset;
            destination.buffer = gpu.index_buffer;
            destination.size = index_size;
            SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
            index_offset += index_size;
        }
    }

    // One joint palette per rig (vertex storage buffer), each sized by that
    // rig's own joint count — a rig can only ever read its own palette, so the
    // entity's 238 joint indices can never reach a smaller player buffer. A rig
    // with no skin (and a scene with no rigs at all) gets a single identity
    // matrix: an unskinned vertex has zero weights, so the shader never reads
    // the palette for it, and any draw before the first upload_pose is well
    // defined (the initial content is the bind pose, identity per joint).
    pose_buffers_.resize(std::max<std::size_t>(scene.rigs.size(), 1));
    for (std::size_t rig_index = 0; rig_index < pose_buffers_.size(); ++rig_index) {
        PoseGpu& pose = pose_buffers_[rig_index];
        pose.joint_count = static_cast<uint32_t>(rig_at(scene, rig_index).skin.joints.size());
        const std::vector<Mat4> rest_palette(std::max<std::size_t>(pose.joint_count, 1),
                                             mat4_identity());
        const uint32_t pose_size = static_cast<uint32_t>(rest_palette.size() * sizeof(Mat4));

        SDL_GPUBufferCreateInfo buffer_info{};
        buffer_info.size = pose_size;
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        pose.buffer = SDL_CreateGPUBuffer(device_, &buffer_info);
        if (pose.buffer == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUBuffer(pose)");
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = pose_size;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(pose)");
        }
        transfer_buffers.push_back(transfer);

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(pose)");
        }
        std::memcpy(mapped, rest_palette.data(), pose_size);
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transfer;
        SDL_GPUBufferRegion destination{};
        destination.buffer = pose.buffer;
        destination.size = pose_size;
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    }

    SDL_EndGPUCopyPass(copy_pass);

    // The uploads are on the GPU timeline; wait for them so the transfer
    // buffers can be released and the first frame can draw immediately.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        throw_sdl_error("renderer: SDL_SubmitGPUCommandBufferAndAcquireFence");
    }
    const bool waited = SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
    if (!waited) {
        throw_sdl_error("renderer: SDL_WaitForGPUFences");
    }

    uploaded_ = true;
}

void Renderer::upload_pose(std::size_t rigIndex, const std::vector<Mat4>& palette) {
    if (rigIndex >= pose_buffers_.size()) {
        throw std::runtime_error("renderer: upload_pose: no rig " + std::to_string(rigIndex) +
                                 " in this scene");
    }
    const PoseGpu& pose = pose_buffers_[rigIndex];
    if (pose.joint_count == 0) {
        return;  // no skin: the identity palette stands
    }
    if (palette.size() != pose.joint_count) {
        throw std::runtime_error("renderer: upload_pose(rig " + std::to_string(rigIndex) +
                                 ") got " + std::to_string(palette.size()) +
                                 " matrices, expected " + std::to_string(pose.joint_count));
    }

    const uint32_t byte_count = static_cast<uint32_t>(palette.size() * sizeof(Mat4));
    std::vector<SDL_GPUTransferBuffer*> transfer_buffers;
    TransferBufferGuard transfer_guard{device_, &transfer_buffers};

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = byte_count;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (transfer == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(pose)");
    }
    transfer_buffers.push_back(transfer);

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (mapped == nullptr) {
        throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(pose)");
    }
    std::memcpy(mapped, palette.data(), byte_count);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    // The copy pass is its own submission, ordered before the frame's render
    // pass on the same queue, so the draws read the new palette.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        throw_sdl_error("renderer: SDL_AcquireGPUCommandBuffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTransferBufferLocation source{transfer, 0};
    const SDL_GPUBufferRegion destination{pose.buffer, 0, byte_count};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    // The transfer buffer may only be released once the copy has run (upload()'s
    // pattern); the fence wait costs microseconds at this size. A ring of
    // transfer buffers would avoid the wait if profiling ever asks for it.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        throw_sdl_error("renderer: SDL_SubmitGPUCommandBufferAndAcquireFence");
    }
    const bool waited = SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
    if (!waited) {
        throw_sdl_error("renderer: SDL_WaitForGPUFences");
    }
}

void Renderer::render_frame(const Scene& scene, const Mat4& viewProj, bool acquire_swapchain) {
    // Refresh the label before the frame's command buffer: the upload is its
    // own submission and only happens when the rounded percentage changed.
    // Uploading is not drawing — the overlay draws only after the swapchain
    // has been acquired below.
    update_hud_text();

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        throw_sdl_error("renderer: SDL_AcquireGPUCommandBuffer");
    }

    // Acquire first: the swapchain's size decides the offscreen target size, so
    // a resize between frames can never produce a size-mismatched copy.
    SDL_GPUTexture* swapchain = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    if (acquire_swapchain) {
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchain, &width, &height)) {
            SDL_CancelGPUCommandBuffer(cmd);
            throw_sdl_error("renderer: SDL_WaitAndAcquireGPUSwapchainTexture");
        }
        if (swapchain == nullptr) {
            // Too many frames in flight: skip this frame, not an error.
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }
    } else {
        int pixel_width = 0;
        int pixel_height = 0;
        if (!SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height)) {
            SDL_CancelGPUCommandBuffer(cmd);
            throw_sdl_error("renderer: SDL_GetWindowSizeInPixels");
        }
        if (pixel_width <= 0 || pixel_height <= 0) {
            SDL_CancelGPUCommandBuffer(cmd);
            return;  // minimized
        }
        width = static_cast<uint32_t>(pixel_width);
        height = static_cast<uint32_t>(pixel_height);
    }

    ensure_targets(width, height);

    SDL_GPUColorTargetInfo color_target{};
    color_target.texture = color_target_;
    color_target.clear_color = SDL_FColor{0.1f, 0.1f, 0.1f, 1.0f};
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_target{};
    depth_target.texture = depth_target_;
    depth_target.clear_depth = 1.0f;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    // Vertex slot 0: one view-projection for the whole frame.
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(Mat4));

    for (size_t i = 0; i < scene.meshes.size() && i < meshes_.size(); ++i) {
        const MeshGpu& mesh = meshes_[i];
        if (mesh.index_buffer == nullptr || mesh.index_count == 0) {
            continue;
        }

        // Vertex slot 1: this mesh's model matrix — identity for a single-glb
        // load, the instance's baked TRS in a composed scene.
        SDL_PushGPUVertexUniformData(cmd, 1, &scene.meshes[i].model, sizeof(Mat4));

        // Fragment slot 0: this mesh's material factors.
        MaterialUniforms material{};
        const int material_index = scene.meshes[i].materialIndex;
        const bool has_material = material_index >= 0 &&
                                  static_cast<size_t>(material_index) < scene.materials.size();
        const float* base_color = has_material
                                      ? scene.materials[static_cast<size_t>(material_index)].baseColorFactor
                                      : kDefaultBaseColor;
        const float* emissive = has_material
                                    ? scene.materials[static_cast<size_t>(material_index)].emissiveFactor
                                    : kDefaultEmissive;
        std::memcpy(material.base_color, base_color, sizeof(material.base_color));
        std::memcpy(material.emissive, emissive, 3 * sizeof(float));
        SDL_PushGPUFragmentUniformData(cmd, 0, &material, sizeof(material));

        // Base-color texture; the white fallback keeps the binding total.
        SDL_GPUTexture* albedo = white_texture_;
        const int texture_index =
            has_material ? scene.materials[static_cast<size_t>(material_index)].baseColorTexture : -1;
        if (texture_index >= 0 && static_cast<size_t>(texture_index) < textures_.size() &&
            textures_[static_cast<size_t>(texture_index)] != nullptr) {
            albedo = textures_[static_cast<size_t>(texture_index)];
        }

        // Vertex storage buffer 0: this mesh's own rig palette — never another
        // rig's. A static mesh (rig < 0) binds slot 0, whose zero-weight
        // vertices the shader passes through without reading it.
        const std::size_t slot = mesh_palette_slot(scene, scene.meshes[i]);
        if (slot < pose_buffers_.size() && pose_buffers_[slot].buffer != nullptr) {
            SDL_GPUBuffer* palette_buffers[1] = {pose_buffers_[slot].buffer};
            SDL_BindGPUVertexStorageBuffers(pass, 0, palette_buffers, 1);
        }

        const SDL_GPUBufferBinding vertex_binding{mesh.vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        const SDL_GPUBufferBinding index_binding{mesh.index_buffer, 0};
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        const SDL_GPUTextureSamplerBinding texture_binding{albedo, sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &texture_binding, 1);

        SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(pass);

    // HUD overlay: a second, depth-less pass that loads the scene color and
    // composites the bars, then the stamina label. It runs only after the
    // swapchain acquisition above succeeded (the early returns skip the frame
    // before this point), and before the swapchain copy below.
    if (hud_.visible) {
        SDL_GPUColorTargetInfo hud_target{};
        hud_target.texture = color_target_;
        hud_target.load_op = SDL_GPU_LOADOP_LOAD;
        hud_target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* hud_pass = SDL_BeginGPURenderPass(cmd, &hud_target, 1, nullptr);
        if (hud_pass == nullptr) {
            SDL_CancelGPUCommandBuffer(cmd);
            throw_sdl_error("renderer: SDL_BeginGPURenderPass(hud)");
        }

        SDL_BindGPUGraphicsPipeline(hud_pass, hud_pipeline_);
        const SDL_GPUBufferBinding hud_vertex_binding{hud_vertex_buffer_, 0};
        SDL_BindGPUVertexBuffers(hud_pass, 0, &hud_vertex_binding, 1);
        const SDL_GPUBufferBinding hud_index_binding{hud_index_buffer_, 0};
        SDL_BindGPUIndexBuffer(hud_pass, &hud_index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Bars first, label last: the label sits above the stamina bar and must
        // stay readable over it.
        draw_hud_quad(cmd, hud_pass, hud_stamina_rect(hud_.stamina), white_texture_, hud_sampler_,
                      kStaminaBarColor);
        draw_hud_quad(cmd, hud_pass, hud_health_rect(hud_.health), white_texture_, hud_sampler_,
                      kHealthBarColor);
        // The player's own pool, amber, above the stamina bar. A zero fraction
        // (a state no run has fed yet) draws nothing.
        draw_hud_quad(cmd, hud_pass, hud_player_health_rect(hud_.playerHealth), white_texture_,
                      hud_sampler_, kPlayerHealthBarColor);

        if (hud_text_texture_ != nullptr && hud_text_width_ > 0.0f) {
            const HudRect stamina = hud_stamina_rect(hud_.stamina);
            const HudRect label{stamina.x, stamina.y - hud_text_height_ - kHudLabelGap,
                                hud_text_width_, hud_text_height_};
            draw_hud_quad(cmd, hud_pass, label, hud_text_texture_, hud_sampler_, kHudTextColor);
        }

        SDL_EndGPURenderPass(hud_pass);
    }

    if (swapchain != nullptr) {
        // Same format on both sides: a texture copy, not a blit.
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTextureLocation source{color_target_, 0, 0, 0, 0, 0};
        const SDL_GPUTextureLocation destination{swapchain, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy_pass, &source, &destination, width, height, 1, false);
        SDL_EndGPUCopyPass(copy_pass);
    }

    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        throw_sdl_error("renderer: SDL_SubmitGPUCommandBuffer");
    }
}

std::vector<uint8_t> Renderer::read_pixels() {
    if (color_target_ == nullptr || target_width_ == 0 || target_height_ == 0) {
        throw std::runtime_error("renderer: read_pixels called before the first frame");
    }

    const uint32_t width = target_width_;
    const uint32_t height = target_height_;
    const size_t byte_count = static_cast<size_t>(width) * height * 4;

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = static_cast<uint32_t>(byte_count);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (transfer == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUTransferBuffer(download)");
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        throw_sdl_error("renderer: SDL_AcquireGPUCommandBuffer");
    }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion source{};
    source.texture = color_target_;
    source.w = width;
    source.h = height;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = transfer;
    SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        throw_sdl_error("renderer: SDL_SubmitGPUCommandBufferAndAcquireFence");
    }
    const bool waited = SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
    if (!waited) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        throw_sdl_error("renderer: SDL_WaitForGPUFences");
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        throw_sdl_error("renderer: SDL_MapGPUTransferBuffer(download)");
    }
    std::vector<uint8_t> pixels(byte_count);
    std::memcpy(pixels.data(), mapped, byte_count);
    SDL_UnmapGPUTransferBuffer(device_, transfer);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);

    // The target carries the swapchain's format; SDL's SDR macOS swapchain is
    // BGRA in memory, and the returned data is RGBA8.
    if (color_format_ == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
        color_format_ == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB) {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
    }
    return pixels;
}

void Renderer::set_hud(const HudState& state) {
    hud_ = state;
}

void Renderer::update_hud_text() {
    if (!hud_.visible || hud_font_ == nullptr || hud_text_unavailable_) {
        return;
    }

    // The label differs exactly when the rounded percentage differs, so a
    // string compare is the "recreate only when the integer changes" gate.
    char label[32];
    hud_format_stamina(label, hud_.stamina);
    if (hud_text_texture_ != nullptr && std::strcmp(label, hud_text_label_) == 0) {
        return;
    }

    // SDL_ttf's blended surfaces are ARGB8888; the GPU wants a byte-ordered
    // RGBA8 upload.
    SDL_Surface* rendered =
        TTF_RenderText_Blended(hud_font_, label, 0, SDL_Color{255, 255, 255, 255});
    SDL_Surface* surface =
        rendered != nullptr ? SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (rendered != nullptr) {
        SDL_DestroySurface(rendered);
    }
    if (surface == nullptr || surface->w <= 0 || surface->h <= 0 || surface->pitch < 0 ||
        surface->pitch < surface->w * 4) {
        if (surface != nullptr) {
            SDL_DestroySurface(surface);
        }
        std::fprintf(stderr, "renderer: HUD label render failed (%s); drawing the HUD bars only\n",
                     SDL_GetError());
        hud_text_unavailable_ = true;
        return;
    }

    const uint32_t width = static_cast<uint32_t>(surface->w);
    const uint32_t height = static_cast<uint32_t>(surface->h);
    const uint32_t row_bytes = width * 4;

    SDL_GPUTexture* texture = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUFence* fence = nullptr;
    bool uploaded = false;

    // One exit path for every GPU step: each stage only has to break, and the
    // cleanup below releases whatever the earlier stages created.
    do {
        SDL_GPUTextureCreateInfo texture_info{};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_info.width = width;
        texture_info.height = height;
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        texture = SDL_CreateGPUTexture(device_, &texture_info);
        if (texture == nullptr) {
            break;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = row_bytes * height;
        transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            break;
        }

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr) {
            break;
        }
        const uint8_t* source_pixels = static_cast<const uint8_t*>(surface->pixels);
        if (static_cast<uint32_t>(surface->pitch) == row_bytes) {
            std::memcpy(mapped, source_pixels, row_bytes * height);
        } else {
            for (uint32_t row = 0; row < height; ++row) {
                std::memcpy(
                    static_cast<uint8_t*>(mapped) + static_cast<std::size_t>(row) * row_bytes,
                    source_pixels + static_cast<std::size_t>(row) * surface->pitch, row_bytes);
            }
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr) {
            break;
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = width;
        source.rows_per_layer = height;
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = width;
        destination.h = height;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
        SDL_EndGPUCopyPass(copy_pass);

        // The upload is its own submission, like upload_pose: a fence wait
        // before the transfer buffer can go, and the frame's own command
        // buffer then sees a settled texture. Only a label change pays this.
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr) {
            break;
        }
        const bool waited = SDL_WaitForGPUFences(device_, true, &fence, 1);
        SDL_ReleaseGPUFence(device_, fence);
        fence = nullptr;
        uploaded = waited;
    } while (false);

    if (fence != nullptr) {
        SDL_ReleaseGPUFence(device_, fence);
    }
    if (transfer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
    }
    SDL_DestroySurface(surface);
    if (!uploaded) {
        if (texture != nullptr) {
            SDL_ReleaseGPUTexture(device_, texture);
        }
        std::fprintf(stderr, "renderer: HUD label upload failed (%s); drawing the HUD bars only\n",
                     SDL_GetError());
        hud_text_unavailable_ = true;
        return;
    }

    if (hud_text_texture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, hud_text_texture_);
    }
    hud_text_texture_ = texture;
    hud_text_width_ = static_cast<float>(width);
    hud_text_height_ = static_cast<float>(height);
    std::snprintf(hud_text_label_, sizeof(hud_text_label_), "%s", label);
}

void Renderer::release_font() {
    if (hud_font_ != nullptr) {
        TTF_CloseFont(hud_font_);
        hud_font_ = nullptr;
    }
    if (ttf_initialized_) {
        TTF_Quit();
        ttf_initialized_ = false;
    }
}

void Renderer::ensure_targets(uint32_t width, uint32_t height) {
    if (color_target_ != nullptr && width == target_width_ && height == target_height_) {
        return;
    }
    release_targets();

    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = color_format_;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    color_target_ = SDL_CreateGPUTexture(device_, &texture_info);
    if (color_target_ == nullptr) {
        throw_sdl_error("renderer: SDL_CreateGPUTexture(color)");
    }

    texture_info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_target_ = SDL_CreateGPUTexture(device_, &texture_info);
    if (depth_target_ == nullptr) {
        release_targets();
        throw_sdl_error("renderer: SDL_CreateGPUTexture(depth)");
    }

    target_width_ = width;
    target_height_ = height;
}

void Renderer::release_targets() {
    if (color_target_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, color_target_);
        color_target_ = nullptr;
    }
    if (depth_target_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, depth_target_);
        depth_target_ = nullptr;
    }
    target_width_ = 0;
    target_height_ = 0;
}

void Renderer::release_gpu_resources() {
    for (SDL_GPUTexture* texture : textures_) {
        if (texture != nullptr) {
            SDL_ReleaseGPUTexture(device_, texture);
        }
    }
    textures_.clear();
    if (white_texture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, white_texture_);
        white_texture_ = nullptr;
    }
    for (PoseGpu& pose : pose_buffers_) {
        if (pose.buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device_, pose.buffer);
            pose.buffer = nullptr;
        }
    }
    pose_buffers_.clear();
    for (MeshGpu& mesh : meshes_) {
        if (mesh.vertex_buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device_, mesh.vertex_buffer);
        }
        if (mesh.index_buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device_, mesh.index_buffer);
        }
    }
    meshes_.clear();
    if (hud_text_texture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, hud_text_texture_);
        hud_text_texture_ = nullptr;
    }
    hud_text_width_ = 0.0f;
    hud_text_height_ = 0.0f;
    hud_text_label_[0] = '\0';
    if (hud_vertex_buffer_ != nullptr) {
        SDL_ReleaseGPUBuffer(device_, hud_vertex_buffer_);
        hud_vertex_buffer_ = nullptr;
    }
    if (hud_index_buffer_ != nullptr) {
        SDL_ReleaseGPUBuffer(device_, hud_index_buffer_);
        hud_index_buffer_ = nullptr;
    }
    if (sampler_ != nullptr) {
        SDL_ReleaseGPUSampler(device_, sampler_);
        sampler_ = nullptr;
    }
    if (hud_sampler_ != nullptr) {
        SDL_ReleaseGPUSampler(device_, hud_sampler_);
        hud_sampler_ = nullptr;
    }
    if (pipeline_ != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        pipeline_ = nullptr;
    }
    if (hud_pipeline_ != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device_, hud_pipeline_);
        hud_pipeline_ = nullptr;
    }
    if (vertex_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, vertex_shader_);
        vertex_shader_ = nullptr;
    }
    if (fragment_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, fragment_shader_);
        fragment_shader_ = nullptr;
    }
    if (hud_vertex_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, hud_vertex_shader_);
        hud_vertex_shader_ = nullptr;
    }
    if (hud_fragment_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, hud_fragment_shader_);
        hud_fragment_shader_ = nullptr;
    }
}
