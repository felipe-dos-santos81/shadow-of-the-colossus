#include "anim.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace {

// Right-handed rotation of `degrees` about X, as a unit quaternion.
Quat quat_about_x(float degrees) {
    const float half_angle = 0.5f * degrees * kDegToRad;
    return Quat{std::sin(half_angle), 0.0f, 0.0f, std::cos(half_angle)};
}

// Whether `ch` has at least one complete key: a channel with no keys, or one
// whose packed values are truncated, has nothing to sample.
bool has_sampleable_keys(const AnimationChannelCpu& ch) {
    const std::size_t stride = (ch.path == 1) ? 4u : 3u;
    return !ch.times.empty() && ch.values.size() >= ch.times.size() * stride;
}

// The first float of `key` in a channel's packed value array.
const float* key_values(const std::vector<float>& values, std::size_t key, int stride) {
    return values.data() + key * static_cast<std::size_t>(stride);
}

void copy_key(const std::vector<float>& values, std::size_t key, int stride, float* out) {
    const float* from = key_values(values, key, stride);
    for (int i = 0; i < stride; ++i) {
        out[i] = from[i];
    }
}

}  // namespace

float animation_loop_time(float time, float duration) {
    if (duration <= 0.0f) {
        return 0.0f;
    }
    float wrapped = std::fmod(time, duration);
    if (wrapped < 0.0f) {
        wrapped += duration;
    }
    return wrapped;
}

float animation_clamp_time(float time, float duration) {
    // An empty clip (duration <= 0, or a malformed NaN) has only t = 0, and a
    // non-finite time has no place in a capture: both pin to the clip's start.
    if (!(duration > 0.0f) || !std::isfinite(time)) {
        return 0.0f;
    }
    if (time < 0.0f) {
        return 0.0f;
    }
    return time > duration ? duration : time;
}

void animation_sample(const AnimationChannelCpu& ch, float t, float* out) {
    if (!std::isfinite(t)) {
        // A non-finite time (NaN, ±inf) compares false against both clamp
        // branches below and would index times[] out of bounds; sample the
        // clip's start instead. The CLI rejects a non-finite --anim-time, so
        // this only hardens the public API for any caller.
        t = 0.0f;
    }
    if (!has_sampleable_keys(ch)) {
        return;  // empty or truncated: nothing to write, the caller keeps its default
    }
    const std::size_t key_count = ch.times.size();
    const int stride = (ch.path == 1) ? 4 : 3;

    // Clamped ends: at or beyond the first/last key the end key's value holds.
    if (t <= ch.times.front()) {
        copy_key(ch.values, 0, stride, out);
        return;
    }
    if (t >= ch.times.back()) {
        copy_key(ch.values, key_count - 1, stride, out);
        return;
    }

    // The pair bracketing t: `right` is the first key after t (upper_bound),
    // `left` the key at or before it — both interior thanks to the clamps.
    const std::size_t right = static_cast<std::size_t>(
        std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin());
    const std::size_t left = right - 1;

    if (ch.interpolation == 1) {  // STEP: hold the left key until the next one
        copy_key(ch.values, left, stride, out);
        return;
    }

    const float span = ch.times[right] - ch.times[left];
    const float u = (span > 0.0f) ? (t - ch.times[left]) / span : 0.0f;
    if (ch.path == 1) {
        const float* a = key_values(ch.values, left, stride);
        const float* b = key_values(ch.values, right, stride);
        const Quat q = quat_slerp(Quat{a[0], a[1], a[2], a[3]}, Quat{b[0], b[1], b[2], b[3]}, u);
        out[0] = q.x;
        out[1] = q.y;
        out[2] = q.z;
        out[3] = q.w;
    } else {
        const float* a = key_values(ch.values, left, stride);
        const float* b = key_values(ch.values, right, stride);
        for (int i = 0; i < stride; ++i) {
            out[i] = a[i] + (b[i] - a[i]) * u;
        }
    }
}

LocalPose animation_local_pose(const Scene& scene, std::size_t rigIndex,
                               const AnimationCpu& clip, float time) {
    const std::vector<JointCpu>& joints = rig_at(scene, rigIndex).skin.joints;
    const std::size_t joint_count = joints.size();

    // Local TRS per joint, seeded from the rest pose and overridden by each
    // channel's sampled component.
    LocalPose pose;
    pose.t.resize(joint_count);
    pose.r.resize(joint_count);
    pose.s.resize(joint_count);
    for (std::size_t i = 0; i < joint_count; ++i) {
        pose.t[i] = joints[i].restT;
        pose.r[i] = joints[i].restR;
        pose.s[i] = joints[i].restS;
    }
    for (const AnimationChannelCpu& channel : clip.channels) {
        if (channel.joint < 0 || static_cast<std::size_t>(channel.joint) >= joint_count ||
            !has_sampleable_keys(channel)) {
            continue;  // not this skin's joint, or nothing to sample: keep rest
        }
        float sampled[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        animation_sample(channel, time, sampled);
        const std::size_t index = static_cast<std::size_t>(channel.joint);
        switch (channel.path) {
            case 0: pose.t[index] = Vec3{sampled[0], sampled[1], sampled[2]}; break;
            case 1: pose.r[index] = Quat{sampled[0], sampled[1], sampled[2], sampled[3]}; break;
            case 2: pose.s[index] = Vec3{sampled[0], sampled[1], sampled[2]}; break;
            default: break;  // unknown paths are ignored (the parser only emits 0/1/2)
        }
    }
    return pose;
}

std::vector<Mat4> pose_palette(const Scene& scene, std::size_t rigIndex, const LocalPose& pose) {
    const std::vector<JointCpu>& joints = rig_at(scene, rigIndex).skin.joints;
    const std::size_t joint_count = joints.size();
    std::vector<Mat4> palette(joint_count);
    if (joint_count == 0 || pose.t.size() != joint_count || pose.r.size() != joint_count ||
        pose.s.size() != joint_count) {
        return palette;  // no skin, or a pose sized for another rig
    }
    const std::vector<Vec3>& translations = pose.t;
    const std::vector<Quat>& rotations = pose.r;
    const std::vector<Vec3>& scales = pose.s;

    // World = parent world × local. Converted data lists parents before their
    // children, but resolve parent-first anyway so any joint order is correct:
    // each unresolved chain is walked up to its first resolved ancestor, then
    // composed downward. A malformed cycle is broken (not hung on) by the
    // chain-length guard.
    std::vector<Mat4> world(joint_count);
    std::vector<bool> resolved(joint_count, false);
    std::vector<int> chain;
    for (std::size_t i = 0; i < joint_count; ++i) {
        if (resolved[i]) {
            continue;
        }
        chain.clear();
        int j = static_cast<int>(i);
        while (j >= 0 && !resolved[static_cast<std::size_t>(j)]) {
            chain.push_back(j);
            if (chain.size() > joint_count) {
                break;
            }
            j = joints[static_cast<std::size_t>(j)].parent;
        }
        Mat4 parent_world =
            (j >= 0 && resolved[static_cast<std::size_t>(j)])
                ? world[static_cast<std::size_t>(j)]
                : mat4_identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::size_t index = static_cast<std::size_t>(*it);
            world[index] = mat4_mul(
                parent_world, mat4_from_trs(translations[index], rotations[index], scales[index]));
            parent_world = world[index];
            resolved[index] = true;
        }
    }

    for (std::size_t i = 0; i < joint_count; ++i) {
        palette[i] = mat4_mul(world[i], joints[i].inverseBind);
    }
    return palette;
}

std::vector<Mat4> animation_pose(const Scene& scene, std::size_t rigIndex,
                                 const AnimationCpu& clip, float time) {
    return pose_palette(scene, rigIndex, animation_local_pose(scene, rigIndex, clip, time));
}

LocalPose blend_poses(const LocalPose& a, const LocalPose& b, float w) {
    if (a.t.size() != b.t.size() || a.r.size() != b.r.size() || a.s.size() != b.s.size()) {
        return w < 0.5f ? a : b;  // poses of different rigs cannot mix: take the nearer
    }
    w = std::clamp(w, 0.0f, 1.0f);
    LocalPose out = a;
    for (std::size_t i = 0; i < a.t.size(); ++i) {
        out.t[i] = Vec3{a.t[i].x + (b.t[i].x - a.t[i].x) * w, a.t[i].y + (b.t[i].y - a.t[i].y) * w,
                        a.t[i].z + (b.t[i].z - a.t[i].z) * w};
        out.s[i] = Vec3{a.s[i].x + (b.s[i].x - a.s[i].x) * w, a.s[i].y + (b.s[i].y - a.s[i].y) * w,
                        a.s[i].z + (b.s[i].z - a.s[i].z) * w};
    }
    for (std::size_t i = 0; i < a.r.size(); ++i) {
        out.r[i] = quat_slerp(a.r[i], b.r[i], w);
    }
    return out;
}

void posed_bounds(const Scene& scene, const std::vector<Mat4>& palette, Vec3& outMin,
                  Vec3& outMax) {
    outMin = scene.boundsMin;
    outMax = scene.boundsMax;

    // Without a usable skin the vertices are drawn as authored: bind positions
    // (and the mesh's own model transform below) are the whole answer.
    const SkinCpu& skin = player_rig(scene).skin;
    const bool skinned = !skin.joints.empty() && palette.size() == skin.joints.size();

    bool have_vertex = false;
    Vec3 bounds_min{0.0f, 0.0f, 0.0f};
    Vec3 bounds_max{0.0f, 0.0f, 0.0f};
    for (const MeshCpu& mesh : scene.meshes) {
        for (const Vertex& vertex : mesh.vertices) {
            Vec3 point{vertex.px, vertex.py, vertex.pz};
            if (skinned) {
                // The shader's skinning sum, verbatim: every influence adds
                // w_i * palette[j_i] * p. A vertex with no weights (an
                // unskinned primitive inside a skinned scene) stays at its bind
                // position, as the shader's zero-weight path does.
                float total_weight = 0.0f;
                Vec3 posed{0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 4; ++i) {
                    const float weight = vertex.w[i];
                    const std::size_t joint = vertex.j[i];
                    if (weight == 0.0f || joint >= palette.size()) {
                        continue;
                    }
                    const Mat4& skin = palette[joint];
                    posed.x += weight * (skin.m[0] * point.x + skin.m[4] * point.y +
                                         skin.m[8] * point.z + skin.m[12]);
                    posed.y += weight * (skin.m[1] * point.x + skin.m[5] * point.y +
                                         skin.m[9] * point.z + skin.m[13]);
                    posed.z += weight * (skin.m[2] * point.x + skin.m[6] * point.y +
                                         skin.m[10] * point.z + skin.m[14]);
                    total_weight += weight;
                }
                if (total_weight > 0.0f) {
                    point = posed;
                }
            }

            // The instance's model transform (identity for a single glb).
            const Mat4& model = mesh.model;
            const Vec3 world{model.m[0] * point.x + model.m[4] * point.y +
                                 model.m[8] * point.z + model.m[12],
                             model.m[1] * point.x + model.m[5] * point.y +
                                 model.m[9] * point.z + model.m[13],
                             model.m[2] * point.x + model.m[6] * point.y +
                                 model.m[10] * point.z + model.m[14]};

            if (!have_vertex) {
                bounds_min = world;
                bounds_max = world;
                have_vertex = true;
                continue;
            }
            bounds_min.x = std::min(bounds_min.x, world.x);
            bounds_min.y = std::min(bounds_min.y, world.y);
            bounds_min.z = std::min(bounds_min.z, world.z);
            bounds_max.x = std::max(bounds_max.x, world.x);
            bounds_max.y = std::max(bounds_max.y, world.y);
            bounds_max.z = std::max(bounds_max.z, world.z);
        }
    }

    if (have_vertex) {
        outMin = bounds_min;
        outMax = bounds_max;
    }
}

namespace {

// The joint's bind-pose origin in mesh space: solve inverseBind * p = 0 with
// the affine 3x3 upper-left block. glTF inverse bind matrices are affine
// (last row 0, 0, 0, 1); a singular block is refused rather than inverted.
bool inverse_bind_origin(const Mat4& inverse_bind, Vec3& out) {
    const float a = inverse_bind.m[0], b = inverse_bind.m[4], c = inverse_bind.m[8];
    const float d = inverse_bind.m[1], e = inverse_bind.m[5], f = inverse_bind.m[9];
    const float g = inverse_bind.m[2], h = inverse_bind.m[6], i = inverse_bind.m[10];
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) {
        return false;
    }
    const float inv_det = 1.0f / determinant;
    const float inv00 = (e * i - f * h) * inv_det;
    const float inv01 = (c * h - b * i) * inv_det;
    const float inv02 = (b * f - c * e) * inv_det;
    const float inv10 = (f * g - d * i) * inv_det;
    const float inv11 = (a * i - c * g) * inv_det;
    const float inv12 = (c * d - a * f) * inv_det;
    const float inv20 = (d * h - e * g) * inv_det;
    const float inv21 = (b * g - a * h) * inv_det;
    const float inv22 = (a * e - b * d) * inv_det;
    const float tx = inverse_bind.m[12];
    const float ty = inverse_bind.m[13];
    const float tz = inverse_bind.m[14];
    out = Vec3{-(inv00 * tx + inv01 * ty + inv02 * tz),
               -(inv10 * tx + inv11 * ty + inv12 * tz),
               -(inv20 * tx + inv21 * ty + inv22 * tz)};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

// A matrix * (point, 1), returning the xyz part.
Vec3 transform_point(const Mat4& m, Vec3 p) {
    return Vec3{m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

// Whether every component of `v` is finite.
bool is_finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

bool posed_joint_world_position(const Scene& scene, std::size_t rigIndex,
                                const std::vector<Mat4>& palette,
                                const std::string& jointName, Vec3& out) {
    const RigCpu& rig = rig_at(scene, rigIndex);
    for (std::size_t joint = 0; joint < rig.skin.joints.size(); ++joint) {
        if (rig.skin.joints[joint].name != jointName) {
            continue;
        }
        if (joint >= palette.size()) {
            return false;
        }
        Vec3 bind_origin{};
        if (!inverse_bind_origin(rig.skin.joints[joint].inverseBind, bind_origin)) {
            return false;
        }
        // Mesh space -> world: every mesh of the rig shares the rig's placement
        // (compose_scene bakes it; apply_rig_lift post-multiplies the ground
        // lift), so the first mesh's model is the rig's model — but only when
        // that mesh is actually the rig's. An empty range (a rigged glb that
        // contributed no geometry) has no model of its own: it must not borrow
        // the next rig's first mesh.
        Mat4 model = mat4_identity();
        if (rig.meshBegin < rig.meshEnd && rig.meshBegin < scene.meshes.size()) {
            model = scene.meshes[rig.meshBegin].model;
        }
        const Vec3 posed = transform_point(palette[joint], bind_origin);
        out = transform_point(model, posed);
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }
    return false;
}

bool rig_bone_world(const Scene& scene, std::size_t rigIndex, const std::vector<Mat4>& palette,
                    const std::string& boneName, Mat4& out) {
    const RigCpu& rig = rig_at(scene, rigIndex);
    for (std::size_t joint = 0; joint < rig.skin.joints.size(); ++joint) {
        if (rig.skin.joints[joint].name != boneName) {
            continue;
        }
        if (joint >= palette.size()) {
            return false;
        }
        // The rig's placement, as posed_joint_world_position takes it.
        Mat4 model = mat4_identity();
        if (rig.meshBegin < rig.meshEnd && rig.meshBegin < scene.meshes.size()) {
            model = scene.meshes[rig.meshBegin].model;
        }
        out = mat4_mul(mat4_mul(model, palette[joint]), rig.skin.joints[joint].bindWorld);
        return true;
    }
    return false;
}

Mat4 club_model(const Scene& scene, const std::vector<Mat4>& entityPalette,
                std::size_t swordJoint, const Mat4& entityMeshModel) {
    // The interface carries no rig index: the palette's size is what names its
    // rig. An empty palette (an unskinned rig) or an out-of-range joint has no
    // binding at all.
    if (entityPalette.empty() || swordJoint >= entityPalette.size()) {
        return mat4_identity();
    }

    // An entity rig is preferred over the player's: the club is an entity prop,
    // and a scene can hold both rigs. A single-rig scene (the tests, a
    // standalone load) falls back to whichever rig matches the palette.
    const RigCpu* binding = nullptr;
    for (std::size_t i = 0; i < scene.rigs.size() && binding == nullptr; ++i) {
        if (i == scene.playerRig) {
            continue;
        }
        if (scene.rigs[i].skin.joints.size() == entityPalette.size()) {
            binding = &scene.rigs[i];
        }
    }
    if (binding == nullptr && scene.playerRig < scene.rigs.size() &&
        scene.rigs[scene.playerRig].skin.joints.size() == entityPalette.size()) {
        binding = &scene.rigs[scene.playerRig];
    }
    if (binding == nullptr || swordJoint >= binding->skin.joints.size()) {
        return mat4_identity();
    }

    // The grip socket's pose in mesh space, carried back through the bind world
    // into the frame the prop's vertices were authored in.
    return mat4_mul(mat4_mul(entityMeshModel, entityPalette[swordJoint]),
                    binding->skin.joints[swordJoint].bindWorld);
}

bool posed_weakpoint_proxy(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette,
                           const std::string& materialName, Vec3& outCenter,
                           float& outRadius) {
    const RigCpu& rig = rig_at(scene, rigIndex);
    const std::size_t joint_count = rig.skin.joints.size();

    // The shell is posed with the rig's own skin: without one, or with a
    // palette sized for another rig, no vertex can be placed.
    if (joint_count == 0 || palette.size() != joint_count) {
        return false;
    }

    // The rig's own half-open mesh range, clamped to the scene: an entity shell
    // is never searched in another rig's geometry, and a malformed range end
    // never indexes past the meshes.
    const std::size_t mesh_end = std::min(rig.meshEnd, scene.meshes.size());

    bool have_vertex = false;
    Vec3 sum{0.0f, 0.0f, 0.0f};
    Vec3 bounds_min{0.0f, 0.0f, 0.0f};
    Vec3 bounds_max{0.0f, 0.0f, 0.0f};
    std::size_t vertex_count = 0;

    for (std::size_t index = rig.meshBegin; index < mesh_end; ++index) {
        const MeshCpu& mesh = scene.meshes[index];
        if (mesh.materialIndex < 0 ||
            static_cast<std::size_t>(mesh.materialIndex) >= scene.materials.size() ||
            scene.materials[static_cast<std::size_t>(mesh.materialIndex)].name != materialName) {
            continue;
        }

        for (const Vertex& vertex : mesh.vertices) {
            // The shader's skinning sum, verbatim: every influence adds
            // w_i * palette[j_i] * p. A vertex with no weights (an unskinned
            // primitive inside a skinned mesh) stays at its bind position, as
            // the shader's zero-weight path does.
            Vec3 posed{0.0f, 0.0f, 0.0f};
            float total_weight = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const float weight = vertex.w[i];
                const std::size_t joint = vertex.j[i];
                if (weight == 0.0f || joint >= palette.size()) {
                    continue;
                }
                if (!std::isfinite(weight)) {
                    return false;
                }
                const Mat4& skin = palette[joint];
                posed.x += weight * (skin.m[0] * vertex.px + skin.m[4] * vertex.py +
                                     skin.m[8] * vertex.pz + skin.m[12]);
                posed.y += weight * (skin.m[1] * vertex.px + skin.m[5] * vertex.py +
                                     skin.m[9] * vertex.pz + skin.m[13]);
                posed.z += weight * (skin.m[2] * vertex.px + skin.m[6] * vertex.py +
                                     skin.m[10] * vertex.pz + skin.m[14]);
                total_weight += weight;
            }
            if (!std::isfinite(total_weight)) {
                return false;
            }
            const Vec3 mesh_space =
                total_weight > 0.0f ? posed : Vec3{vertex.px, vertex.py, vertex.pz};

            // The instance's model transform (identity for a single glb).
            const Vec3 world = transform_point(mesh.model, mesh_space);
            if (!is_finite(world)) {
                return false;
            }

            sum.x += world.x;
            sum.y += world.y;
            sum.z += world.z;
            ++vertex_count;
            if (!have_vertex) {
                bounds_min = world;
                bounds_max = world;
                have_vertex = true;
            } else {
                bounds_min.x = std::min(bounds_min.x, world.x);
                bounds_min.y = std::min(bounds_min.y, world.y);
                bounds_min.z = std::min(bounds_min.z, world.z);
                bounds_max.x = std::max(bounds_max.x, world.x);
                bounds_max.y = std::max(bounds_max.y, world.y);
                bounds_max.z = std::max(bounds_max.z, world.z);
            }
        }
    }

    if (!have_vertex) {
        return false;  // no mesh of this rig carries the shell material
    }

    const float inv_count = 1.0f / static_cast<float>(vertex_count);
    const Vec3 center{sum.x * inv_count, sum.y * inv_count, sum.z * inv_count};
    if (!is_finite(center)) {
        return false;
    }

    // The farthest of the posed bounding box's eight corners from the mean.
    float max_distance_squared = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) ? bounds_max.x : bounds_min.x,
                         (corner & 2) ? bounds_max.y : bounds_min.y,
                         (corner & 4) ? bounds_max.z : bounds_min.z};
        const float dx = point.x - center.x;
        const float dy = point.y - center.y;
        const float dz = point.z - center.z;
        max_distance_squared =
            std::max(max_distance_squared, dx * dx + dy * dy + dz * dz);
    }
    const float radius = std::sqrt(max_distance_squared);
    if (!std::isfinite(radius)) {
        return false;
    }

    outCenter = center;
    outRadius = radius;
    return true;
}

AnimationCpu make_procedural_wave(const Scene& scene, std::size_t rigIndex) {
    AnimationCpu clip;
    clip.name = "procedural-wave";
    clip.duration = 2.0f;

    const std::vector<JointCpu>& joints = rig_at(scene, rigIndex).skin.joints;
    const std::size_t joint_count = std::min<std::size_t>(12, joints.size());
    const Quat tilt = quat_about_x(15.0f);
    for (std::size_t i = 0; i < joint_count; ++i) {
        const Quat rest = quat_normalize(joints[i].restR);
        const Quat raised = quat_normalize(quat_mul(rest, tilt));

        AnimationChannelCpu channel;
        channel.joint = static_cast<int>(i);
        channel.path = 1;           // rotation
        channel.interpolation = 0;  // LINEAR
        channel.times = {0.0f, 1.0f, 2.0f};
        channel.values = {rest.x,   rest.y,   rest.z,   rest.w,
                          raised.x, raised.y, raised.z, raised.w,
                          rest.x,   rest.y,   rest.z,   rest.w};
        clip.channels.push_back(std::move(channel));
    }
    return clip;
}

void build_playback(const Scene& scene, std::size_t rigIndex, Playback& playback) {
    const RigCpu& rig = rig_at(scene, rigIndex);
    playback.clips = rig.animations;
    playback.index = -1;
    playback.time = 0.0f;
    playback.playing = false;
    playback.has_procedural = false;
    playback.pinned = false;

    // The stand-in is the fallback, never a peer of the model's own clips: it
    // is appended only for a skinned rig whose record carries none.
    if (playback.clips.empty() && !rig.skin.joints.empty()) {
        playback.clips.push_back(make_procedural_wave(scene, rigIndex));
        playback.has_procedural = true;
    }
    if (!playback.clips.empty()) {
        playback.index = 0;
        playback.playing = true;
    }
}

int clip_index_named(const Playback& playback, const std::string& name) {
    for (std::size_t i = 0; i < playback.clips.size(); ++i) {
        if (playback.clips[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float playback_time(const Playback& playback, float duration) {
    return playback.pinned ? animation_clamp_time(playback.time, duration)
                           : animation_loop_time(playback.time, duration);
}

int select_clip_index(const Playback& playback, const PlayerInput& input) {
    if (playback.clips.empty()) {
        return -1;  // nothing to play
    }
    if (playback.has_procedural) {
        return 0;  // the stand-in is the only clip: locomotion has no real target
    }

    const bool moving = input.forward || input.back || input.left || input.right;
    const char* requested = "player_a_stand";
    if (moving) {
        requested = input.run ? "player_a_run" : "player_a_walk";
    }

    // The requested clip, then the fallback chain: walk while moving, stand,
    // and finally the first real clip in scene order.
    if (const int index = clip_index_named(playback, requested); index >= 0) {
        return index;
    }
    if (moving) {
        if (const int index = clip_index_named(playback, "player_a_walk"); index >= 0) {
            return index;
        }
    }
    if (const int index = clip_index_named(playback, "player_a_stand"); index >= 0) {
        return index;
    }
    return 0;
}

int select_mode_clip_index(const Playback& playback, const PlayerCombat& combat,
                           const PlayerInput& input) {
    if (playback.clips.empty()) {
        return -1;  // nothing to play
    }
    switch (combat.mode) {
        case PlayerMode::Grounded:
            return select_clip_index(playback, input);
        case PlayerMode::Grabbing:
            return clip_index_named(playback, "player_a_grip_loop");
        case PlayerMode::Climbing:
            return clip_index_named(playback, "player_a_grip_move_f1");
        case PlayerMode::Striking:
            return clip_index_named(playback, "player_a_grip_float_u_hi_attack");
        case PlayerMode::Falling:
        case PlayerMode::Staggered:
            return clip_index_named(playback, "player_a_damage_m_fly2");
        case PlayerMode::Hurt:
            if (!combat.heavyHit) {
                return clip_index_named(playback, "player_a_damage_m_fly2");
            }
            // Heavy: h_fly1 runs 0.60 s, then h_fly_end takes the 0.63 s tail.
            if (const int fly = clip_index_named(playback, "player_a_damage_h_fly1"); fly >= 0) {
                const float duration =
                    playback.clips[static_cast<std::size_t>(fly)].duration;
                if (combat.modeSeconds < duration) {
                    return fly;
                }
            }
            return clip_index_named(playback, "player_a_damage_h_fly_end");
        case PlayerMode::Dead:
            return clip_index_named(playback, "player_a_fall_damage_to_death");
    }
    return -1;
}

namespace {

// Whether `clip` carries the root translation series of a skin with these
// joints: any translation channel (path 0) on the joint named exactly
// "Bone_root" — the joint the converter's writer targets for the root series.
// The joint is found by name, never by index, so the answer travels with the
// rig; a channel on a joint outside this skin is not this rig's series.
bool root_translation_on(const std::vector<JointCpu>& joints, const AnimationCpu& clip) {
    for (const AnimationChannelCpu& channel : clip.channels) {
        if (channel.path != 0 || channel.joint < 0 ||
            static_cast<std::size_t>(channel.joint) >= joints.size()) {
            continue;  // not a translation channel, or not this skin's joint
        }
        if (joints[static_cast<std::size_t>(channel.joint)].name == "Bone_root") {
            return true;
        }
    }
    return false;
}

// The one grounding rule behind both lift helpers: a root-carrying clip already
// holds the ground height (lifting on top floats the model by the offset), so
// it needs 0; otherwise the meshes' origin-to-feet offset applies.
float lift_for(const Scene& scene, const std::vector<JointCpu>& joints,
               const AnimationCpu& clip, std::size_t meshBegin, std::size_t meshEnd) {
    if (root_translation_on(joints, clip)) {
        return 0.0f;
    }
    return mesh_ground_offset(scene, meshBegin, meshEnd);
}

}  // namespace

bool clip_has_root_translation(const Scene& scene, std::size_t rigIndex,
                               const AnimationCpu& clip) {
    return root_translation_on(rig_at(scene, rigIndex).skin.joints, clip);
}

float effective_rig_lift(const Scene& scene, std::size_t rigIndex, const AnimationCpu& clip) {
    const RigCpu& rig = rig_at(scene, rigIndex);
    return lift_for(scene, rig.skin.joints, clip, rig.meshBegin, rig.meshEnd);
}

void apply_rig_lift(Scene& scene, std::size_t rigIndex, float lift) {
    if (!(lift > 0.0f) || rigIndex >= scene.rigs.size()) {
        return;  // no lift, or no such rig: the placements stand as authored
    }
    const RigCpu& rig = scene.rigs[rigIndex];
    const Mat4 lift_matrix =
        mat4_from_trs(Vec3{0.0f, lift, 0.0f}, Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    for (std::size_t i = rig.meshBegin; i < rig.meshEnd && i < scene.meshes.size(); ++i) {
        scene.meshes[i].model = mat4_mul(scene.meshes[i].model, lift_matrix);
    }
}

float effective_player_lift(const Scene& scene, const AnimationCpu& clip,
                            std::size_t meshBegin, std::size_t meshEnd) {
    return lift_for(scene, player_rig(scene).skin.joints, clip, meshBegin, meshEnd);
}
