// HUD overlay shader: one alpha-blended, textured quad per draw.
//
// The geometry is a single unit quad [0,1]x[0,1] (y down) created once by the
// renderer; a per-draw uniform places it in NDC. A 1x1 white texture plus a
// color tint draws the solid bars, and the SDL_ttf stamina label is a plain
// RGBA texture sampled 1:1.
//
// SDL_GPU MSL conventions: vertex attributes arrive through [[stage_in]];
// uniform buffers are numbered in push order starting at 0; textures and
// samplers use [[texture(n)]] / [[sampler(n)]]. SDL compiles this source at
// runtime (plan ruling R2: no Xcode Metal toolchain, no metallib).
//
// Keep the entry points named `hud_vertex` / `hud_fragment` — the renderer
// passes those names to SDL_CreateGPUShader.

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 pos [[attribute(0)]];  // unit quad, y down
    float2 uv  [[attribute(1)]];
};

// Uniform slot 0 on both stages. std140: two vec4s.
struct Draw {
    float4 xform;  // xy = NDC origin (left, top), zw = NDC size (width, height)
    float4 color;  // RGBA tint multiplied into the sampled texel
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
};

vertex VSOut hud_vertex(VertexIn in [[stage_in]], constant Draw& d [[buffer(0)]]) {
    VSOut out;
    // y is flipped here: NDC grows up, the authored quad grows down from its
    // top edge, so the origin is the top-left corner in NDC terms.
    out.position = float4(d.xform.x + in.pos.x * d.xform.z,
                          d.xform.y - in.pos.y * d.xform.w,
                          0.0,
                          1.0);
    out.uv = in.uv;
    return out;
}

fragment float4 hud_fragment(VSOut in [[stage_in]],
                             constant Draw& d [[buffer(0)]],
                             texture2d<float> source [[texture(0)]],
                             sampler source_sampler [[sampler(0)]]) {
    // Colors are authored in the same display-space values the mesh shader
    // writes (it encodes its output to sRGB itself), so no decode here.
    return source.sample(source_sampler, in.uv) * d.color;
}
