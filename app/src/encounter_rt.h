#pragma once

// The viewer's stateless runtime helpers, moved out of app.cpp (S10 Task 6a):
// pose/clip playback, Valus's club and corpse, the death and effect frames,
// Valus's attacks and the combat events, the player/entity transforms, the
// collision capsules, the sword grip, and the plain's ride maths.

#include "anim.h"
#include "app.h"
#include "asset_io.h"
#include "audio_events.h"
#include "audio_map.h"
#include "camera.h"
#include "collision.h"
#include "death.h"
#include "effect.h"
#include "grab.h"
#include "heightmap.h"
#include "manifest.h"
#include "player.h"
#include "player_state.h"
#include "renderer.h"
#include "ride.h"
#include "scene.h"
#include "sword.h"
#include "valus.h"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

// Resolves an `--anim` value: an exact clip name, or a 0-based index. -1 when
// nothing matches.
int resolve_clip(const std::vector<AnimationCpu>& clips, const std::string& want);

// The `s2-anim` line reports one rig's selected clip: its name, that rig's own
// joint count (the size of that rig's palette), whether it is the built-in
// procedural clip, and the current time (the loop wrap, or the pinned capture
// time). Printed at startup for every rig and on every player clip change; a
// rig with no clip at all prints nothing. The player's line (Scene::playerRig)
// keeps the S2 shape; every other rig's line carries a `rig=<n> ` prefix so two
// playback clocks are never confused. Real clips print procedural=0; the
// stand-in is only ever the single clip of a model the converter could not
// animate.
void print_anim_status(const Scene& scene, std::size_t rigIndex, const Playback& playback);

// Selects a clip by index, restarts it at t = 0, and reports the change.
void select_clip(Playback& playback, const Scene& scene, std::size_t rigIndex, int index);

// Advances the rig's playback by `dt` and uploads the rig's palette. Called
// once per frame per rig, before the draw; a no-op when the rig has no clip
// (and the upload itself is a no-op for a rig with no skin). The palette is
// returned so a per-frame consumer that needs the pose (the collision pass)
// reads this frame's matrices instead of re-evaluating the clip.
std::vector<Mat4> update_pose(Renderer& renderer, const Scene& scene, std::size_t rigIndex,
                              Playback& playback, float dt);

// S4.2: Valus's club is a separate, unskinned glb authored in the bind frame of
// the entity rig's OptBone_sword. It is appended as static geometry (rig = -1)
// and placed from that joint's posed frame each frame. Since S11 the glb and
// the socket are the encounter's `PropRole::Weapon` row (EncounterDesc).

// S9 (design D5): Wander's sword, an unskinned glb placed every frame on the
// road from the player rig's posed OptBone_sword (recon §3) through the grip
// below. The beam leaves road_points::kSwordTip.
constexpr const char* kSwordGlb = "models/shadow/characters/wander/_cmn/wander_sword.glb";

constexpr const char* kSwordBone = "OptBone_sword";

// The grip. The data attaches the glb with identity (its blade along the bone's
// -Z, like wander_swordcase.glb and Valus's club; wander_sword.cgpr's 3x3 is
// identity) and no clip animates OptBone_sword. The game's hoist pose comes from
// an additive layer the bake does not carry (D8), so the raised blade lies flat
// across the head. The stand-in for that missing layer (source: authored, from
// the stills): a rotation about the bone's X by 90 deg * raise — identity while
// Lowered, Rx(+90) when Raised, where the bone's +Y is vertical (0.99) and the
// blade points up. The sword's model is bone * grip; kSwordTip is a glb point.
Mat4 sword_grip(float raise);

// The raised camera's sideways shift (source: authored, from the stills): the
// follow target moves this far to Wander's left (away from the right-hand blade)
// times the raise, so a beam along the look leaves the tip right of centre and
// runs into the centre instead of hiding behind his head. The look direction, the
// aim and the movement basis are unchanged; camera_follow resets the target.
constexpr float kSwordShoulderM = 0.9f;
// The raised sword's camera step (S10: out of sword_frame, unchanged): the
// shoulder shift of camera.target for `raise` (none at 0). It runs after the
// frame's follow/climb camera and scripted look, right before sword_frame reads
// the eye and the aim — the place it always ran (sword_frame computed only the
// tip, which never reads the camera, before it).
void camera_sword_shoulder(Camera& camera, float raise);

// One sword frame's readings (sword_frame): the tip, the beam, the shade ray and
// the locked ray's hit (NaN = none).
struct SwordProbe {
    Vec3 tip{};
    SwordBeam beam{};
    bool sunlit = false;
    float hit = std::numeric_limits<float>::quiet_NaN();
};

// The S9 runs' scripted look: the camera turned to look along the unit `dir`
// (camera_eye's orbit convention inverted; the target and distance kept).
void camera_look_along(Camera& camera, Vec3 dir);

// A stage's name in the S9 lines.
const char* stage_label(StageId stage);

// Valus's chest on the road's map (the beam's target).
Vec3 valus_chest();

// The corpse swap's assets (S6, design D5/§8.3): the already-converted dead
// body (6 meshes, the six `_dead` materials with the veins veil) and the
// dirtmound it lies on — the encounter's `PropRole::Corpse` and `Ground` rows
// (S11). Both load as static instances at startup, hidden, and appear at
// Collapse at the live entity's transform.

// The club, once loaded into a composed scene: the entity rig it binds to, the
// socket joint resolved by name, and its half-open static mesh range. `loaded`
// stays false in single-model mode, in a manifest scene with no entity, and
// when the entity rig carries no OptBone_sword — in every case the club is
// simply not drawn.
struct ClubAttachment {
    bool loaded = false;
    std::size_t rigIndex = 0;
    std::size_t swordJoint = 0;
    std::size_t meshBegin = 0;
    std::size_t meshEnd = 0;
};

// The slot of the joint named exactly `name` in `rig`, or -1. The club socket
// is resolved by name, never by an index that could mean another bone.
int joint_index_named(const RigCpu& rig, std::string_view name);

// Loads the club into a composed scene and resolves its socket. The entity rig
// and its first mesh must exist (compose_scene guarantees both for an entity);
// a rig without OptBone_sword leaves the club undrawn, with one line naming the
// missing joint rather than a silently wrong-hand prop.
ClubAttachment load_club(Scene& scene, const std::filesystem::path& libraryDir,
                         std::size_t entityRig, const std::string& entityGlb,
                         const EncounterDesc& desc);

// Places the club's meshes at the entity's posed sword joint. The entity's own
// first mesh carries the placement (manifest TRS plus the once-per-run grounding
// lift), so the club reads it instead of keeping a copy that could drift. A
// no-op when the club is not loaded or this frame has no palette for its rig.
void update_club_model(Scene& scene, const std::vector<std::vector<Mat4>>& palettes,
                       const ClubAttachment& club);

// The dead body + dirtmound, loaded as static instances at startup and hidden
// until Collapse (design D5/§8.3). `meshBegin..meshEnd` is their half-open
// static range in the scene; `loaded` stays false in a scene with no entity
// (and outside the death-wired runs, where the corpse is never placed).
struct CorpseAttachment {
    bool loaded = false;
    std::size_t meshBegin = 0;
    std::size_t meshEnd = 0;
};

// Appends the corpse (the dead body then the dirtmound) at the entity's
// placement, hidden. The initial model only has to be sane: while the corpse is
// visible `apply_death_visibility` rewrites it from the live entity's first
// mesh, so it lands exactly where the body fell. A missing glb throws (the same
// policy as the club): the corpse is the death's payoff, not an optional prop.
CorpseAttachment load_corpse(Scene& scene, const std::filesystem::path& libraryDir,
                             std::size_t entityRig, const EncounterDesc& desc);

// The per-mesh draw gate for a half-open range; out-of-range bounds are
// clamped, so a caller never indexes past the scene.
void set_mesh_range_visible(Scene& scene, std::size_t begin, std::size_t end, bool visible);

// Whether every mesh of a half-open range is visible (an empty range is
// vacuously visible). The S6 reset assertion reads the mesh gate itself, not
// just the death flags, so a half-restored swap fails loudly.
bool mesh_range_visible(const Scene& scene, std::size_t begin, std::size_t end);

// The corpse swap (design G3): while `corpseVisible` the live entity's meshes
// (weakpoint shells included) and the held club are hidden and the dead body +
// dirtmound are shown at the entity's current transform; before and after the
// death window the live body is shown and the corpse hidden. Every other mesh
// (the level, the player) is untouched, so an R reset restores the opening
// frame exactly.
void apply_death_visibility(Scene& scene, const CorpseAttachment& corpse,
                            const ClubAttachment& club, std::size_t entityRig,
                            bool corpseVisible);

// The stable phase token the `s6-phase` line prints (the machine never enters
// Tendrils — the burst overlaps Motion — but the name exists for completeness).
const char* death_phase_name(DeathPhase phase);

// The cinematic key the camera is on at death-clock time `t`: the segment's
// destination key (`follow` before the pull and after the return). The
// `s6-death` line reports it.
const char* death_camera_key(float t);

// This frame's tendril roots (design §8.2): the hit weakpoint's shell first,
// the other second — the binary's one-tendril-per-hit-point list, with the
// shells as the fallback for a hit point the app does not track. Each root's
// outward direction is the shell's direction from the body centre (the curl's
// initial heading); `seed` is the root's slot, so the two curls differ. `age`
// is the death clock (the burst begins at 0.4 s); `active` is the machine's
// burst window, so the frame is the renderer's no-op outside it. The chest
// joint, the fallback chest height (without one) and the shells are
// `encounter.desc`'s.
EffectFrame build_effect_frame(const Scene& scene, std::size_t entityRig,
                               const std::vector<std::vector<Mat4>>& palettes,
                               const Encounter& encounter, const DeathState& death);

// The death cue's one-shot latch: true exactly on the first frame the death
// clock has reached the burst start (0.4 s), when `fired` is set. The defeat
// itself queues nothing — the roar is the burst's sound (design §8.4). The test
// is `total >= kDeathTendrilStart`, not `death_tendrils_active`: a long hitch
// can jump the clock past the render window's 3.0 s end, and the roar must still
// fire once (the ribbon window itself stays 0.4-3.0).
bool death_cue_due(const DeathState& death, bool& fired);

// One frame of the death machine and its presentation state (design §8.1/§8.3):
// advance the clock, drive the pinned one-shot death clip from it, and swap the
// corpse at Collapse. The caller owns the camera, the tendril frame and the
// audio (they need this frame's poses and the caller's bookkeeping); the
// interactive loop and the S6 selftest share this, so a reset/replay runs the
// same sequence.
void advance_death_frame(DeathState& death, float dt, Playback& entityPlayback, int deathClip,
                         const CorpseAttachment& corpse, const ClubAttachment& club,
                         Scene& scene, std::size_t entityRig);

// S6: the club whoosh, fired once at the swing phase of an in-progress club
// attack (`kValusClubSwingSeconds`, the impact's lead — not the telegraph).
// The app calls this after `advance_valus_attack`; the frozen S5 timeline does
// not, so its 20-event contract cannot gain a club_swing.
void maybe_club_swing(Encounter& encounter, SfxQueue* sfx);

// Every rig's pose for this frame: the player's (locomotion) and each entity's
// (its own loop, on its own clock), each into its own palette buffer. The
// palettes are returned so this frame's consumers (the entity collision proxy,
// the target camera's chest point) read exactly the matrices the renderer was
// given, not a second evaluation of the clip. The club, when loaded, is placed
// from the entity's fresh palette here: every draw path goes through this one
// function, so no renderer frame can show a stale club.
std::vector<std::vector<Mat4>> update_all_poses(Renderer& renderer, Scene& scene,
                                                std::vector<Playback>& playbacks, float dt,
                                                const ClubAttachment& club);

// Frames the single-model camera on the pose that is actually drawn. With a
// clip active the palette carries the clip's root translation series, which
// holds the ground height and lifts the subject above the bind-pose bounds the
// camera was framed on at load; framing on the bind bounds cuts the posed
// subject at the top (Task 7 §5.2). A scene with no clip keeps the bind bounds.
// Manifest mode never uses this: its follow camera tracks the player instead.
void frame_single_model(Camera& camera, const Scene& scene, const Playback& playback);

// The player's combat clips are the binding's mode mapping (the entity's R3
// reactions, death and attack clips are its EncounterDesc's since S11). Every
// name is resolved once at load; a missing one is a load error, never a silent
// fallback.
constexpr const char* kGripLoopClip = "player_a_grip_loop";

constexpr const char* kGripMoveClip = "player_a_grip_move_f1";

constexpr const char* kGripAttackClip = "player_a_grip_float_u_hi_attack";

constexpr const char* kFallClip = "player_a_damage_m_fly2";

constexpr const char* kHeavyHurtClip = "player_a_damage_h_fly1";

constexpr const char* kHeavyHurtEndClip = "player_a_damage_h_fly_end";

constexpr const char* kPlayerDeathClip = "player_a_fall_damage_to_death";

// The world position of one vertex under a rig's palette: the shader's
// skinning sum (worldJoint * inverseBind per influence), then the mesh's own
// model transform (placement and grounding lift included). A vertex with no
// weights stays at its bind position, like the shader's zero-weight path.
bool posed_vertex_world(const std::vector<Mat4>& palette, const MeshCpu& mesh,
                        const Vertex& vertex, Vec3& out);

// A rig's posed world-space bounds over its own half-open mesh range.
struct RigBounds {
    bool any = false;
    Vec3 min{};
    Vec3 max{};
};

RigBounds posed_rig_bounds(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette);

// The horizontal distance from `p` to a capsule's XZ axis segment; `t` is the
// clamped segment parameter.
float capsule_horizontal_distance(const Capsule& capsule, Vec3 p, Vec3& closest);

bool capsule_overlaps_vertically(const Capsule& capsule, float feet, float head);

// Moves the composed scene's player meshes with the player: the player rig's
// half-open mesh range (compose_scene appends the entities after it, and they
// must not follow the player). `groundOffsetY` is the lift to apply (see
// active_player_lift), so the character stands on the ground point instead of
// sinking into it.
void apply_player_transform(Scene& scene, const RigCpu& rig, const PlayerState& player,
                            float groundOffsetY);

// The lift to draw the composed scene's player meshes with while the active
// clip plays: the S3 origin-to-feet lift, except when the clip carries the
// root translation series — that series already holds the character's ground
// height, so lifting on top of it would float the model by the offset
// (~0.76 m for Wander). A model with no clip at all keeps the plain lift.
float active_player_lift(const Scene& scene, const Playback& playback, const RigCpu& rig);

// Applies every entity's grounding lift to its model transform, once for the
// whole run. compose_scene baked the manifest TRS into each of the entity's
// mesh matrices; the manifest y is the ground sample, so the rig's
// origin-to-feet lift raises the model's origin to `y + lift` — 0 when the
// entity's clip already carries the root series (the S2 rule, shared with the
// player). The player's meshes are not touched here: its transform is rewritten
// from its own lift path each frame. In an encounter the first entity's model is
// immediately replaced by update_entity_transform (ruling R8), which recomposes
// the same product from the stored transform; the lift it uses is the same one
// computed here.
void apply_entity_lifts(Scene& scene, const std::vector<Playback>& playbacks);

// Ruling R8: recompose an entity's mesh models from the transform captured in
// Encounter and the live `entityYawDeg`. compose_scene baked T * R(yaw) * S
// into every mesh, and apply_rig_lift appended the rig-local grounding lift;
// this rebuilds that product with the current yaw, so the whole body — and the
// club, which reads the live first-mesh model — turns. All meshes of one
// entity share its placement model (compose_scene gives them the same TRS), so
// one matrix covers the rig's range. A no-op until the encounter captured a
// transform, and for a rig index the scene does not have.
void update_entity_transform(Scene& scene, const Encounter& encounter,
                             std::size_t rigIndex);

// The entity's chest joint in world space for this frame's palette — the
// target-framing camera's orbit point. False when the rig has no chest joint
// (`desc.joints.chest`; Valus's Bone_MUNE).
bool entity_chest_position(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette, Vec3& out,
                           const EncounterDesc& desc = encounters::kValus);

// Every entity rig's collision proxy for this frame (S4.0 has one entity; the
// loop keeps the composed order general). The player's rig is skipped: its
// capsule is the controller's own, pinned by collision.h.
std::vector<Capsule> entity_capsules(const Scene& scene,
                                     const std::vector<std::vector<Mat4>>& palettes,
                                     const EncounterDesc& desc = encounters::kValus);

// Applies the controller's locomotion choice unless a clip was pinned on the
// command line (--anim or --anim-time): a hard switch — the chosen clip
// restarts at t = 0 — only when the state asks for a different clip. No
// blending, by design.
void update_locomotion(Playback& playback, const Scene& scene, const PlayerInput& input,
                       bool manualClip);

// The modes that hold the colossus; every other mode is the follow-camera's.
bool mode_is_attached(PlayerMode mode);

bool finite_position(Vec3 p);

// The combat clips the player's mode selector names, resolved once at load.
// The heavy hurt pair (`player_a_damage_h_fly1` / `..._h_fly_end`) is not a
// field: the mode selector plays it as one timed pair, and the load gate below
// requires both by name through kHeavyHurtClip / kHeavyHurtEndClip.
struct PlayerClips {
    int gripLoop = -1;
    int gripMove = -1;
    int strike = -1;
    int fall = -1;
    int death = -1;
};

// The entity clips R3 names, resolved once at load from the EncounterDesc
// (`head`/`arm` are weakpoints 0/1's reactions). `stand` is the clip the
// manifest itself named (the entity's own loop).
struct EntityClips {
    int stand = -1;
    int head = -1;
    int arm = -1;
    int body = -1;
    int death = -1;
    int stompL = -1;
    int stompR = -1;
    int club = -1;
};

PlayerClips resolve_player_clips(const Playback& playback);

EntityClips resolve_entity_clips(const Playback& playback, const std::string& standClip,
                                 const EncounterDesc& desc);

// Prints one load error when `name` is absent from `playback`; false means the
// caller must exit 2. The message names the model and the exact clip: the
// fight never silently falls back to the first clip.
bool require_named_clip(const Playback& playback, const std::string& model, const char* name);

// The mode-driven clip choice: Grounded keeps the existing locomotion selector,
// the attached modes play their grip/attack clip, a fall/stagger plays the
// damage clip, Hurt plays its medium/heavy reaction and Dead plays the death
// clip. The death clip is pinned and keeps playing: it runs once and holds its
// last frame instead of wrapping. The pin is cleared as soon as any live mode
// is selected again (the R soft reset). `manualClip` (--anim/--anim-time) keeps
// its S2 override meaning.
void update_combat_clips(Playback& playback, const Scene& scene, const PlayerCombat& combat,
                         const PlayerInput& input, bool manualClip);

// This frame's PlayerFrame: the entity's posed skeleton is the only source of
// climb bands, and its capsules are the ground-mode collision proxy. The
// camera look yaw is the Grounded movement basis (the state machine's
// player_update call), exactly as the interactive path always supplied it.
// The weakpoint lookup resolves a band's shell through posed_weakpoint_proxy on
// this frame's palette; `weakpoint_material_name` answers nullptr for anything
// but the two weakpoints, so it must be checked before building a string.
//
// `entityLive = false` is the S6 corpse state (design §8.3): the dead colossus
// is a static prop, so the frame carries no climb joints, no weakpoint shells
// and no collision capsules — it cannot be grabbed, climbed, struck or walked
// into. The lookups are left empty (player_state/climb_band_segment guard an
// empty lookup) and the frozen timelines pass the default true.
PlayerFrame player_frame_of(const Scene& scene, std::size_t entityRig,
                            const std::vector<std::vector<Mat4>>& palettes,
                            const Heightmap& heightmap, float cameraYawDeg,
                            bool entityLive = true,
                            const EncounterDesc& desc = encounters::kValus);

// Enters the climb orbit on the first attached frame (or on F, `recenter`),
// seeded outward along the band's surface normal; every later attached frame
// just keeps the orbit on the attach point. The player position *is* the
// attach point (the state machine wrote the band surface into it). A band that
// cannot be resolved enters with a zero normal, which keeps the current yaw.
void update_climb_camera(Camera& camera, CameraClimb& climb, const PlayerState& player,
                         const PlayerCombat& combat, const PlayerFrame& frame, float dragDx,
                         float dragDy, float wheelDelta, bool recenter, float dtSeconds);

// Starts one entity one-shot clip (a reaction or the death): pinned so it can
// never loop, with its clock owned by encounter.reactionSeconds (the pose
// update does not advance a non-playing clock). `reactionSeconds == 0` is the
// clip's first frame.
void start_encounter_one_shot(Playback& playback, Encounter& encounter, int clipIndex);

// Advances the encounter's one-shot clip. A reaction returns to the entity's
// own loop when it finishes; a death freezes on its last frame (pinned, not
// playing) and stays defeated. This is the only writer of a one-shot clock, so
// the pose update and the status agree on the drawn time.
void advance_encounter_reaction(Playback& playback, Encounter& encounter, int standClip,
                                float dtSeconds);

// The clip an attack plays, or -1 when the resolved table lacks it. The load
// gate below refuses a fight manifest without all three, so -1 is defensive.
int valus_attack_clip(ValusAttack attack, const EntityClips& clips);

// The posed joint the attack's damage is measured from: the attacking foot for
// a stomp, the club's tip for the swing (design §7.4) — the attack row's.
const char* valus_attack_damage_joint(ValusAttack attack,
                                      const EncounterDesc& desc = encounters::kValus);

// Cuts an in-progress attack short. The cooldown it would have started is
// parked in pendingCooldownSeconds: design §7.4 starts it after the reaction,
// not at the interruption. The caller owns the playback (the reaction clip
// replaces the attack clip in the same step). S5's `ClubCancel` cue is deferred
// to silence, but the transition is still reported.
void cancel_valus_attack(Encounter& encounter, SfxQueue* sfx = nullptr);

// Starts one attack at its first frame. The attack clock is owned by
// encounter.attackSeconds, exactly like the reaction clock: update_pose only
// advances a playing clip, so the playback flag stays false and the app writes
// the sampled time each frame. That keeps the drawn pose and the impact window
// on one clock — a playing flag plus a second clock would drift a frame apart.
void start_valus_attack(Encounter& encounter, Playback& playback, const EntityClips& clips,
                        ValusAttack attack, SfxQueue* sfx = nullptr);

// One frame of Valus's attack state: tick the cooldown, start the selected
// attack, advance the active clip and return to stand when it ends. The impact
// is deliberately not applied here — the caller checks attack_impact_due after
// the player's step, so a strike that lands this frame cancels the attack and
// its pending impact first. A reaction clip owns the body while it plays: no
// new attack starts until it has returned to stand.
void advance_valus_attack(Encounter& encounter, Playback& playback,
                          const EntityClips& clips, const ValusAttackInput& input,
                          float dtSeconds, SfxQueue* sfx = nullptr);

// Applies one attack's damage at the frame its window first opens (the caller
// marks attackHit before calling, so it can never fire twice). The reach is the
// distance from the posed damage joint to the player's capsule segment (feet to
// feet + kPlayerCapsuleHeight), measured by player_capsule_distance — the same
// shape and the same function the weakpoint gate uses. An attached player takes
// no damage (design §7.4); Dead and the i-frame window are player_take_damage's
// own gates.
//
// S5: `sfx`, when given, banks the impact once per call — the world sound fires
// even on a miss, because the club really hits the ground. `events`, when
// given, receives the hurt/death report flags for the same hit.
void apply_valus_impact(PlayerCombat& combat, const PlayerState& player,
                        const PlayerFrame& frame, const CombatTuning& tuning,
                        ValusAttack attack, SfxQueue* sfx = nullptr,
                        StepEvents* events = nullptr,
                        const EncounterDesc& desc = encounters::kValus);

// The per-frame attack decision input: the posed feet from the last palette
// (posed_joint_world_position reads the live mesh model, so a yaw step taken
// earlier this frame is already included), the planar player/entity transform,
// and the mode gate. A foot that cannot resolve is pushed to infinity, which
// reads as out of range rather than as a stomp at the world origin.
ValusAttackInput valus_attack_input(const Scene& scene, std::size_t entityRig,
                                    const std::vector<std::vector<Mat4>>& palettes,
                                    const PlayerState& player, const PlayerCombat& combat,
                                    const Encounter& encounter);

// Applies one step's damage to the encounter and routes the entity's reaction:
// weakpoint 0 (forehead) -> the head reaction, 1 (right arm) -> the arm
// reaction, any other band -> the generic body reaction. A killing blow
// supersedes the reaction and starts the death clip. A defeated entity takes
// no further damage and no second death. A landed strike also cancels Valus's
// in-progress attack and its pending impact (design §7.4); a whiff never has
// damageDealt > 0, so it reaches none of this.
//
// S5/S6: `sfx`, when given, banks Valus's reaction at the entity's world
// position. The defeat itself queues nothing: its roar is the tendril burst's
// sound at 0.4 s, and the app queues it then (S6, design §8.4). The killing
// blow's weakpoint is recorded for the tendril roots.
void apply_combat_events(Encounter& encounter, Playback& entityPlayback,
                         const EntityClips& clips, const StepEvents& events, int weakpoint,
                         SfxQueue* sfx = nullptr);

// The spawn the s3-selftest and the interactive loop start from: the
// manifest's own spawn when it has one, otherwise the composed scene's bounds
// centre.
Vec3 spawn_position(const SceneManifest& manifest, const Scene& scene);

// The `s1-scene` line: what the loaded scene holds (one line for either load
// path — a single glb or a composed manifest).
void print_scene_line(const Scene& scene);

// One rig's clip blend (D7): the pose shown last frame, the outgoing pose a
// switch froze, and the switch detector's memory. A switch is a new clip name,
// or the machine's blend clock restarting (the same clip replayed).
struct RigBlend {
    std::string clip;
    float lastBlend = -1.0f;
    LocalPose shown;
    LocalPose from;
};

// The palette of rig `rigIndex` playing `clipName` at `time` (wrapped into the
// clip), blended from the outgoing pose while `blendFrom` (seconds into the
// 0.25 s blend, -1 = none) runs. Empty when the rig does not carry the clip.
std::vector<Mat4> blended_palette(const Scene& scene, std::size_t rigIndex,
                                  const Playback& playback, RigBlend& blend,
                                  const std::string& clipName, float time, float blendFrom);

// Heading `yawDeg` (0 faces +Z, 90 faces +X — player.h) as a unit XZ vector,
// and its right hand (+X of that frame).
Vec3 yaw_forward(float yawDeg);

Vec3 yaw_right(float yawDeg);

// `p` in the frame of a body at `origin` facing `yawDeg` (+Z forward, +X right).
Vec3 to_body_frame(Vec3 p, Vec3 origin, float yawDeg);

Vec3 from_body_frame(Vec3 local, Vec3 origin, float yawDeg);

// A pure translation (the rider's seat-relative offset).
Mat4 translation_of(Vec3 t);

// `p` in the local frame of `m` (a rotation with a uniform scale, plus a
// translation): mat4 * point inverted without a general inverse.
Vec3 point_in_frame(const Mat4& m, Vec3 p);

// Every mesh of `rig` drawn with `model` (the rider: a full matrix, no lift).
void set_rig_model(Scene& scene, const RigCpu& rig, const Mat4& model);

// The hoof cue and touchdown fractions (design §6, measured) of an Agro gait
// loop; null for every other clip (the start/stop clips carry no hooves).
const float* footfall_fractions(const std::string& clip, RideSfx& cue);

// How many touchdowns of `clip` (duration `duration`) fall in (prev, now] of
// its clock; a repeated fraction (the trot's two hooves at 0.0) sounds once.
int footfalls_crossed(const float* fractions, float duration, float prev, float now);
