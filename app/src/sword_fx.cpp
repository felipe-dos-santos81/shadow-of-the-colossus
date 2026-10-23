#include "sword_fx.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "stb_image.h"

namespace {

// The lines' additive tint (display space, like the tendril pass): white light.
// source: authored
constexpr float kTint[3] = {1.0f, 1.0f, 1.0f};

Vec3 add(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(Vec3 v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }

void push(std::vector<EffectVertex>& out, Vec3 p, float u, float v, float alpha) {
    out.push_back(EffectVertex{p.x, p.y, p.z, u, v, kTint[0], kTint[1], kTint[2], alpha});
}

// A camera-facing square sprite (bottom-left, bottom-right, top-right,
// top-left — the index buffer's (0,1,2), (0,2,3)) over the whole texture.
void push_sprite(std::vector<EffectVertex>& out, Vec3 center, Vec3 right, Vec3 up, float size,
                 float alpha) {
    const Vec3 r = scale(right, 0.5f * size);
    const Vec3 u = scale(up, 0.5f * size);
    push(out, add(add(center, scale(r, -1.0f)), scale(u, -1.0f)), 0.0f, 1.0f, alpha);
    push(out, add(add(center, r), scale(u, -1.0f)), 1.0f, 1.0f, alpha);
    push(out, add(add(center, r), u), 1.0f, 0.0f, alpha);
    push(out, add(add(center, scale(r, -1.0f)), u), 0.0f, 0.0f, alpha);
}

void log_sdl(const char* what) {
    std::fprintf(stderr, "sword_fx: %s failed: %s\n", what, SDL_GetError());
}

SDL_GPUShader* create_shader(SDL_GPUDevice* device, SDL_GPUShaderStage stage, const char* entry,
                             uint32_t uniforms, uint32_t samplers, const std::vector<uint8_t>& source) {
    SDL_GPUShaderCreateInfo info{};
    info.code_size = source.size();
    info.code = source.data();
    info.entrypoint = entry;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniforms;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (shader == nullptr) {
        log_sdl(entry);
    }
    return shader;
}

}  // namespace

std::vector<EffectVertex> build_sword_vertices(const SwordFxFrame& frame, const Mat4& viewProj) {
    std::vector<EffectVertex> out;
    if (!frame.active || !(frame.alpha > 0.0f)) {
        return out;
    }
    out.reserve(kSwordMaxVertices);

    // The camera's right/up/forward from the view-projection's rows 0, 1 and 3
    // (row 3 is the perspective's w = the view-space depth along the look).
    Vec3 right = vec3_normalize(Vec3{viewProj.m[0], viewProj.m[4], viewProj.m[8]});
    Vec3 up = vec3_normalize(Vec3{viewProj.m[1], viewProj.m[5], viewProj.m[9]});
    const Vec3 forward = vec3_normalize(Vec3{viewProj.m[3], viewProj.m[7], viewProj.m[11]});
    if (vec3_dot(right, right) < 0.5f) right = Vec3{1.0f, 0.0f, 0.0f};
    if (vec3_dot(up, up) < 0.5f) up = Vec3{0.0f, 1.0f, 0.0f};

    // The lines sample atlas frame 0: u across the width, v along the length
    // (the tip at the frame's bottom edge — the textures are top-down).
    const float u1 = 1.0f / static_cast<float>(frame.atlasCols > 0 ? frame.atlasCols : 1);
    const float v1 = 1.0f / static_cast<float>(frame.atlasRows > 0 ? frame.atlasRows : 1);
    const float half = 0.5f * frame.width;
    for (const Vec3& line : frame.lines) {
        // Camera-facing: the width is across the line and the look; a line
        // along the look falls back to the camera's right.
        Vec3 side = vec3_normalize(vec3_cross(line, forward));
        if (vec3_dot(side, side) < 0.5f) side = right;
        const Vec3 s = scale(side, half);
        const Vec3 far = add(frame.tip, scale(line, frame.length));
        push(out, add(frame.tip, scale(s, -1.0f)), 0.0f, v1, frame.alpha);
        push(out, add(frame.tip, s), u1, v1, frame.alpha);
        push(out, add(far, s), u1, 0.0f, frame.alpha);
        push(out, add(far, scale(s, -1.0f)), 0.0f, 0.0f, frame.alpha);
    }

    push_sprite(out, frame.tip, right, up, frame.flareSize, frame.alpha);
    if (frame.hit) {
        push_sprite(out, frame.hitPoint, right, up, frame.hitSize, frame.alpha);
    }
    return out;
}

std::array<TextureCpu, 4> load_sword_textures(const std::string& libraryRoot,
                                              const std::array<std::string, 4>& rel) {
    std::array<TextureCpu, 4> textures{};
    for (std::size_t i = 0; i < rel.size(); ++i) {
        const std::string path = (std::filesystem::path(libraryRoot) / rel[i]).generic_string();
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            throw std::runtime_error("sword_fx: cannot load texture " + path + " (" + rel[i] +
                                     "): " + stbi_failure_reason());
        }
        textures[i].width = width;
        textures[i].height = height;
        textures[i].rgba8.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        textures[i].sourceRelPath = rel[i];
        stbi_image_free(pixels);
    }
    return textures;
}

SwordFxRenderer::~SwordFxRenderer() { release(); }

void SwordFxRenderer::release() {
    if (device_ == nullptr) {
        return;
    }
    if (vertex_buffer_ != nullptr) SDL_ReleaseGPUBuffer(device_, vertex_buffer_);
    if (index_buffer_ != nullptr) SDL_ReleaseGPUBuffer(device_, index_buffer_);
    for (SDL_GPUTexture* texture : textures_) {
        if (texture != nullptr) SDL_ReleaseGPUTexture(device_, texture);
    }
    if (sampler_ != nullptr) SDL_ReleaseGPUSampler(device_, sampler_);
    if (pipeline_ != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    if (vertex_shader_ != nullptr) SDL_ReleaseGPUShader(device_, vertex_shader_);
    if (fragment_shader_ != nullptr) SDL_ReleaseGPUShader(device_, fragment_shader_);
    vertex_buffer_ = index_buffer_ = nullptr;
    textures_ = {};
    sampler_ = nullptr;
    pipeline_ = nullptr;
    vertex_shader_ = fragment_shader_ = nullptr;
    vertex_count_ = 0;
    device_ = nullptr;
}

bool SwordFxRenderer::create(SDL_GPUDevice* device, const std::array<TextureCpu, 4>& textures,
                             SDL_GPUTextureFormat colorFormat) {
    if (device == nullptr) {
        return false;
    }
    if (device_ != nullptr) {
        return device_ == device;  // idempotent, like EffectRenderer
    }
    device_ = device;

    SDL_GPUBufferCreateInfo vertex_info{};
    vertex_info.size = static_cast<uint32_t>(kSwordMaxVertices * sizeof(EffectVertex));
    vertex_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertex_buffer_ = SDL_CreateGPUBuffer(device_, &vertex_info);

    // The static index list: ten quads (8 lines, the flare, the hit).
    std::array<uint16_t, (kSwordLineQuads + 2) * 6> indices{};
    for (int q = 0; q < kSwordLineQuads + 2; ++q) {
        const uint16_t b = static_cast<uint16_t>(q * 4);
        const uint16_t quad[6] = {b, static_cast<uint16_t>(b + 1), static_cast<uint16_t>(b + 2),
                                  b, static_cast<uint16_t>(b + 2), static_cast<uint16_t>(b + 3)};
        std::memcpy(&indices[static_cast<std::size_t>(q) * 6], quad, sizeof(quad));
    }
    SDL_GPUBufferCreateInfo index_info{};
    index_info.size = sizeof(indices);
    index_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    index_buffer_ = SDL_CreateGPUBuffer(device_, &index_info);

    if (vertex_buffer_ == nullptr || index_buffer_ == nullptr || !create_pipeline(colorFormat) ||
        !upload_textures(textures) || !upload(indices.data(), sizeof(indices), index_buffer_)) {
        std::fprintf(stderr, "sword_fx: the sword pass is unavailable\n");
        release();
        return false;
    }
    return true;
}

bool SwordFxRenderer::create_pipeline(SDL_GPUTextureFormat colorFormat) {
    // tendril.metal sits beside the binary (plan ruling R2, effect.cpp).
    const char* base = SDL_GetBasePath();
    if (base == nullptr) {
        log_sdl("SDL_GetBasePath");
        return false;
    }
    const std::string path = std::string(base) + "tendril.metal";
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        log_sdl(path.c_str());
        return false;
    }
    const std::vector<uint8_t> source(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    SDL_free(data);

    vertex_shader_ = create_shader(device_, SDL_GPU_SHADERSTAGE_VERTEX, "tendril_vertex", 1, 0, source);
    fragment_shader_ =
        create_shader(device_, SDL_GPU_SHADERSTAGE_FRAGMENT, "tendril_fragment_additive", 0, 1, source);
    if (vertex_shader_ == nullptr || fragment_shader_ == nullptr) {
        return false;
    }

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.max_anisotropy = 1.0f;
    sampler_ = SDL_CreateGPUSampler(device_, &sampler_info);
    if (sampler_ == nullptr) {
        log_sdl("SDL_CreateGPUSampler");
        return false;
    }

    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.pitch = sizeof(EffectVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(EffectVertex, px)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(EffectVertex, u)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(EffectVertex, r)};

    // Additive (SRC_ALPHA, ONE), depth-tested without depth write, no culling:
    // the tendril pass's additive state.
    SDL_GPUColorTargetDescription target{};
    target.format = colorFormat;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader_;
    info.fragment_shader = fragment_shader_;
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.target_info.has_depth_stencil_target = true;
    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
    if (pipeline_ == nullptr) {
        log_sdl("SDL_CreateGPUGraphicsPipeline");
        return false;
    }
    return true;
}

// One copy submission and a fence wait (effect.cpp's upload pattern).
bool SwordFxRenderer::upload(const void* data, uint32_t bytes, SDL_GPUBuffer* buffer) {
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
    if (transfer == nullptr) {
        log_sdl("SDL_CreateGPUTransferBuffer");
        return false;
    }
    bool ok = false;
    if (void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false)) {
        std::memcpy(mapped, data, bytes);
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_)) {
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
            const SDL_GPUTransferBufferLocation source{transfer, 0};
            const SDL_GPUBufferRegion destination{buffer, 0, bytes};
            SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
                ok = SDL_WaitForGPUFences(device_, true, &fence, 1);
                SDL_ReleaseGPUFence(device_, fence);
            }
        }
    }
    if (!ok) log_sdl("buffer upload");
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return ok;
}

bool SwordFxRenderer::upload_textures(const std::array<TextureCpu, 4>& source) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        const TextureCpu& texture = source[i];
        if (texture.width <= 0 || texture.height <= 0 ||
            texture.rgba8.size() != static_cast<std::size_t>(texture.width) * texture.height * 4) {
            std::fprintf(stderr, "sword_fx: texture %s has bad dimensions\n", texture.sourceRelPath.c_str());
            return false;
        }
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<uint32_t>(texture.width);
        info.height = static_cast<uint32_t>(texture.height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        textures_[i] = SDL_CreateGPUTexture(device_, &info);
        if (textures_[i] == nullptr) {
            log_sdl("SDL_CreateGPUTexture");
            return false;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<uint32_t>(texture.rgba8.size());
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transfer_info);
        if (transfer == nullptr) {
            log_sdl("SDL_CreateGPUTransferBuffer");
            return false;
        }
        bool ok = false;
        if (void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false)) {
            std::memcpy(mapped, texture.rgba8.data(), texture.rgba8.size());
            SDL_UnmapGPUTransferBuffer(device_, transfer);
            if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_)) {
                SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureTransferInfo from{};
                from.transfer_buffer = transfer;
                from.pixels_per_row = info.width;
                from.rows_per_layer = info.height;
                SDL_GPUTextureRegion to{};
                to.texture = textures_[i];
                to.w = info.width;
                to.h = info.height;
                to.d = 1;
                SDL_UploadToGPUTexture(copy, &from, &to, false);
                SDL_EndGPUCopyPass(copy);
                if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
                    ok = SDL_WaitForGPUFences(device_, true, &fence, 1);
                    SDL_ReleaseGPUFence(device_, fence);
                }
            }
        }
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        if (!ok) {
            log_sdl("texture upload");
            return false;
        }
    }
    return true;
}

void SwordFxRenderer::update(const SwordFxFrame& frame, const Mat4& viewProj) {
    vertex_count_ = 0;
    if (device_ == nullptr) {
        return;
    }
    const std::vector<EffectVertex> vertices = build_sword_vertices(frame, viewProj);
    if (vertices.empty() ||
        !upload(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(EffectVertex)),
                vertex_buffer_)) {
        return;  // draw() stays a no-op
    }
    vertex_count_ = static_cast<int>(vertices.size());
}

void SwordFxRenderer::draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& viewProj) {
    if (device_ == nullptr || vertex_count_ == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(Mat4));
    const SDL_GPUBufferBinding vertex_binding{vertex_buffer_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    const SDL_GPUBufferBinding index_binding{index_buffer_, 0};
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // The lines twice (the base beams, then the wave overlay), the flare, the hit.
    const auto draw_range = [&](int texture, uint32_t firstQuad, uint32_t quads) {
        const SDL_GPUTextureSamplerBinding binding{textures_[static_cast<std::size_t>(texture)], sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUIndexedPrimitives(pass, quads * 6, 1, firstQuad * 6, 0, 0);
    };
    draw_range(0, 0, kSwordLineQuads);
    draw_range(1, 0, kSwordLineQuads);
    draw_range(2, kSwordLineQuads, 1);
    if (vertex_count_ == kSwordMaxVertices) {
        draw_range(3, kSwordLineQuads + 1, 1);
    }
}
