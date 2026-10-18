#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math3d.h"

// One mesh vertex, laid out for the GPU pipeline: POSITION/NORMAL/TEXCOORD_0 at
// byte offsets 0/12/24 and the skin attributes JOINTS_0 (USHORT4) and WEIGHTS_0
// (FLOAT4) at 32/40, 56 bytes total.
struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    uint16_t j[4];  // JOINTS_0: indices into SkinCpu::joints
    float w[4];     // WEIGHTS_0
};
static_assert(sizeof(Vertex) == 56, "Vertex must match the pipeline vertex layout");

// One primitive from the converted glb, holding every attribute the renderer
// needs (the skeleton and the clips live on Scene).
struct MeshCpu {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int materialIndex;  // index into Scene::materials, -1 = none
};

// One decoded PNG, RGBA8, top-down (glTF image origin).
struct TextureCpu {
    int width;
    int height;
    std::vector<uint8_t> rgba8;
    std::string sourceRelPath;  // library-relative, '/' separators
};

struct MaterialCpu {
    std::string name;
    float baseColorFactor[4];
    float emissiveFactor[3];
    int baseColorTexture;  // index into Scene::textures, -1 = none
    int normalTexture;     // index into Scene::textures, -1 = none
};

// One skeleton joint, in the skin's own order: `parent` is resolved against
// that order (a joint parented to a non-joint node is a root here), and the
// rest transform is the node's local TRS, decomposed from its matrix.
struct JointCpu {
    std::string name;
    int parent;          // index into SkinCpu::joints, -1 = root
    Mat4 inverseBind;
    Vec3 restT; Quat restR; Vec3 restS;   // rest local TRS, decomposed from the node matrix
};

// One animation channel, flattened for sampling: `values` holds 3 (T/S) or 4
// (R) floats per key, one key per entry in `times`.
struct AnimationChannelCpu {
    int joint;           // index into SkinCpu::joints (channels to non-joint nodes are skipped)
    int path;            // 0 = translation, 1 = rotation, 2 = scale
    int interpolation;   // 0 = LINEAR, 1 = STEP
    std::vector<float> times;   // seconds, strictly increasing
    std::vector<float> values;  // 3 (T/S) or 4 (R) floats per key
};

struct AnimationCpu {
    std::string name;
    float duration;  // the maximum input time in seconds, 0 when the clip is empty
    std::vector<AnimationChannelCpu> channels;
};

struct SkinCpu {
    std::vector<JointCpu> joints;
};

// A converted model, ready for the renderer: geometry, materials, textures, the
// world-space bounds of all vertices, and — when the source has them — the
// skeleton and its clips.
struct Scene {
    std::vector<MeshCpu> meshes;
    std::vector<MaterialCpu> materials;
    std::vector<TextureCpu> textures;
    Vec3 boundsMin, boundsMax;
    SkinCpu skin;
    std::vector<AnimationCpu> animations;
};
