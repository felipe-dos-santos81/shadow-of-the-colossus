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
// needs (the skeleton and the clips live on the rig records in Scene).
struct MeshCpu {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int materialIndex;  // index into Scene::materials, -1 = none
    // The world transform the renderer draws this mesh with: identity for a
    // single-glb load, the placed instance's TRS in a composed scene. The
    // vertices stay in the glb's own space.
    Mat4 model = mat4_identity();
    // The box of `vertices` in the mesh's own vertex space (before `model`):
    // the renderer transforms it by the CURRENT model each frame to frustum-cull
    // static meshes.
    Vec3 localMin{}, localMax{};
    // The rig this mesh is drawn with: an index into Scene::rigs, or -1 for a
    // static mesh (a manifest instance). A single glb always loads as rig 0
    // (includeRig=true, skeleton or not). The player's meshes are 0, each
    // `entity`'s are 1, 2, ... in manifest order.
    int rig = -1;
    // The per-mesh draw gate (S6 Task 5): the renderer skips a mesh whose
    // `visible` is false. The corpse swap toggles the live entity's range off
    // and the dead body's on at Collapse; every other mesh stays true, so a
    // scene that never touches it draws exactly as before. Collision, bounds
    // and the heightmap read the mesh lists, not this flag.
    bool visible = true;
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
    float emissiveFactor[3];  // additive light (`m.emissive.rgb` in the mesh shader)
    int baseColorTexture;  // index into Scene::textures, -1 = none
    int normalTexture;     // index into Scene::textures, -1 = none
    // The glTF emissive slot, used as the corpse's veins carrier: index into Scene::textures,
    // -1 = none. The slot has two different meanings in the mesh shader: `emissiveFactor` is
    // additive light, while this texture is multiplicative (a dark veil): the shipped veins
    // texture is black RGB with the vein mask in alpha, so the shader applies
    // `mix(1, rgb, alpha)` rather than adding it.
    int emissiveTexture = -1;
};

// One skeleton joint, in the skin's own order: `parent` is resolved against
// that order (a joint parented to a non-joint node is a root here), and the
// rest transform is the node's local TRS, decomposed from its matrix.
struct JointCpu {
    std::string name;
    int parent;          // index into SkinCpu::joints, -1 = root
    Mat4 inverseBind;
    Vec3 restT; Quat restR; Vec3 restS;   // rest local TRS, decomposed from the node matrix
    // The joint's bind-pose world transform, composed once at load, parent
    // first, from the rest chain: bindWorld[j] = bindWorld[parent] *
    // TRS(restT, restR, restS). The rest pose is the bind pose, so this is the
    // inverse of inverseBind without ever inverting a matrix. The club is
    // authored in OptBone_sword's bind frame, and palette[j] * bindWorld[j] is
    // the posed joint its vertices are placed by.
    Mat4 bindWorld = mat4_identity();
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

// One loaded glb's rig: its skeleton, its clips, and the half-open range of
// Scene::meshes it owns. Ranges are disjoint and cover every rigged mesh; the
// meshes of a rigged glb that contributed no geometry leave an empty range.
// `dynamic` marks the player's rig (0): the one the follower controller drives
// each frame; an entity's rig (1..) plays its own clip.
struct RigCpu {
    SkinCpu skin;
    std::vector<AnimationCpu> animations;
    std::size_t meshBegin = 0;
    std::size_t meshEnd = 0;
    bool dynamic = false;
};

// A converted model, ready for the renderer: geometry, materials, textures, the
// world-space bounds of all vertices, and — when the source has them — one rig
// record per skeleton-bearing glb (the player first, then the entities).
struct Scene {
    std::vector<MeshCpu> meshes;
    std::vector<MaterialCpu> materials;
    std::vector<TextureCpu> textures;
    Vec3 boundsMin, boundsMax;
    std::vector<RigCpu> rigs;
    std::size_t playerRig = 0;  // index into rigs; 0 after a load
};

// The rig at `rigIndex`. An index that is no rig of this scene (a default-
// constructed Scene, or a hand-built test fixture) answers with a shared empty
// record — no joints, no clips, an empty mesh range — so callers never index an
// empty vector and every query has a defined answer.
inline const RigCpu& rig_at(const Scene& scene, std::size_t rigIndex) {
    static const RigCpu kNoRig{};
    return rigIndex < scene.rigs.size() ? scene.rigs[rigIndex] : kNoRig;
}

// The player's rig — rigs[playerRig], the one the S3 single-rig path drives.
inline const RigCpu& player_rig(const Scene& scene) { return rig_at(scene, scene.playerRig); }

// The palette slot a mesh is drawn with: the mesh's own rig, or slot 0 for a
// static mesh (rig < 0) — an unskinned vertex has zero weights, so the shader
// reads whichever palette is bound and passes it through unchanged. Slots are
// the renderer's per-rig palette buffers, one per `Scene::rigs` entry; a scene
// with no rigs still has slot 0 (the renderer's identity fallback).
inline std::size_t mesh_palette_slot(const Scene& scene, const MeshCpu& mesh) {
    if (mesh.rig <= 0) {
        return 0;  // static (or the player): slot 0
    }
    const std::size_t rig = static_cast<std::size_t>(mesh.rig);
    return rig < scene.rigs.size() ? rig : 0;  // an index no rig of this scene: static slot
}
