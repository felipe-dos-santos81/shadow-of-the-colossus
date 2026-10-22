// Tendril effect shader (S6 Task 4, design §8.2): world-space blended geometry.
//
// One vertex entry point and two fragment entry points: the renderer builds a
// premultiplied-alpha pipeline (the veins ribbon's black body and the smoke
// quads) and an additive pipeline (the wisp haze and the veins ribbon's glow)
// from this one source. The vertices are
// already in world space, so there is no model matrix — only the frame's
// view-projection.
//
// SDL_GPU MSL conventions: vertex attributes arrive through [[stage_in]],
// uniform buffers are numbered in push order starting at 0, textures and
// samplers have their own [[texture(n)]] / [[sampler(n)]] namespaces. SDL
// compiles this source at runtime (plan ruling R2: the build machine has no
// Xcode Metal toolchain, so there is no metallib).
//
// Keep the entry points named exactly `tendril_vertex`,
// `tendril_fragment_premultiplied` and `tendril_fragment_additive` — the
// renderer passes those names to SDL_CreateGPUShader.

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 pos   [[attribute(0)]];
    float2 uv    [[attribute(1)]];
    float4 color [[attribute(2)]];
};

// Vertex uniform slot 0: pushed once per effect draw.
struct Uniforms {
    float4x4 viewProj;
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VSOut tendril_vertex(VertexIn in [[stage_in]],
                            constant Uniforms& u [[buffer(0)]]) {
    VSOut out;
    out.position = u.viewProj * float4(in.pos, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

// Premultiplied alpha (SRC_ONE, ONE_MINUS_SRC_ALPHA): the smoke's rgb is scaled
// by its own coverage before the blend, so a near-black tint darkens the scene
// by (1 - a) — the black tendril column. The veins PNG's rgb is black, so the
// ribbon drawn here is black wherever its mask covers.
//
// No gamma op: the effect art is authored in display space and the offscreen
// target is the display-space UNORM mesh.metal encodes to.
fragment float4 tendril_fragment_premultiplied(VSOut in [[stage_in]],
                                               texture2d<float> tex [[texture(0)]],
                                               sampler tex_sampler [[sampler(0)]]) {
    const float4 sampled = tex.sample(tex_sampler, in.uv);
    const float4 color = float4(sampled.rgb * in.color.rgb, sampled.a * in.color.a);
    return float4(color.rgb * color.a, color.a);
}

// Additive (SRC_ALPHA, ONE): the fragment emits the authored tint, and the
// texture's alpha is the coverage that scales it. The veins PNG is black RGB
// with the vein mask in alpha (progress ruling R10), so reading its rgb would
// add nothing: the mask, tinted by the vertex color, is the glow.
fragment float4 tendril_fragment_additive(VSOut in [[stage_in]],
                                          texture2d<float> tex [[texture(0)]],
                                          sampler tex_sampler [[sampler(0)]]) {
    const float coverage = tex.sample(tex_sampler, in.uv).a * in.color.a;
    return float4(in.color.rgb, coverage);
}
