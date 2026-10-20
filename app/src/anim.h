#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "player.h"
#include "player_state.h"
#include "scene.h"

// CPU pose evaluation for the skeletal path: sampling the animation channels
// parsed into the Scene and composing the skin palette (one matrix per joint)
// that the renderer uploads per frame.
//
// Everything here is per rig: the player's rig and each entity's rig keep their
// own playbacks and palettes, sized by that rig's own joint count. A rig index
// is an index into `Scene::rigs`; an index that is no rig of the scene answers
// from an empty record (see rig_at), never from another rig.

// Wraps `time` into [0, duration) for looping playback; a duration of zero or
// less (an empty clip) yields 0.
float animation_loop_time(float time, float duration);

// Clamps `time` into [0, duration]: the --entity-time pin for deterministic
// captures. 0 for a clip with no duration (an empty clip), and 0 for a
// non-finite input — a capture never samples a time the clip does not have.
float animation_clamp_time(float time, float duration);

// Samples one channel at `t` into `out`: 4 floats for a rotation (path 1),
// 3 for a translation or scale. Values outside the key range clamp to the end
// keys; LINEAR interpolates (rotations via shortest-path slerp, vectors via
// componentwise lerp) and STEP holds the left key. A non-finite `t` (NaN,
// ±inf) samples the clip's start. A channel with no keys — or one whose packed
// values are truncated — writes nothing, leaving the caller's default in place.
void animation_sample(const AnimationChannelCpu& ch, float t, float* out);

// The skin palette of rig `rigIndex` at `time`: one matrix per joint of that
// rig's skin (worldJoint * inverseBind), sized by that rig's own joint count.
// Channels override the component they target (T/R/S); a channel whose joint is
// outside this rig's skin — a clip from another rig — is ignored, never read
// out of bounds. Joints and components without a channel keep their rest
    // values. `time` is taken as given — sample it with playback_time() (the
    // loop wrap, or the --entity-time clamp).
std::vector<Mat4> animation_pose(const Scene& scene, std::size_t rigIndex,
                                 const AnimationCpu& clip, float time);

// World-space bounds of the scene's vertices under a palette (worldJoint * inverseBind):
// sum_i w_i * palette[j_i] * p, then the mesh's model transform. Falls back to bind
// positions when the player's rig has no skin.
void posed_bounds(const Scene& scene, const std::vector<Mat4>& palette,
                  Vec3& outMin, Vec3& outMax);

// The world-space position of the joint named exactly `jointName` in rig
// `rigIndex`, under `palette` and that rig's mesh model transform (so an
// entity's placement and grounding lift are included). The joint's bind origin
// is recovered from its inverse bind matrix and carried through the palette:
// palette[j] * bindOrigin is the posed joint position in mesh space. Returns
// false — leaving `out` untouched — when the joint is absent, the palette is
// shorter than the skin, or the inverse bind is singular (no NaN is written).
bool posed_joint_world_position(const Scene& scene, std::size_t rigIndex,
                                const std::vector<Mat4>& palette,
                                const std::string& jointName, Vec3& out);

// A deterministic 2-second "wave" clip: rotation channels for the first
// min(12, joints) joints of rig `rigIndex` with keys at t = 0/1/2 s holding
// restR, restR tilted 15 degrees about X, and restR again — a smooth wobble
// through LINEAR slerp.
AnimationCpu make_procedural_wave(const Scene& scene, std::size_t rigIndex);

// The clip list and the playback state. The rig's own clips come first; the
// procedural wave is the only clip of a skinned rig that carries none.
// `time` is unwrapped seconds (every pose wraps it into the clip's duration).
struct Playback {
    std::vector<AnimationCpu> clips;
    int index = -1;  // -1 = nothing to play (no skin and no clips)
    float time = 0.0f;
    bool playing = false;
    bool has_procedural = false;
    // A pinned clock (--entity-time) samples `time` clamped into the clip
    // instead of wrapped: a capture at the clip's duration shows its last key
    // rather than looping back to the start. A normal loop leaves this false.
    bool pinned = false;
};

// The clip time this playback samples at: the loop wrap for a running clock,
// the clamped pin for a pinned one. Shared by the pose update and the status
// line so the reported time is always the drawn time. 0 for an empty clip.
float playback_time(const Playback& playback, float duration);

// Fills `playback` from rig `rigIndex` and starts on the first clip: the rig's
// real clips in scene order, or — only when it has no clips at all and has a
// skin — the built-in procedural wave (`has_procedural` marks that fallback).
// A rig with neither a skin nor clips gets an empty list (`index == -1`); an
// index that is no rig of the scene behaves the same way.
void build_playback(const Scene& scene, std::size_t rigIndex, Playback& playback);

// The clip the locomotion controller asks for: still -> "player_a_stand",
// moving -> "player_a_walk", moving with `input.run` -> "player_a_run"
// (movement is any of forward/back/left/right). A missing requested clip falls
// back through walk (while moving), then stand, then the first real clip. A
// playback whose only clip is the procedural stand-in returns 0, and an empty
// clip list returns -1 (nothing to play).
int select_clip_index(const Playback& playback, const PlayerInput& input);

// The clip the combat state machine asks for (S4.1): Grounded delegates to
// `select_clip_index` (the locomotion selector, unchanged), Grabbing ->
// "player_a_grip_loop", Climbing -> "player_a_grip_move_f1", Striking ->
// "player_a_grip_float_u_hi_attack", Falling/Staggered ->
// "player_a_damage_m_fly2". Unlike the locomotion selector there is no
// fallback chain: a missing named clip answers -1, and the app treats that as a
// load error naming the missing clip — never a silent switch to the first clip.
int select_mode_clip_index(const Playback& playback, const PlayerCombat& combat,
                           const PlayerInput& input);

// The index of the clip named `name` in `playback`, or -1 when it is absent.
// Names are exact: this is how a manifest's `entity` clip is resolved.
int clip_index_named(const Playback& playback, const std::string& name);

// Whether `clip` carries the root translation series of rig `rigIndex`: any
// translation channel (path 0) on the skin joint named "Bone_root" — the joint
// the converter's writer targets for the root series. The joint is found by
// name, never by index, in that rig's own skin. Channels targeting a joint
// outside that skin are ignored.
bool clip_has_root_translation(const Scene& scene, std::size_t rigIndex,
                               const AnimationCpu& clip);

// The lift to draw rig `rigIndex`'s meshes with while `clip` plays: 0 when the
// clip carries the rig's root translation series (that series already holds the
// ground height), else the rig's origin-to-feet lift (mesh_ground_offset) over
// the rig's own mesh range. Lifting on top of a root-carrying clip floats the
// character by the offset (~0.76 m for Wander).
float effective_rig_lift(const Scene& scene, std::size_t rigIndex, const AnimationCpu& clip);

// Raises rig `rigIndex`'s meshes by `lift` metres, once for the whole run: the
// rig's model matrices become model * T(0, lift, 0). The lift is a local-space
// measurement (mesh_ground_offset reads the mesh vertices), so it composes
// after the placement and scales with it. A zero (or negative) `lift` and an
// index that is no rig leave every matrix untouched.
void apply_rig_lift(Scene& scene, std::size_t rigIndex, float lift);

// The player's path: the shared grounding rule over the player's rig joints,
// with the caller's mesh range — the S3 call shape.
float effective_player_lift(const Scene& scene, const AnimationCpu& clip,
                            std::size_t meshBegin, std::size_t meshEnd);
