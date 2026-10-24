#include "additive.h"

// The game's sub-motion layers (additive recon §1; binary FUN_00639520, compose FUN_00622470).

namespace {

constexpr float kLayerFps = 30.0f;  // binary DAT_0126dac4: the layer clock is dt × 30 frames
// The layer's frame-rate scale (record +0x38) and the speed of flag-0x2 rows
// (actor +0x534 × +0x6f8): read as 1. guess
constexpr float kLayerRate = 1.0f;

int playing_slot(const AddLayers& layers, int clip) {
    for (std::size_t i = 0; i < layers.slot.size(); ++i) {
        if (layers.slot[i].state != AddLayer::Free && layers.slot[i].clip == clip) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void start(AddLayer& layer, const AddEdge& edge) {
    layer.clip = edge.to;
    layer.ref = edge.ref;
    layer.frame = 0.0f;
    layer.state = AddLayer::Playing;
}

}  // namespace

void additive_apply(LocalPose& base, const LocalPose& cur, const LocalPose& ref, float w) {
    if (!(w > 0.0f)) {
        return;  // w = 0: no slerp, the pose bit for bit
    }
    const std::size_t n = base.r.size();
    if (cur.r.size() != n || ref.r.size() != n || cur.t.size() != base.t.size() ||
        ref.t.size() != base.t.size()) {
        return;  // poses of another rig: nothing to add
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Quat c = cur.r[i];
        const Quat r = ref.r[i];
        if (c.x != r.x || c.y != r.y || c.z != r.z || c.w != r.w) {  // equal: identity delta
            const Quat delta = quat_mul(c, Quat{-r.x, -r.y, -r.z, r.w});
            const Quat d = w >= 1.0f ? delta : quat_slerp(Quat{0.0f, 0.0f, 0.0f, 1.0f}, delta, w);
            base.r[i] = quat_mul(d, base.r[i]);
        }
        const Vec3 ct = cur.t[i];
        const Vec3 rt = ref.t[i];
        if (ct.x != rt.x || ct.y != rt.y || ct.z != rt.z) {
            base.t[i] = Vec3{base.t[i].x + w * (ct.x - rt.x), base.t[i].y + w * (ct.y - rt.y),
                             base.t[i].z + w * (ct.z - rt.z)};
        }
    }
}

void additive_request(AddLayers& layers, const std::vector<AddEdge>& edges, std::string_view orient) {
    // From-rows, in table order: the first whose `from` plays on a layer and passes its gate is
    // applied and ends the request; one that plays but fails its gate still blocks the Always rows.
    bool blocked = false;
    for (const AddEdge& edge : edges) {
        if (edge.from < 0 || edge.orient != orient) {
            continue;
        }
        const int s = playing_slot(layers, edge.from);
        if (s < 0) {
            continue;
        }
        AddLayer& layer = layers.slot[static_cast<std::size_t>(s)];
        const bool pass = edge.p30 >= 0.0f ? layer.frame > edge.p30 * kLayerRate
                                           : layer.state == AddLayer::Ended;
        if (!pass) {
            blocked = true;
            continue;
        }
        start(layer, edge);  // to == from: Continue (restart in place); else Shift (same slot)
        if (edge.p34 != 0.0f) {
            layer.blendFrames = edge.p34 * kLayerRate;
            layer.weight = 0.0f;
        } else {
            layer.blendFrames = 0.0f;
            layer.weight = 1.0f;
        }
        return;
    }
    if (blocked) {
        return;
    }
    // Always rows (from = −1): every one of this orient applies.
    for (const AddEdge& edge : edges) {
        if (edge.from >= 0 || edge.orient != orient) {
            continue;
        }
        const int s = playing_slot(layers, edge.to);
        if (s >= 0) {
            AddLayer& layer = layers.slot[static_cast<std::size_t>(s)];
            const float gate = edge.p30 * kLayerRate;
            if (gate < 0.0f || layer.frame <= gate) {
                continue;  // p30 < 0 never cancels; else the frame must pass p30
            }
            start(layer, edge);  // "Always Cancel into": restart, ramp over p34 (raw), weight 0
            layer.blendFrames = edge.p34;
            layer.weight = 0.0f;
            continue;
        }
        for (AddLayer& layer : layers.slot) {
            if (layer.state == AddLayer::Free) {
                start(layer, edge);  // a fresh Always start: full weight
                layer.blendFrames = 0.0f;
                layer.weight = 1.0f;
                break;
            }
        }  // no free slot: dropped
    }
}

void additive_step(AddLayers& layers, const std::vector<float>& clipSeconds, float dt) {
    const float frames = dt * kLayerFps;
    for (AddLayer& layer : layers.slot) {
        if (layer.state == AddLayer::Ended || layer.state == AddLayer::Fading) {
            layer = AddLayer{};  // stopped: every boss ADD motion has blend-out 0 (data +0x218), a cut
            continue;
        }
        if (layer.state != AddLayer::Playing) {
            continue;
        }
        const float end = layer.clip >= 0 && static_cast<std::size_t>(layer.clip) < clipSeconds.size()
                              ? clipSeconds[static_cast<std::size_t>(layer.clip)] * kLayerFps
                              : 0.0f;
        layer.frame += frames * kLayerRate;
        if (layer.frame >= end) {
            layer.frame = end;  // held on its last frame for this one step
            layer.state = AddLayer::Ended;
        }
        layer.weight = layer.blendFrames != 0.0f && layer.frame <= layer.blendFrames
                           ? layer.frame / layer.blendFrames
                           : 1.0f;
    }
}

bool additive_playing(const AddLayers& layers, int clip) { return playing_slot(layers, clip) >= 0; }

void additive_compose(LocalPose& pose, const Scene& scene, std::size_t rigIndex,
                      const std::vector<AnimationCpu>& clips, const AddLayers& layers) {
    const auto valid = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < clips.size(); };
    for (const AddLayer& layer : layers.slot) {
        if (layer.state == AddLayer::Free || !valid(layer.clip)) {
            continue;
        }
        const AnimationCpu& clip = clips[static_cast<std::size_t>(layer.clip)];
        // The reference at frame 0 (frame 0 matches every measurement; recon Open).
        const AnimationCpu& ref = clips[static_cast<std::size_t>(valid(layer.ref) ? layer.ref : layer.clip)];
        additive_apply(pose, animation_local_pose(scene, rigIndex, clip, layer.frame / kLayerFps),
                       animation_local_pose(scene, rigIndex, ref, 0.0f), layer.weight);
    }
}
