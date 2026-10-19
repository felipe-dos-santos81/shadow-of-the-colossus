// Mesh shader for the viewer: one lit, textured, skinned pass.
//
// SDL_GPU MSL conventions: vertex attributes arrive through [[stage_in]];
// uniform buffers are numbered in push order starting at 0, storage buffers
// follow them; textures and samplers have their own [[texture(n)]] /
// [[sampler(n)]] namespaces. SDL compiles this source at runtime with
// newLibraryWithSource: (plan ruling R2: the build machine has no Xcode Metal
// toolchain, so there is no metallib).
//
// Keep the entry points named `mesh_vertex` / `mesh_fragment` — the renderer
// passes those names to SDL_CreateGPUShader.

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv  [[attribute(2)]];
    ushort4 joints [[attribute(3)]];   // JOINTS_0: indices into the palette
    float4 weights [[attribute(4)]];   // WEIGHTS_0: zero = unskinned
};

// Vertex uniform slot 0: pushed once per frame.
struct Uniforms {
    float4x4 viewProj;
};

// Vertex uniform slot 1: pushed per mesh.
struct PerDraw {
    float4x4 model;
};

// Fragment uniform slot 0: pushed per mesh. std140: both fields are vec4.
struct Material {
    float4 baseColor;  // linear baseColorFactor (RGBA)
    float4 emissive;   // linear emissiveFactor in xyz (w unused pad)
};

struct VSOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
};

// Vertex storage buffer 0 (`[[buffer(2)]]`: SDL_GPU numbers uniform buffers
// first, storage buffers after): one worldJoint * inverseBind matrix per skin
// joint, uploaded per frame. A scene without a skin keeps a single identity
// matrix here.
vertex VSOut mesh_vertex(VertexIn in [[stage_in]],
                         constant Uniforms& u [[buffer(0)]],
                         constant PerDraw& d [[buffer(1)]],
                         device const float4x4* palette [[buffer(2)]]) {
    VSOut out;
    // Linear-blend skinning: blend the four joint matrices per vertex. A zero
    // weight sum means "unskinned" (the loader writes zero weights for a
    // primitive without JOINTS_0/WEIGHTS_0): the vertex passes through
    // untransformed. Blending instead would select palette[0] for it — in a
    // composed scene that is the player's animated root bone, which would warp
    // every unskinned prop with the player's animation.
    const float weight_sum = in.weights.x + in.weights.y + in.weights.z + in.weights.w;
    float4 skinned = float4(in.pos, 1.0);
    float4 skinned_normal = float4(in.nrm, 0.0);
    if (weight_sum > 0.0) {
        skinned = in.weights.x * (palette[in.joints.x] * float4(in.pos, 1.0))
                + in.weights.y * (palette[in.joints.y] * float4(in.pos, 1.0))
                + in.weights.z * (palette[in.joints.z] * float4(in.pos, 1.0))
                + in.weights.w * (palette[in.joints.w] * float4(in.pos, 1.0));
        skinned_normal = in.weights.x * (palette[in.joints.x] * float4(in.nrm, 0.0))
                       + in.weights.y * (palette[in.joints.y] * float4(in.nrm, 0.0))
                       + in.weights.z * (palette[in.joints.z] * float4(in.nrm, 0.0))
                       + in.weights.w * (palette[in.joints.w] * float4(in.nrm, 0.0));
    }
    // The blended normal is normalized by the fragment shader.
    float4 world = d.model * float4(skinned.xyz, 1.0);
    out.position = u.viewProj * world;
    out.normal = (d.model * float4(skinned_normal.xyz, 0.0)).xyz;
    out.uv = in.uv;
    return out;
}

fragment float4 mesh_fragment(VSOut in [[stage_in]],
                              constant Material& m [[buffer(0)]],
                              texture2d<float> albedo [[texture(0)]],
                              sampler albedo_sampler [[sampler(0)]]) {
    // glTF base color PNGs are sRGB-encoded: un-gamma before lighting.
    float3 albedo_linear = pow(albedo.sample(albedo_sampler, in.uv).rgb, float3(2.2));

    // Provisional key light (world space) and flat ambient, until a lighting
    // task replaces them.
    const float3 key_light = normalize(float3(0.4, 0.8, 0.6));
    const float ambient = 0.1;

    // Lambert key light plus flat ambient, on the world-space normal.
    float3 n = normalize(in.normal);
    float n_dot_l = max(dot(n, key_light), 0.0);

    float3 lit = albedo_linear * (n_dot_l + ambient) * m.baseColor.rgb + m.emissive.rgb;

    // The offscreen target has the swapchain's format, which the SDR swapchain
    // composition makes an 8-bit UNORM: encode to sRGB here. If the swapchain
    // ever becomes an _SRGB variant, drop this output pow only — the albedo
    // decode above stays, because the sampled PNGs are always created UNORM.
    return float4(pow(max(lit, float3(0.0)), float3(1.0 / 2.2)), 1.0);
}
