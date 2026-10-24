#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "anim.h"

// The additive motion layer (S13 design D6, Task 6; additive recon §1/§3): a pool of layers,
// each a clip played on top of the base pose as its delta from a reference frame. Pure.

struct AddLayer {
    int clip = -1, ref = -1;  // clip indices; ref: the edge's reference clip (frame 0)
    float frame = 0, blendFrames = 0, weight = 1;
    enum State { Free, Playing, Ended, Fading } state = Free;
};

// The pool. 4 slots: guess.
struct AddLayers {
    std::array<AddLayer, 4> slot;
};

// One additive motion-graph edge (from = −1 for an Always row).
struct AddEdge {
    std::string orient;
    int from, to, ref;
    float p30, p34;
    bool speed;
    // The from motion-def's reference clip (-1 none): a from-row matches a layer playing `from` over this ref,
    // since two motion-defs can play one clip (KIRIN's neck dummy vs swing start). -2: any ref. S13 Task 14.
    int fromRef = -2;
};

// base ← slerp(I, cur·ref⁻¹, w)·base, t ← base + w·(cur_t − ref_t); scale untouched. S13 Task 6.
void additive_apply(LocalPose& base, const LocalPose& cur, const LocalPose& ref, float w);

// Requests `orient` (Continue / Shift / Always / Always-Cancel, recon §1, binary FUN_00639520):
// the first from-row whose `from` plays and passes its p30 gate wins (a playing one that fails
// still blocks the Always rows); then every Always row; no free slot → dropped. S13 Task 6.
void additive_request(AddLayers& layers, const std::vector<AddEdge>& edges, std::string_view orient);

// Advances every layer by dt·30 frames, ramping its weight over blendFrames; a layer reaching its
// end holds its last frame for that step (Ended) and is cut on the next (blend-out 0). S13 Task 6.
void additive_step(AddLayers& layers, const std::vector<float>& clipSeconds, float dt);

// Whether clip `clip` plays in any layer. S13 Task 6.
bool additive_playing(const AddLayers& layers, int clip);

// The pose path's layer pass: every non-free layer's `clip` at its frame, over its `ref` (else the
// clip itself) at frame 0, onto `pose` (additive_apply per bone at the layer's weight). `clips` are
// the rig's playback clips the layer indices name. An all-free pool leaves `pose` untouched.
void additive_compose(LocalPose& pose, const Scene& scene, std::size_t rigIndex,
                      const std::vector<AnimationCpu>& clips, const AddLayers& layers);
