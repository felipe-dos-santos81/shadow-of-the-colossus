#pragma once

// The objanim event consumer (S12 Task 8; design D6, rulings §13.1/§13.5/§13.6).
//
// Reads a `<glb stem>.events.json` sidecar (`sotc-objanim-events/1`, written by the
// converter's EventsSidecar: docs/notes/aqua-core-api.md §12) and answers three
// per-frame questions for one colossus: which damage rows hit the player
// (events_damage, plus EventsSpawnRunner for the damage objanims a type-29 event
// spawns), which effect events started (events_fired), and whether the player's
// position point is inside a judge volume (events_player_in_judge, the attackdata
// runner's ATK/FIX/ATTAIN tests). Pure: no SDL, no GPU, no Scene — the caller hands
// the posed joints in through PoseView.
//
// Pose space (R4 decode, docs/notes/s12-recon.md §R4 "Values for later tasks"). The
// sidecar is already in the glb frame (x and quaternion x negated once by the
// converter), so nothing here negates x again:
// - bone `B`: world = joint(B) · T(pos) · R(quat)            (R4 measured: slam blade
//   0.70 m / 0.2°, far end 0.09 m from the tip; feet 1.92/0.54 m inside r 7.26)
// - entity / entityRoot: world = entity · T(pos) · R(quat)   (R4 measured on Quadratus)
// - skelton: centre = entity · (−x, hipY + y, −z), i.e. R(180° about y) of the sidecar
//   value lifted by the hip height (R4 data + measured: WEAPON_ATK_ATTAIN lands 30 m
//   ahead). Orientation = entity · R(quat): the sectors open FORWARD (D6, a guess —
//   the binary composes R(180°) into the rotation too, which would open them backward
//   and put LEG_R_ATK's sector on his left; a walk row checks it).
// - optBone k: the k-th OptBone_* joint in glb joint order (PoseView::optBones; a guess).
// - spawn: the spawning effect's posed transform (EventsSpawnRunner).
// Volume sizes: box = half extents along the volume's axes; sphere radius = size.x;
// cylinder/ellipsoid axes (rx, half-height, rz); sectors about local +z.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "math3d.h"

struct EventsParent {
    enum Kind { Entity, EntityRoot, Skelton, Bone, OptBone, Spawn } kind = Entity;
    std::string bone;  // Bone: the glb joint of the same name
    int optBone = -1;  // OptBone: k
};

// One pose key: `f` is the objanim frame (-1 for a row's single `static` key);
// `size` is the volume size, or an effect's `vec` (meaning open, unused).
struct EventsKey {
    int f = -1;
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Quat quat{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 size{0.0f, 0.0f, 0.0f};
};

// A damage (types 3/10/13) or judge (44/46) volume row.
struct EventsVolume {
    enum Shape { Sphere, Cylinder, Box, Ellipsoid } shape = Sphere;
    std::string name;
    EventsParent parent;
    std::vector<EventsKey> keys;              // ascending f; one key (f -1) when static
    std::vector<std::pair<int, int>> windows; // damage: active frames [first, last]
    bool hasDef = false;                      // damage: the map's damage_def row exists
    float amount = 0.0f, reaction = 0.0f;     // damage_def +0x1c / +0x18 (raw)
    float inner = 0.0f;                       // judge 44: inner-radius fraction
    float angle = 2.0f * kPi;                 // judge 44: full sector width (rad)
};

// A type-29 effect row: starts at each event window's first frame.
struct EventsEffect {
    std::string name;
    EventsParent parent;
    std::vector<std::pair<int, int>> events;  // [start, end]
    std::vector<int> spawns;                  // indices into EventsFile::spawned
    std::vector<EventsKey> keys;              // one per event start, or one static key
};

struct EventsClip {
    std::string name;
    float fps = 30.0f;
    int frames = 0;
    std::vector<EventsVolume> damage, judge;
    std::vector<EventsEffect> effects;
};

// A damage objanim a type-29 event spawns (footsteps: `knight_walk_dmg`).
struct EventsSpawned {
    std::string name;
    float fps = 30.0f;
    int frames = 0;
    std::vector<EventsVolume> damage;  // parent Spawn
};

struct EventsFile {
    std::string skeleton;
    std::vector<EventsClip> clips;
    std::vector<EventsVolume> judgeDefaults;  // objanim_<bank>_skelton
    std::vector<EventsSpawned> spawned;
};

// Index of the clip (or spawned objanim) named `name`, or -1.
int events_clip(const EventsFile& file, std::string_view name);
int events_spawned(const EventsFile& file, std::string_view name);

// Reads the sidecar at `path`. Strict: a wrong `format`/`frame`, an unknown shape or
// parent, a damage row without a pose, an effect spawning an objanim the file does
// not carry, or more than 64 damage rows in a clip (EventsClipState::hit) fail with
// `error` naming the row.
bool events_load(const std::string& path, EventsFile& out, std::string& error);

// One frame of a colossus's skeleton. `joint` returns the world matrix of the glb
// joint named (false when absent: posing a row on it throws, never skips silently).
struct PoseFrame {
    Mat4 entity = mat4_identity();  // the colossus's world transform (root + facing)
    float hipY = 0.0f;              // skelton parent: hip height above the entity origin
    std::function<bool(std::string_view, Mat4&)> joint;
};

// The colossus this frame and last frame (the swept test poses each row in both).
struct PoseView {
    int entity = -1;                    // compared with PlayerView::attachedTo/standingOn
    PoseFrame now, prev;
    std::vector<std::string> optBones;  // OptBone_* joint names in glb joint order
};

// A row posed in world space: centre, orthonormal axes (local x, y, z) and size.
struct PosedVolume {
    Vec3 centre{0.0f, 0.0f, 0.0f};
    Vec3 axis[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    Vec3 size{0.0f, 0.0f, 0.0f};
};

// `row` at objanim `frame` (keys lerped between whole frames) in `pose` by the R4
// convention above. A Spawn row is posed with `pose.entity` as the spawn transform.
// Throws std::runtime_error when its joint is not in the pose.
PosedVolume events_pose(const EventsVolume& row, float frame, const PoseFrame& pose,
                        const std::vector<std::string>& optBones);

// The player: the capsule axis [feet, feet + kPlayerCapsuleHeight], radius
// kPlayerCapsuleRadius (collision.h); judge volumes test the `feet` point.
struct PlayerView {
    Vec3 feet{0.0f, 0.0f, 0.0f};
    int attachedTo = -1;  // the entity the player grips (-1 none)
    int standingOn = -1;  // the entity the player stands on (-1 none)
};

// A clip's playback for the event consumer. The caller bumps `instance` (and clears
// `hit`) when the clip loops or restarts; a restart that is not a loop passes the
// previous state with `clip` -1 so no start before `frame` re-fires.
struct EventsClipState {
    int clip = -1;      // index into EventsFile::clips; -1 = none
    float frame = 0.0f; // the objanim's own frame (time · fps)
    int instance = 0;
    uint64_t hit = 0;   // damage rows (bit = row index) that already hit this instance
};

struct DamageHit {
    std::string name;
    int row = -1;    // damage row index (its bit in EventsClipState::hit), or spawned row
    float amount = 0.0f, reaction = 0.0f;
};

// The damage rows of `now.clip` that newly hit the player this frame: rows inside a
// window at `now.frame`, not yet in `now.hit`, posed in `pose.now` and — when the row
// was active at `prev.frame` of the same instance — swept from its `pose.prev` pose
// in sub-steps of ≤ kEventsSweepStep of row travel. Sorted by amount, highest first:
// the caller applies front() (overlapping rows → the highest amount) and marks every
// returned row (`now.hit |= 1 << row`). Rows without a damage_def are not damage the
// app can apply and are skipped. None when the player is attached to or standing on
// this entity (§13.5).
std::vector<DamageHit> events_damage(const EventsFile& file, const EventsClipState& now,
                                     const EventsClipState& prev, const PoseView& pose,
                                     const PlayerView& player);

// Review Focus 4: the slam tip moves ~350 m/s (11.7 m a frame at 30 fps); a row is
// tested every 0.5 m of its travel between the two poses. (authored)
inline constexpr float kEventsSweepStep = 0.5f;

struct EventFire {
    std::string name;
    int clip = -1, row = -1;  // the effect row: file.clips[clip].effects[row]
    Mat4 at = mat4_identity(); // its posed world transform (the spawn transform)
};

// The type-29 events of `now.clip` whose start frame was crossed in (prev.frame,
// now.frame] — wrap-aware: the same clip one instance on reads as a loop wrap, so a
// start fires once across it; a new clip (or prev.clip -1) fires starts in [0, now.frame].
std::vector<EventFire> events_fired(const EventsFile& file, const EventsClipState& now,
                                    const EventsClipState& prev, const PoseView& pose);

// Runs the spawned damage timelines (`spawns`) at their spawn transforms.
class EventsSpawnRunner {
public:
    struct Instance {
        int spawned = -1;          // index into EventsFile::spawned
        Mat4 at = mat4_identity(); // the spawn transform
        float frame = 0.0f;
        uint64_t hit = 0;          // rows that already hit this instance
    };

    // Starts one instance per spawned name of each fired effect.
    void fire(const EventsFile& file, const std::vector<EventFire>& fired);
    // Advances every instance by `dt` seconds; drops the finished ones.
    void step(const EventsFile& file, float dt);
    // The rows that newly hit the player, highest amount first; marks them hit.
    // None when the player is attached to or standing on `entity` (§13.5).
    std::vector<DamageHit> damage(const EventsFile& file, const PlayerView& player, int entity);

    std::vector<Instance> active;
};

// True when the player's feet point is inside any judge volume named `name` — the
// current clip's (`now`) and the skeleton's defaults — posed in `pose.now`.
bool events_player_in_judge(const EventsFile& file, std::string_view name,
                            const EventsClipState& now, const PoseView& pose,
                            const PlayerView& player);
