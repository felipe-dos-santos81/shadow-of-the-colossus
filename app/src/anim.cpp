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

std::vector<Mat4> animation_pose(const Scene& scene, const AnimationCpu& clip, float time) {
    const std::vector<JointCpu>& joints = scene.skin.joints;
    const std::size_t joint_count = joints.size();
    std::vector<Mat4> palette(joint_count);
    if (joint_count == 0) {
        return palette;
    }

    // Local TRS per joint, seeded from the rest pose and overridden by each
    // channel's sampled component.
    std::vector<Vec3> translations(joint_count);
    std::vector<Quat> rotations(joint_count);
    std::vector<Vec3> scales(joint_count);
    for (std::size_t i = 0; i < joint_count; ++i) {
        translations[i] = joints[i].restT;
        rotations[i] = joints[i].restR;
        scales[i] = joints[i].restS;
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
            case 0: translations[index] = Vec3{sampled[0], sampled[1], sampled[2]}; break;
            case 1: rotations[index] = Quat{sampled[0], sampled[1], sampled[2], sampled[3]}; break;
            case 2: scales[index] = Vec3{sampled[0], sampled[1], sampled[2]}; break;
            default: break;  // unknown paths are ignored (the parser only emits 0/1/2)
        }
    }

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

AnimationCpu make_procedural_wave(const Scene& scene) {
    AnimationCpu clip;
    clip.name = "procedural-wave";
    clip.duration = 2.0f;

    const std::size_t joint_count = std::min<std::size_t>(12, scene.skin.joints.size());
    const Quat tilt = quat_about_x(15.0f);
    for (std::size_t i = 0; i < joint_count; ++i) {
        const Quat rest = quat_normalize(scene.skin.joints[i].restR);
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
