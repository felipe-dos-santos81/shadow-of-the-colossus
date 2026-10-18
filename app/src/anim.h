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
// componentwise lerp) and STEP holds the left key. A channel with no keys
// writes nothing, leaving the caller's default in place.
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
