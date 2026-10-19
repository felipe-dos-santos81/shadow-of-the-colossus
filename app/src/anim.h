#pragma once

#include <vector>

#include "scene.h"

// CPU pose evaluation for the skeletal path: sampling the animation channels
// parsed into the Scene and composing the skin palette (one matrix per joint)
// that the renderer uploads per frame.

// Wraps `time` into [0, duration) for looping playback; a duration of zero or
// less (an empty clip) yields 0.
float animation_loop_time(float time, float duration);

// Samples one channel at `t` into `out`: 4 floats for a rotation (path 1),
// 3 for a translation or scale. Values outside the key range clamp to the end
// keys; LINEAR interpolates (rotations via shortest-path slerp, vectors via
// componentwise lerp) and STEP holds the left key. A non-finite `t` (NaN,
// ±inf) samples the clip's start. A channel with no keys — or one whose packed
// values are truncated — writes nothing, leaving the caller's default in place.
void animation_sample(const AnimationChannelCpu& ch, float t, float* out);

// The skin palette at `time`: one matrix per skin joint, worldJoint *
// inverseBind. Channels override the component they target (T/R/S); joints and
// components without a channel keep their rest values. `time` is taken as
// given — wrap it with animation_loop_time first.
std::vector<Mat4> animation_pose(const Scene& scene, const AnimationCpu& clip, float time);

// A deterministic 2-second "wave" clip: rotation channels for the first
// min(12, joints) joints with keys at t = 0/1/2 s holding restR, restR tilted
// 15 degrees about X, and restR again — a smooth wobble through LINEAR slerp.
AnimationCpu make_procedural_wave(const Scene& scene);

// The clip list and the playback state. The scene's own clips come first; the
// procedural wave is the only clip of a skinned model that carries none.
// `time` is unwrapped seconds (every pose wraps it into the clip's duration).
struct Playback {
    std::vector<AnimationCpu> clips;
    int index = -1;  // -1 = nothing to play (no skin and no clips)
    float time = 0.0f;
    bool playing = false;
    bool has_procedural = false;
};

// Fills `playback` from the scene and starts on the first clip: the model's
// real clips in scene order, or — only when it has no clips at all and has a
// skin — the built-in procedural wave (`has_procedural` marks that fallback).
// A model with neither a skin nor clips gets an empty list (`index == -1`).
void build_playback(const Scene& scene, Playback& playback);
