#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math3d.h"

// One mesh vertex, laid out for the GPU pipeline: POSITION/NORMAL/TEXCOORD_0 at
// byte offsets 0/12/24, 32 bytes total.
struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};
static_assert(sizeof(Vertex) == 32, "Vertex must match the pipeline vertex layout");

// One primitive from the converted glb, holding every attribute the renderer
// needs (animations and skins are later milestones).
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

// A converted model, ready for the renderer: geometry, materials, textures and
// the world-space bounds of all vertices.
struct Scene {
    std::vector<MeshCpu> meshes;
    std::vector<MaterialCpu> materials;
    std::vector<TextureCpu> textures;
    Vec3 boundsMin, boundsMax;
};
