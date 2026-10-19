#pragma once

#include <cstddef>
#include <vector>

#include "player.h"
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

// The clip the locomotion controller asks for: still -> "player_a_stand",
// moving -> "player_a_walk", moving with `input.run` -> "player_a_run"
// (movement is any of forward/back/left/right). A missing requested clip falls
// back through walk (while moving), then stand, then the first real clip. A
// playback whose only clip is the procedural stand-in returns 0, and an empty
// clip list returns -1 (nothing to play).
int select_clip_index(const Playback& playback, const PlayerInput& input);

// Whether `clip` carries the root translation series: any translation channel
// (path 0) on the skin joint named "Bone_root" — the joint the converter's
// writer targets for the root series. The joint is found by name, never by
// index. Channels targeting a joint outside this skin are ignored.
bool clip_has_root_translation(const Scene& scene, const AnimationCpu& clip);

// The lift to draw the composed scene's player meshes with while `clip` plays:
// 0 when the clip carries the root translation series (that series already
// holds the ground height), else the S3 origin-to-feet lift
// (player_ground_offset), lifted from the player's meshes
// [playerMeshBegin, end). Lifting on top of a root-carrying clip floats the
// character by the offset (~0.76 m for Wander).
float effective_player_lift(const Scene& scene, const AnimationCpu& clip,
                            std::size_t playerMeshBegin);
