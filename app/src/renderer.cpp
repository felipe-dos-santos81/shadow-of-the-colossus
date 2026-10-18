#include "renderer.h"

#include <algorithm>
#include <cstddef>
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

// No per-mesh transforms exist yet (the converter emits world-space geometry),
// but vertex slot 1 is part of the shader contract, so every mesh draws with
// the shared identity from math3d.h.
const Mat4 kIdentity = mat4_identity();

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
    } catch (...) {
        // The half-built object's destructor will not run: release here.
        release_gpu_resources();
        throw;
    }
}

Renderer::~Renderer() {
    release_targets();
    release_gpu_resources();
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

    // The joint palette (vertex storage buffer): one matrix per skin joint, or
    // a single matrix for a scene without a skin — the shader always reads
    // palette[joints], and an unskinned vertex defaults to joint 0 weight 1.
    // The initial content is the bind pose (identity per joint), so any draw
    // that happens before the first upload_pose is well defined.
    pose_joint_count_ = static_cast<uint32_t>(scene.skin.joints.size());
    const std::vector<Mat4> rest_palette(std::max<uint32_t>(pose_joint_count_, 1u),
                                         mat4_identity());
    {
        const uint32_t pose_size = static_cast<uint32_t>(rest_palette.size() * sizeof(Mat4));
        SDL_GPUBufferCreateInfo buffer_info{};
        buffer_info.size = pose_size;
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        pose_buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
        if (pose_buffer_ == nullptr) {
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
        destination.buffer = pose_buffer_;
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

void Renderer::upload_pose(const std::vector<Mat4>& palette) {
    if (pose_joint_count_ == 0) {
        return;  // no skin: the identity palette stands
    }
    if (palette.size() != pose_joint_count_) {
        throw std::runtime_error("renderer: upload_pose got " + std::to_string(palette.size()) +
                                 " matrices, expected " + std::to_string(pose_joint_count_));
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
    const SDL_GPUBufferRegion destination{pose_buffer_, 0, byte_count};
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

    // Vertex storage buffer 0: the joint palette, shared by every draw.
    if (pose_buffer_ != nullptr) {
        SDL_GPUBuffer* pose_buffers[1] = {pose_buffer_};
        SDL_BindGPUVertexStorageBuffers(pass, 0, pose_buffers, 1);
    }

    for (size_t i = 0; i < scene.meshes.size() && i < meshes_.size(); ++i) {
        const MeshGpu& mesh = meshes_[i];
        if (mesh.index_buffer == nullptr || mesh.index_count == 0) {
            continue;
        }

        // Vertex slot 1: this mesh's model matrix (identity for now).
        SDL_PushGPUVertexUniformData(cmd, 1, &kIdentity, sizeof(Mat4));

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

        const SDL_GPUBufferBinding vertex_binding{mesh.vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        const SDL_GPUBufferBinding index_binding{mesh.index_buffer, 0};
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        const SDL_GPUTextureSamplerBinding texture_binding{albedo, sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &texture_binding, 1);

        SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(pass);

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
    if (pose_buffer_ != nullptr) {
        SDL_ReleaseGPUBuffer(device_, pose_buffer_);
        pose_buffer_ = nullptr;
    }
    pose_joint_count_ = 0;
    for (MeshGpu& mesh : meshes_) {
        if (mesh.vertex_buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device_, mesh.vertex_buffer);
        }
        if (mesh.index_buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device_, mesh.index_buffer);
        }
    }
    meshes_.clear();
    if (sampler_ != nullptr) {
        SDL_ReleaseGPUSampler(device_, sampler_);
        sampler_ = nullptr;
    }
    if (pipeline_ != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        pipeline_ = nullptr;
    }
    if (vertex_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, vertex_shader_);
        vertex_shader_ = nullptr;
    }
    if (fragment_shader_ != nullptr) {
        SDL_ReleaseGPUShader(device_, fragment_shader_);
        fragment_shader_ = nullptr;
    }
}
