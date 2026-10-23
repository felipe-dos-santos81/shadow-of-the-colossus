#include "encounter_rt.h"

#include "road_points.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

int resolve_clip(const std::vector<AnimationCpu>& clips, const std::string& want) {
    char* end = nullptr;
    const long index = std::strtol(want.c_str(), &end, 10);
    if (!want.empty() && end != want.c_str() && *end == '\0') {
        return (index >= 0 && static_cast<size_t>(index) < clips.size()) ? static_cast<int>(index)
                                                                         : -1;
    }
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == want) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void print_anim_status(const Scene& scene, std::size_t rigIndex, const Playback& playback) {
    if (playback.index < 0) {
        return;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const bool procedural = playback.has_procedural && playback.index == 0;
    const std::size_t joints = rig_at(scene, rigIndex).skin.joints.size();
    const float time = playback_time(playback, clip.duration);
    if (rigIndex == scene.playerRig) {
        std::printf("s2-anim clip=%s joints=%zu procedural=%d t=%.3f\n", clip.name.c_str(),
                    joints, procedural ? 1 : 0, time);
    } else {
        std::printf("s2-anim rig=%zu clip=%s joints=%zu procedural=%d t=%.3f\n", rigIndex,
                    clip.name.c_str(), joints, procedural ? 1 : 0, time);
    }
}

void select_clip(Playback& playback, const Scene& scene, std::size_t rigIndex, int index) {
    playback.index = index;
    playback.time = 0.0f;
    print_anim_status(scene, rigIndex, playback);
}

std::vector<Mat4> update_pose(Renderer& renderer, const Scene& scene, std::size_t rigIndex,
                              Playback& playback, float dt) {
    if (playback.index < 0) {
        return {};
    }
    if (playback.playing) {
        playback.time += dt;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const float time = playback_time(playback, clip.duration);
    std::vector<Mat4> palette =
        pose_palette(scene, rigIndex, animation_local_pose(scene, rigIndex, clip, time));
    renderer.upload_pose(rigIndex, palette);
    return palette;
}

void camera_sword_shoulder(Camera& camera, float raise) {
    if (raise > 0.0f) {
        const float look = camera_look_yaw(camera) * kDegToRad;
        const float shift = kSwordShoulderM * raise;
        camera.target.x += std::cos(look) * shift;  // left = (cos, 0, -sin)
        camera.target.z -= std::sin(look) * shift;
    }
}

Mat4 sword_grip(float raise) {
    const float half = 0.5f * 90.0f * kDegToRad * std::clamp(raise, 0.0f, 1.0f);
    return mat4_from_trs(Vec3{0.0f, 0.0f, 0.0f}, Quat{std::sin(half), 0.0f, 0.0f, std::cos(half)},
                         Vec3{1.0f, 1.0f, 1.0f});
}

void camera_look_along(Camera& camera, Vec3 dir) {
    camera.yaw = std::atan2(-dir.x, -dir.z) / kDegToRad;
    camera.pitch = std::asin(std::clamp(-dir.y, -1.0f, 1.0f)) / kDegToRad;
}

const char* stage_label(StageId stage) {
    return stage == StageId::Shrine ? "shrine" : stage == StageId::Plain ? "plain" : "arena";
}

Vec3 valus_chest() {
    return Vec3{road_points::kValusFeet[0],
                road_points::kValusFeet[1] + road_points::kValusChestLift,
                road_points::kValusFeet[2]};
}

int joint_index_named(const RigCpu& rig, std::string_view name) {
    for (std::size_t i = 0; i < rig.skin.joints.size(); ++i) {
        if (rig.skin.joints[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ClubAttachment load_club(Scene& scene, const std::filesystem::path& libraryDir,
                         std::size_t entityRig, const std::string& entityGlb,
                         const EncounterDesc& desc) {
    ClubAttachment club{};
    const PropDesc* weapon = encounter_prop(desc, PropRole::Weapon);
    if (weapon == nullptr || entityRig >= scene.rigs.size()) {
        return club;  // an unarmed colossus has no club to draw
    }
    const RigCpu& rig = scene.rigs[entityRig];
    const int socket = joint_index_named(rig, weapon->socketJoint);
    if (socket < 0) {
        std::fprintf(stderr, "sotc-viewer: %s has no %s joint; the club is not drawn\n",
                     entityGlb.c_str(), weapon->socketJoint);
        return club;
    }
    if (rig.meshBegin >= scene.meshes.size()) {
        std::fprintf(stderr,
                     "sotc-viewer: the entity rig has no meshes; the club is not drawn\n");
        return club;
    }

    // Static geometry at the entity's placement; each frame its model is
    // rewritten from the posed socket (update_club_model).
    club.meshBegin = append_static_model(scene, libraryDir, weapon->glb,
                                         scene.meshes[rig.meshBegin].model);
    club.meshEnd = scene.meshes.size();
    club.loaded = true;
    club.rigIndex = entityRig;
    club.swordJoint = static_cast<std::size_t>(socket);
    return club;
}

void update_club_model(Scene& scene, const std::vector<std::vector<Mat4>>& palettes,
                       const ClubAttachment& club) {
    if (!club.loaded || club.rigIndex >= palettes.size() ||
        club.rigIndex >= scene.rigs.size()) {
        return;
    }
    const RigCpu& rig = scene.rigs[club.rigIndex];
    if (rig.meshBegin >= scene.meshes.size() || club.meshBegin >= scene.meshes.size()) {
        return;
    }
    const Mat4 model =
        club_model(scene, palettes[club.rigIndex], club.swordJoint,
                   scene.meshes[rig.meshBegin].model);
    const std::size_t end = std::min(club.meshEnd, scene.meshes.size());
    for (std::size_t i = club.meshBegin; i < end; ++i) {
        scene.meshes[i].model = model;
    }
}

CorpseAttachment load_corpse(Scene& scene, const std::filesystem::path& libraryDir,
                             std::size_t entityRig, const EncounterDesc& desc) {
    CorpseAttachment corpse{};
    const PropDesc* body = encounter_prop(desc, PropRole::Corpse);
    const PropDesc* ground = encounter_prop(desc, PropRole::Ground);
    if (body == nullptr || entityRig >= scene.rigs.size()) {
        return corpse;
    }
    const RigCpu& rig = scene.rigs[entityRig];
    if (rig.meshBegin >= scene.meshes.size()) {
        return corpse;
    }
    const Mat4 model = scene.meshes[rig.meshBegin].model;
    corpse.meshBegin = append_static_model(scene, libraryDir, body->glb, model);
    if (ground != nullptr) {
        append_static_model(scene, libraryDir, ground->glb, model);
    }
    corpse.meshEnd = scene.meshes.size();
    for (std::size_t i = corpse.meshBegin; i < corpse.meshEnd; ++i) {
        scene.meshes[i].visible = false;
    }
    corpse.loaded = true;
    return corpse;
}

void set_mesh_range_visible(Scene& scene, std::size_t begin, std::size_t end, bool visible) {
    const std::size_t last = std::min(end, scene.meshes.size());
    for (std::size_t i = std::min(begin, last); i < last; ++i) {
        scene.meshes[i].visible = visible;
    }
}

bool mesh_range_visible(const Scene& scene, std::size_t begin, std::size_t end) {
    const std::size_t last = std::min(end, scene.meshes.size());
    for (std::size_t i = std::min(begin, last); i < last; ++i) {
        if (!scene.meshes[i].visible) {
            return false;
        }
    }
    return true;
}

void apply_death_visibility(Scene& scene, const CorpseAttachment& corpse,
                            const ClubAttachment& club, std::size_t entityRig,
                            bool corpseVisible) {
    if (entityRig < scene.rigs.size()) {
        const RigCpu& rig = scene.rigs[entityRig];
        set_mesh_range_visible(scene, rig.meshBegin, rig.meshEnd, !corpseVisible);
    }
    if (club.loaded) {
        set_mesh_range_visible(scene, club.meshBegin, club.meshEnd, !corpseVisible);
    }
    if (!corpse.loaded) {
        return;
    }
    set_mesh_range_visible(scene, corpse.meshBegin, corpse.meshEnd, corpseVisible);
    if (!corpseVisible || entityRig >= scene.rigs.size()) {
        return;
    }
    // The corpse follows the body that fell: the live entity's first mesh
    // carries the placement (manifest TRS * the grounding lift * the live yaw),
    // so the corpse lands where Valus fell, not at the manifest's opening yaw.
    const RigCpu& rig = scene.rigs[entityRig];
    if (rig.meshBegin >= scene.meshes.size()) {
        return;
    }
    const Mat4 model = scene.meshes[rig.meshBegin].model;
    const std::size_t end = std::min(corpse.meshEnd, scene.meshes.size());
    for (std::size_t i = std::min(corpse.meshBegin, end); i < end; ++i) {
        scene.meshes[i].model = model;
    }
}

const char* death_phase_name(DeathPhase phase) {
    switch (phase) {
        case DeathPhase::Idle:
            return "Idle";
        case DeathPhase::Motion:
            return "Motion";
        case DeathPhase::Tendrils:
            return "Tendrils";
        case DeathPhase::Collapse:
            return "Collapse";
        case DeathPhase::Done:
            return "Done";
    }
    return "Idle";
}

const char* death_camera_key(float t) {
    if (!std::isfinite(t) || t <= 0.0f) {
        return "follow";
    }
    if (t <= kDeathPullSeconds) {
        return "pull";
    }
    if (t <= kDeathLowSeconds) {
        return "low";
    }
    if (t <= kDeathWideSeconds) {
        return "wide";
    }
    return "follow";
}

EffectFrame build_effect_frame(const Scene& scene, std::size_t entityRig,
                               const std::vector<std::vector<Mat4>>& palettes,
                               const Encounter& encounter, const DeathState& death) {
    EffectFrame frame;
    frame.age = death.total;
    frame.active = death_tendrils_active(death);
    if (!frame.active || entityRig >= palettes.size()) {
        return frame;
    }

    const EncounterDesc& desc = *encounter.desc;
    Vec3 centre{encounter.entityPosition.x,
                encounter.entityPosition.y + desc.death.fallbackChestHeight,
                encounter.entityPosition.z};
    Vec3 chest{};
    if (posed_joint_world_position(scene, entityRig, palettes[entityRig], desc.joints.chest,
                                   chest) &&
        std::isfinite(chest.x) && std::isfinite(chest.y) && std::isfinite(chest.z)) {
        centre = chest;
    }

    const int first = encounter.deathWeakpoint == 1 ? 1 : 0;
    const int order[2] = {first, 1 - first};
    for (const int weakpoint : order) {
        if (frame.rootCount >= static_cast<int>(frame.roots.size())) {
            break;
        }
        const char* material = weakpoint_material_name(desc.weakpoints, weakpoint);
        if (material == nullptr) {
            continue;
        }
        Vec3 shell{};
        float radius = 0.0f;
        if (!posed_weakpoint_proxy(scene, entityRig, palettes[entityRig], material, shell,
                                   radius) ||
            !std::isfinite(shell.x) || !std::isfinite(shell.y) || !std::isfinite(shell.z)) {
            continue;
        }
        Vec3 normal{shell.x - centre.x, shell.y - centre.y, shell.z - centre.z};
        const float length = std::sqrt(vec3_dot(normal, normal));
        if (length > 1e-4f) {
            normal = Vec3{normal.x / length, normal.y / length, normal.z / length};
        } else {
            normal = Vec3{0.0f, 1.0f, 0.0f};
        }
        EffectRoot& root = frame.roots[static_cast<std::size_t>(frame.rootCount)];
        root.position = shell;
        root.normal = normal;
        root.seed = static_cast<std::uint32_t>(frame.rootCount);
        ++frame.rootCount;
    }
    return frame;
}

bool death_cue_due(const DeathState& death, bool& fired) {
    if (fired || death.phase == DeathPhase::Idle) {
        return false;
    }
    if (!(death.total >= kDeathTendrilStart)) {
        return false;
    }
    fired = true;
    return true;
}

void advance_death_frame(DeathState& death, float dt, Playback& entityPlayback, int deathClip,
                         const CorpseAttachment& corpse, const ClubAttachment& club,
                         Scene& scene, std::size_t entityRig) {
    death = step_death(death, dt, true);
    if (deathClip >= 0 && entityPlayback.index == deathClip) {
        entityPlayback.time = std::min(death.total, kMotionSeconds);
        entityPlayback.pinned = true;
        entityPlayback.playing = false;
    }
    apply_death_visibility(scene, corpse, club, entityRig, death_corpse_visible(death));
}

void maybe_club_swing(Encounter& encounter, SfxQueue* sfx) {
    if (encounter.attack != ValusAttack::Club || encounter.attackSwinged) {
        return;
    }
    const AttackDesc* club = valus_attack_desc(ValusAttack::Club, *encounter.desc);
    if (club == nullptr || !(encounter.attackSeconds >= club->swingSeconds)) {
        return;  // no club row (another colossus), or not yet at the swing
    }
    encounter.attackSwinged = true;
    if (sfx != nullptr) {
        append_encounter_sfx(EncounterSfx::ClubSwing, encounter.entityPosition, *sfx);
    }
}

std::vector<std::vector<Mat4>> update_all_poses(Renderer& renderer, Scene& scene,
                                                std::vector<Playback>& playbacks, float dt,
                                                const ClubAttachment& club) {
    std::vector<std::vector<Mat4>> palettes(playbacks.size());
    for (std::size_t rigIndex = 0; rigIndex < playbacks.size(); ++rigIndex) {
        palettes[rigIndex] = update_pose(renderer, scene, rigIndex, playbacks[rigIndex], dt);
    }
    update_club_model(scene, palettes, club);
    return palettes;
}

void frame_single_model(Camera& camera, const Scene& scene, const Playback& playback) {
    if (playback.index < 0) {
        camera_frame(camera, scene.boundsMin, scene.boundsMax);
        return;
    }
    const AnimationCpu& clip = playback.clips[static_cast<size_t>(playback.index)];
    const float time = playback_time(playback, clip.duration);
    Vec3 bounds_min{};
    Vec3 bounds_max{};
    posed_bounds(scene, animation_pose(scene, scene.playerRig, clip, time), bounds_min, bounds_max);

    // camera_frame fits the bounds' half-diagonal with distance = r / tan(fov/2),
    // which leaves the bounding sphere tangent to the frustum only at
    // r / sin(fov/2). Pad the posed box by 1 / cos(fov/2) about its centre so the
    // subject actually fits — a tall, narrow figure (Wander is 1.25 m tall,
    // 0.45 wide) otherwise puts the top of the head on the top edge.
    const Vec3 centre{(bounds_min.x + bounds_max.x) * 0.5f,
                      (bounds_min.y + bounds_max.y) * 0.5f,
                      (bounds_min.z + bounds_max.z) * 0.5f};
    const float pad = 1.0f / std::cos(0.5f * camera.fovYDeg * kDegToRad);
    bounds_min = Vec3{centre.x + (bounds_min.x - centre.x) * pad,
                      centre.y + (bounds_min.y - centre.y) * pad,
                      centre.z + (bounds_min.z - centre.z) * pad};
    bounds_max = Vec3{centre.x + (bounds_max.x - centre.x) * pad,
                      centre.y + (bounds_max.y - centre.y) * pad,
                      centre.z + (bounds_max.z - centre.z) * pad};
    camera_frame(camera, bounds_min, bounds_max);
}

bool posed_vertex_world(const std::vector<Mat4>& palette, const MeshCpu& mesh,
                        const Vertex& vertex, Vec3& out) {
    const Vec3 bind{vertex.px, vertex.py, vertex.pz};
    Vec3 point = bind;
    float total_weight = 0.0f;
    Vec3 posed{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        const float weight = vertex.w[i];
        const std::size_t joint = vertex.j[i];
        if (weight == 0.0f || joint >= palette.size()) {
            continue;
        }
        const Mat4& skin = palette[joint];
        posed.x += weight * (skin.m[0] * bind.x + skin.m[4] * bind.y + skin.m[8] * bind.z +
                             skin.m[12]);
        posed.y += weight * (skin.m[1] * bind.x + skin.m[5] * bind.y + skin.m[9] * bind.z +
                             skin.m[13]);
        posed.z += weight * (skin.m[2] * bind.x + skin.m[6] * bind.y + skin.m[10] * bind.z +
                             skin.m[14]);
        total_weight += weight;
    }
    if (total_weight > 0.0f) {
        point = posed;
    }
    const Mat4& model = mesh.model;
    out = Vec3{model.m[0] * point.x + model.m[4] * point.y + model.m[8] * point.z + model.m[12],
               model.m[1] * point.x + model.m[5] * point.y + model.m[9] * point.z + model.m[13],
               model.m[2] * point.x + model.m[6] * point.y + model.m[10] * point.z + model.m[14]};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

RigBounds posed_rig_bounds(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette) {
    RigBounds bounds;
    const RigCpu& rig = rig_at(scene, rigIndex);
    const std::size_t end = std::min(rig.meshEnd, scene.meshes.size());
    for (std::size_t i = std::min(rig.meshBegin, end); i < end; ++i) {
        for (const Vertex& vertex : scene.meshes[i].vertices) {
            Vec3 world{};
            if (!posed_vertex_world(palette, scene.meshes[i], vertex, world)) {
                continue;
            }
            if (!bounds.any) {
                bounds.min = bounds.max = world;
                bounds.any = true;
                continue;
            }
            bounds.min.x = std::min(bounds.min.x, world.x);
            bounds.min.y = std::min(bounds.min.y, world.y);
            bounds.min.z = std::min(bounds.min.z, world.z);
            bounds.max.x = std::max(bounds.max.x, world.x);
            bounds.max.y = std::max(bounds.max.y, world.y);
            bounds.max.z = std::max(bounds.max.z, world.z);
        }
    }
    return bounds;
}

float capsule_horizontal_distance(const Capsule& capsule, Vec3 p, Vec3& closest) {
    const float dx = capsule.b.x - capsule.a.x;
    const float dz = capsule.b.z - capsule.a.z;
    const float length_squared = dx * dx + dz * dz;
    float t = 0.0f;
    if (length_squared > 0.0f) {
        t = ((p.x - capsule.a.x) * dx + (p.z - capsule.a.z) * dz) / length_squared;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    closest = Vec3{capsule.a.x + dx * t, 0.0f, capsule.a.z + dz * t};
    const float ox = p.x - closest.x;
    const float oz = p.z - closest.z;
    return std::sqrt(ox * ox + oz * oz);
}

bool capsule_overlaps_vertically(const Capsule& capsule, float feet, float head) {
    const float radius = std::max(capsule.r, 0.0f);
    const float low = std::min(capsule.a.y, capsule.b.y) - radius;
    const float high = std::max(capsule.a.y, capsule.b.y) + radius;
    return !(high < feet || low > head);
}

void apply_player_transform(Scene& scene, const RigCpu& rig, const PlayerState& player,
                            float groundOffsetY) {
    const Mat4 model = player_model_matrix(player, groundOffsetY);
    for (size_t i = rig.meshBegin; i < rig.meshEnd && i < scene.meshes.size(); ++i) {
        scene.meshes[i].model = model;
    }
}

float active_player_lift(const Scene& scene, const Playback& playback, const RigCpu& rig) {
    if (playback.index < 0) {
        return rig_ground_offset(scene, rig);
    }
    return effective_player_lift(scene, playback.clips[static_cast<size_t>(playback.index)],
                                 rig.meshBegin, rig.meshEnd);
}

void apply_entity_lifts(Scene& scene, const std::vector<Playback>& playbacks) {
    for (std::size_t rigIndex = 0; rigIndex < scene.rigs.size(); ++rigIndex) {
        if (rigIndex == scene.playerRig) {
            continue;
        }
        float lift = rig_ground_offset(scene, scene.rigs[rigIndex]);
        if (rigIndex < playbacks.size() && playbacks[rigIndex].index >= 0) {
            const Playback& playback = playbacks[rigIndex];
            lift = effective_rig_lift(scene, rigIndex,
                                      playback.clips[static_cast<size_t>(playback.index)]);
        }
        apply_rig_lift(scene, rigIndex, lift);
    }
}

void update_entity_transform(Scene& scene, const Encounter& encounter,
                             std::size_t rigIndex) {
    if (!encounter.entityTransformSet || rigIndex >= scene.rigs.size()) {
        return;
    }
    const RigCpu& rig = scene.rigs[rigIndex];
    const std::size_t end = std::min(rig.meshEnd, scene.meshes.size());
    const float half_angle = 0.5f * encounter.entityYawDeg * kDegToRad;
    const Quat rotation{0.0f, std::sin(half_angle), 0.0f, std::cos(half_angle)};
    const Mat4 base =
        mat4_from_trs(encounter.entityPosition, rotation, encounter.entityScale);
    const Mat4 lift =
        mat4_from_trs(Vec3{0.0f, encounter.entityLift, 0.0f},
                      Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    const Mat4 model = mat4_mul(base, lift);
    for (std::size_t i = std::min(rig.meshBegin, end); i < end; ++i) {
        scene.meshes[i].model = model;
    }
}

bool entity_chest_position(const Scene& scene, std::size_t rigIndex,
                           const std::vector<Mat4>& palette, Vec3& out,
                           const EncounterDesc& desc) {
    return posed_joint_world_position(scene, rigIndex, palette, desc.joints.chest, out);
}

std::vector<Capsule> entity_capsules(const Scene& scene,
                                     const std::vector<std::vector<Mat4>>& palettes,
                                     const EncounterDesc& desc) {
    std::vector<Capsule> all;
    for (std::size_t rigIndex = scene.playerRig + 1; rigIndex < scene.rigs.size(); ++rigIndex) {
        if (rigIndex >= palettes.size()) {
            break;
        }
        if (static_cast<int>(rigIndex) == scene.horseRig) {
            continue;  // S8 (D4/D17): Agro is never an encounter entity
        }
        std::vector<Capsule> capsules =
            entity_collision_capsules(scene, rigIndex, palettes[rigIndex], desc.capsules);
        all.insert(all.end(), capsules.begin(), capsules.end());
    }
    return all;
}

void update_locomotion(Playback& playback, const Scene& scene, const PlayerInput& input,
                       bool manualClip) {
    if (manualClip) {
        return;
    }
    const int wanted = select_clip_index(playback, input);
    if (wanted >= 0 && wanted != playback.index) {
        select_clip(playback, scene, scene.playerRig, wanted);
    }
}

bool mode_is_attached(PlayerMode mode) {
    return mode == PlayerMode::Grabbing || mode == PlayerMode::Climbing ||
           mode == PlayerMode::Striking;
}

bool finite_position(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

PlayerClips resolve_player_clips(const Playback& playback) {
    return PlayerClips{clip_index_named(playback, kGripLoopClip),
                       clip_index_named(playback, kGripMoveClip),
                       clip_index_named(playback, kGripAttackClip),
                       clip_index_named(playback, kFallClip),
                       clip_index_named(playback, kPlayerDeathClip)};
}

EntityClips resolve_entity_clips(const Playback& playback, const std::string& standClip,
                                 const EncounterDesc& desc) {
    const auto named = [&playback](const char* name) {
        return name != nullptr ? clip_index_named(playback, name) : -1;
    };
    const auto reaction = [&desc, &named](std::size_t weakpoint) {
        return weakpoint < desc.weakpoints.size() ? named(desc.weakpoints[weakpoint].reactionClip)
                                                  : -1;
    };
    const auto attack = [&desc, &named](ValusAttack a) {
        const AttackDesc* row = valus_attack_desc(a, desc);
        return row != nullptr ? named(row->clip) : -1;
    };
    return EntityClips{clip_index_named(playback, standClip),
                       reaction(0),
                       reaction(1),
                       named(desc.bodyReactionClip),
                       named(desc.death.clip),
                       attack(ValusAttack::StompL),
                       attack(ValusAttack::StompR),
                       attack(ValusAttack::Club)};
}

bool require_named_clip(const Playback& playback, const std::string& model, const char* name) {
    if (clip_index_named(playback, name) >= 0) {
        return true;
    }
    std::fprintf(stderr, "sotc-viewer: %s carries no %s clip (the fight needs it)\n",
                 model.c_str(), name);
    return false;
}

void update_combat_clips(Playback& playback, const Scene& scene, const PlayerCombat& combat,
                         const PlayerInput& input, bool manualClip) {
    if (manualClip) {
        return;
    }
    const int wanted = select_mode_clip_index(playback, combat, input);
    if (wanted >= 0 && wanted != playback.index) {
        select_clip(playback, scene, scene.playerRig, wanted);
    }
    if (combat.mode == PlayerMode::Dead) {
        // Play once, clamp on the last frame; the pose update advances this
        // clock because it is playing, unlike the entity's reaction clock.
        playback.pinned = true;
        playback.playing = true;
    } else {
        playback.pinned = false;
    }
    // wanted < 0 means the named clip is absent. The load already refused that
    // manifest (require_named_clip), so there is deliberately no fallback here.
}

PlayerFrame player_frame_of(const Scene& scene, std::size_t entityRig,
                            const std::vector<std::vector<Mat4>>& palettes,
                            const Heightmap& heightmap, float cameraYawDeg,
                            const EncounterDesc& desc, bool entityLive) {
    PlayerFrame frame;
    frame.bands = desc.bands;
    frame.scene = &scene;
    frame.heightmap = &heightmap;
    frame.entityRig = entityRig;
    frame.cameraYawDeg = cameraYawDeg;
    if (entityLive && entityRig < palettes.size()) {
        frame.jointWorld = [&scene, entityRig, &palettes](std::string_view name, Vec3& out) {
            return posed_joint_world_position(scene, entityRig, palettes[entityRig],
                                              std::string(name), out);
        };
        frame.weakpointProxy = [&scene, entityRig, &palettes, weakpoints = desc.weakpoints](
                                   int weakpoint, Vec3& center, float& radius) {
            const char* material = weakpoint_material_name(weakpoints, weakpoint);
            if (material == nullptr) {
                return false;
            }
            return posed_weakpoint_proxy(scene, entityRig, palettes[entityRig],
                                         std::string(material), center, radius);
        };
    }
    if (entityLive) {
        frame.entityCapsules = entity_capsules(scene, palettes, desc);
    }
    return frame;
}

void start_encounter_one_shot(Playback& playback, Encounter& encounter, int clipIndex) {
    if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= playback.clips.size()) {
        return;
    }
    playback.index = clipIndex;
    playback.time = 0.0f;
    playback.pinned = true;    // clamp at the last frame, never wrap
    playback.playing = false;  // encounter.reactionSeconds advances this clock
    encounter.reactionClip = clipIndex;
    encounter.reactionSeconds = 0.0f;
}

void advance_encounter_reaction(Playback& playback, Encounter& encounter, int standClip,
                                float dtSeconds) {
    if (encounter.reactionClip < 0) {
        return;
    }
    if (!(dtSeconds > 0.0f) || !std::isfinite(dtSeconds)) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(encounter.reactionClip);
    if (index >= playback.clips.size()) {
        encounter.reactionClip = -1;
        encounter.reactionSeconds = 0.0f;
        return;
    }
    const float duration = playback.clips[index].duration;
    encounter.reactionSeconds += dtSeconds;
    if (encounter.reactionSeconds < duration) {
        playback.time = encounter.reactionSeconds;
        return;
    }
    if (encounter.defeated) {
        // Death: play once, then hold the final frame. No cooldown survives a
        // defeat: there is no next attack.
        playback.time = duration;
        playback.pinned = true;
        playback.playing = false;
        encounter.reactionClip = -1;
        encounter.reactionSeconds = duration;
        encounter.pendingCooldownSeconds = 0.0f;
        return;
    }
    // Reaction over: back to the entity's own loop from its first frame.
    if (standClip >= 0) {
        playback.index = standClip;
        playback.time = 0.0f;
        playback.pinned = false;
        playback.playing = true;
    }
    // An attack this reaction interrupted starts its cooldown now, after the
    // reaction, not when the strike cut the clip short (design §7.4).
    if (encounter.pendingCooldownSeconds > 0.0f) {
        encounter.cooldownSeconds = encounter.pendingCooldownSeconds;
        encounter.pendingCooldownSeconds = 0.0f;
    }
    encounter.reactionClip = -1;
    encounter.reactionSeconds = 0.0f;
}

int valus_attack_clip(ValusAttack attack, const EntityClips& clips) {
    switch (attack) {
        case ValusAttack::StompL:
            return clips.stompL;
        case ValusAttack::StompR:
            return clips.stompR;
        case ValusAttack::Club:
            return clips.club;
        case ValusAttack::None:
            break;
    }
    return -1;
}

const char* valus_attack_damage_joint(ValusAttack attack, const EncounterDesc& desc) {
    const AttackDesc* row = valus_attack_desc(attack, desc);
    return row != nullptr ? row->damageJoint : nullptr;
}

void cancel_valus_attack(Encounter& encounter, SfxQueue* sfx) {
    if (encounter.attack == ValusAttack::None) {
        return;
    }
    encounter.pendingCooldownSeconds = valus_attack_cooldown_seconds(encounter.attack);
    encounter.attack = ValusAttack::None;
    encounter.attackSeconds = 0.0f;
    encounter.attackHit = false;
    encounter.attackSwinged = false;
    if (sfx != nullptr) {
        append_encounter_sfx(EncounterSfx::ClubCancel, encounter.entityPosition, *sfx);
    }
}

void start_valus_attack(Encounter& encounter, Playback& playback, const EntityClips& clips,
                        ValusAttack attack, SfxQueue* sfx) {
    const int clip = valus_attack_clip(attack, clips);
    if (clip < 0 || static_cast<std::size_t>(clip) >= playback.clips.size()) {
        return;
    }
    encounter.attack = attack;
    encounter.attackSeconds = 0.0f;
    encounter.attackHit = false;
    encounter.attackSwinged = false;
    playback.index = clip;
    playback.time = 0.0f;
    playback.pinned = false;
    playback.playing = false;  // encounter.attackSeconds advances this clip
    if (sfx != nullptr) {
        // The first frame of the clip is the telegraph: the pose that warns is
        // also the sound that warns.
        append_encounter_sfx(attack == ValusAttack::Club ? EncounterSfx::ClubStart
                                                         : EncounterSfx::StompStart,
                             encounter.entityPosition, *sfx);
    }
}

void advance_valus_attack(Encounter& encounter, Playback& playback,
                          const EntityClips& clips, const ValusAttackInput& input,
                          float dtSeconds, SfxQueue* sfx) {
    if (encounter.defeated) {
        // Defeat drops a pending swing without a cooldown: there is no next
        // attack, and the death clip owns the playback.
        encounter.attack = ValusAttack::None;
        encounter.attackSeconds = 0.0f;
        encounter.attackHit = false;
        encounter.attackSwinged = false;
        encounter.pendingCooldownSeconds = 0.0f;
        return;
    }

    if (std::isfinite(dtSeconds) && dtSeconds > 0.0f) {
        encounter.cooldownSeconds = std::max(0.0f, encounter.cooldownSeconds - dtSeconds);
    } else if (!std::isfinite(encounter.cooldownSeconds)) {
        encounter.cooldownSeconds = 0.0f;
    }

    if (encounter.attack == ValusAttack::None) {
        if (encounter.reactionClip >= 0) {
            return;
        }
        const ValusAttack chosen = select_valus_attack(input);
        if (chosen == ValusAttack::None) {
            return;
        }
        const int clip = valus_attack_clip(chosen, clips);
        if (clip < 0 || static_cast<std::size_t>(clip) >= playback.clips.size() ||
            !(playback.clips[static_cast<std::size_t>(clip)].duration > 0.0f)) {
            return;  // nothing to play: never start a zero-length telegraph
        }
        start_valus_attack(encounter, playback, clips, chosen, sfx);
        if (chosen == ValusAttack::StompL || chosen == ValusAttack::StompR) {
            encounter.stompLeftNext = chosen == ValusAttack::StompR;
        }
        return;  // t = 0 is outside both impact windows
    }

    const int clip = valus_attack_clip(encounter.attack, clips);
    if (clip < 0 || static_cast<std::size_t>(clip) >= playback.clips.size()) {
        encounter.attack = ValusAttack::None;
        encounter.attackSeconds = 0.0f;
        encounter.attackHit = false;
        encounter.attackSwinged = false;
        return;
    }
    const float duration = playback.clips[static_cast<std::size_t>(clip)].duration;
    if (std::isfinite(dtSeconds) && dtSeconds > 0.0f) {
        encounter.attackSeconds += dtSeconds;
    }
    if (encounter.attackSeconds >= duration) {
        // Clip over: back to the entity's own loop, then the cooldown starts.
        encounter.cooldownSeconds = valus_attack_cooldown_seconds(encounter.attack);
        encounter.attack = ValusAttack::None;
        encounter.attackSeconds = 0.0f;
        encounter.attackHit = false;
        encounter.attackSwinged = false;
        if (clips.stand >= 0) {
            playback.index = clips.stand;
            playback.time = 0.0f;
            playback.pinned = false;
            playback.playing = true;
        }
        return;
    }
    playback.time = encounter.attackSeconds;  // the drawn pose and the window agree
}

void apply_valus_impact(PlayerCombat& combat, const PlayerState& player,
                        const PlayerFrame& frame, const CombatTuning& tuning,
                        ValusAttack attack, const EncounterDesc& desc,
                        SfxQueue* sfx, StepEvents* events) {
    if (attack == ValusAttack::None) {
        return;
    }
    const char* joint = valus_attack_damage_joint(attack, desc);
    Vec3 point{};
    const bool resolved =
        joint != nullptr && static_cast<bool>(frame.jointWorld) && frame.jointWorld(joint, point);
    if (sfx != nullptr && resolved) {
        append_encounter_sfx(attack == ValusAttack::Club ? EncounterSfx::ClubImpact
                                                         : EncounterSfx::StompImpact,
                             point, *sfx);
    }
    if (mode_is_attached(combat.mode)) {
        return;
    }
    if (!resolved) {
        return;
    }
    const float distance = player_capsule_distance(player.position, point);
    if (!std::isfinite(distance)) {
        return;
    }

    float damage = 0.0f;
    bool heavy = false;
    switch (attack) {
        case ValusAttack::StompL:
        case ValusAttack::StompR:
            if (distance <= valus_attack_desc(attack, desc)->hitRangeMeters) {
                damage = tuning.stompDamage;
            } else if (distance <= valus_attack_desc(attack, desc)->waveRangeMeters) {
                damage = tuning.stompWaveDamage;
            }
            break;
        case ValusAttack::Club:
            if (distance <= valus_attack_desc(attack, desc)->hitRangeMeters) {
                damage = tuning.clubDamage;
                heavy = true;
            }
            break;
        case ValusAttack::None:
            break;
    }
    if (!(damage > 0.0f)) {
        return;
    }
    const bool hitLanded = player_take_damage(combat, damage, heavy, tuning, events);
    if (sfx != nullptr && hitLanded) {
        // The reaction is the player's, pushed at the moment the damage lands so
        // the cue order is impact -> hurt. A ground-lethal hit is the death; an
        // air-lethal hit stays a hurt here and reports `died` on landing.
        const Sfx reaction = combat.mode == PlayerMode::Dead
                                 ? Sfx::Death
                                 : (heavy ? Sfx::HurtHeavy : Sfx::HurtLight);
        sfx->push(SfxEvent{reaction, 1.0f, player.position, /*spatial=*/true});
    }
}

ValusAttackInput valus_attack_input(const Scene& scene, std::size_t entityRig,
                                    const std::vector<std::vector<Mat4>>& palettes,
                                    const PlayerState& player, const PlayerCombat& combat,
                                    const Encounter& encounter) {
    ValusAttackInput input;
    input.playerGrounded = combat.mode == PlayerMode::Grounded;
    input.playerAttached = mode_is_attached(combat.mode);
    input.defeated = encounter.defeated;
    input.cooldownSeconds = encounter.cooldownSeconds;
    input.playerPos = player.position;
    input.entityPos = encounter.entityPosition;
    input.entityYawDeg = encounter.entityYawDeg;
    input.stompLeftNext = encounter.stompLeftNext;
    const float far = std::numeric_limits<float>::infinity();
    input.footL = Vec3{far, far, far};
    input.footR = Vec3{far, far, far};
    if (entityRig < palettes.size()) {
        Vec3 foot{};
        if (posed_joint_world_position(scene, entityRig, palettes[entityRig],
                                       encounter.desc->joints.footL, foot)) {
            input.footL = foot;
        }
        if (posed_joint_world_position(scene, entityRig, palettes[entityRig],
                                       encounter.desc->joints.footR, foot)) {
            input.footR = foot;
        }
    }
    return input;
}

void apply_combat_events(Encounter& encounter, Playback& entityPlayback,
                         const EntityClips& clips, const StepEvents& events, int weakpoint,
                         SfxQueue* sfx) {
    if (events.damageDealt <= 0 || encounter.defeated) {
        return;
    }
    if (valus_attack_cancelled(/*landedStrike=*/true, encounter.attack)) {
        cancel_valus_attack(encounter, sfx);
    }
    encounter.health =
        std::max(0.0f, encounter.health - static_cast<float>(events.damageDealt));
    if (encounter.health <= 0.0f) {
        encounter.defeated = true;
        encounter.pendingCooldownSeconds = 0.0f;
        encounter.deathWeakpoint = weakpoint;
        start_encounter_one_shot(entityPlayback, encounter, clips.death);
        return;
    }
    const int reaction =
        weakpoint == 1 ? clips.arm : (weakpoint == 0 ? clips.head : clips.body);
    start_encounter_one_shot(entityPlayback, encounter, reaction);
    if (sfx != nullptr) {
        append_encounter_sfx(EncounterSfx::ValusReaction, encounter.entityPosition, *sfx);
    }
}

Vec3 spawn_position(const SceneManifest& manifest, const Scene& scene) {
    if (manifest.hasSpawn) {
        return manifest.spawnPosition;
    }
    return Vec3{(scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
                (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
                (scene.boundsMin.z + scene.boundsMax.z) * 0.5f};
}

void print_scene_line(const Scene& scene) {
    size_t vertices = 0;
    for (const MeshCpu& mesh : scene.meshes) {
        vertices += mesh.vertices.size();
    }
    std::printf("s1-scene meshes=%zu vertices=%zu materials=%zu textures=%zu\n",
                scene.meshes.size(), vertices, scene.materials.size(), scene.textures.size());
}

std::vector<Mat4> blended_palette(const Scene& scene, std::size_t rigIndex,
                                  const Playback& playback, RigBlend& blend,
                                  const std::string& clipName, float time, float blendFrom) {
    const int index = clip_index_named(playback, clipName);
    if (index < 0) {
        return {};
    }
    const bool restarted =
        blendFrom >= 0.0f && (blend.lastBlend < 0.0f || blendFrom < blend.lastBlend);
    const bool switched = clipName != blend.clip || restarted;
    if (switched) {
        blend.from = blend.shown;
        blend.clip = clipName;
    }
    blend.lastBlend = blendFrom;
    const AnimationCpu& clip = playback.clips[static_cast<std::size_t>(index)];
    LocalPose pose =
        animation_local_pose(scene, rigIndex, clip, animation_loop_time(time, clip.duration));
    if (blendFrom >= 0.0f && !blend.from.t.empty()) {
        pose = blend_poses(blend.from, pose, blendFrom / kClipBlendSeconds);
    }
    blend.shown = pose;
    return pose_palette(scene, rigIndex, pose);
}

Vec3 yaw_forward(float yawDeg) {
    const float r = yawDeg * kDegToRad;
    return Vec3{std::sin(r), 0.0f, std::cos(r)};
}

Vec3 yaw_right(float yawDeg) {
    const float r = yawDeg * kDegToRad;
    return Vec3{std::cos(r), 0.0f, -std::sin(r)};
}

Vec3 to_body_frame(Vec3 p, Vec3 origin, float yawDeg) {
    const Vec3 d{p.x - origin.x, p.y - origin.y, p.z - origin.z};
    const Vec3 f = yaw_forward(yawDeg);
    const Vec3 r = yaw_right(yawDeg);
    return Vec3{d.x * r.x + d.z * r.z, d.y, d.x * f.x + d.z * f.z};
}

Vec3 from_body_frame(Vec3 local, Vec3 origin, float yawDeg) {
    const Vec3 f = yaw_forward(yawDeg);
    const Vec3 r = yaw_right(yawDeg);
    return Vec3{origin.x + r.x * local.x + f.x * local.z, origin.y + local.y,
                origin.z + r.z * local.x + f.z * local.z};
}

Mat4 translation_of(Vec3 t) {
    return mat4_from_trs(t, Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{1.0f, 1.0f, 1.0f});
}

Vec3 point_in_frame(const Mat4& m, Vec3 p) {
    const Vec3 d{p.x - m.m[12], p.y - m.m[13], p.z - m.m[14]};
    const auto along = [&](int column) {
        const Vec3 axis{m.m[4 * column], m.m[4 * column + 1], m.m[4 * column + 2]};
        const float norm = vec3_dot(axis, axis);
        return norm > 0.0f ? vec3_dot(axis, d) / norm : 0.0f;
    };
    return Vec3{along(0), along(1), along(2)};
}

void set_rig_model(Scene& scene, const RigCpu& rig, const Mat4& model) {
    for (std::size_t i = rig.meshBegin; i < rig.meshEnd && i < scene.meshes.size(); ++i) {
        scene.meshes[i].model = model;
    }
}

const float* footfall_fractions(const std::string& clip, RideSfx& cue) {
    if (clip == "horse_a_uma_walk") {
        cue = RideSfx::HoofWalk;
        return ride_points::kFootfallWalk;
    }
    if (clip == "horse_a_uma_run_n") {
        cue = RideSfx::HoofTrot;
        return ride_points::kFootfallTrot;
    }
    if (clip == "horse_a_uma_run") {
        cue = RideSfx::HoofGallop;
        return ride_points::kFootfallGallop;
    }
    return nullptr;
}

int footfalls_crossed(const float* fractions, float duration, float prev, float now) {
    if (fractions == nullptr || !(duration > 0.0f) || !(now > prev)) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        if (i > 0 && fractions[i] == fractions[i - 1]) {
            continue;
        }
        const float at = fractions[i] * duration;
        // Touchdowns at at + k * duration inside (prev, now].
        const float first = std::ceil((prev - at) / duration);
        for (float k = first; at + k * duration <= now; k += 1.0f) {
            if (at + k * duration > prev) {
                ++count;
            }
        }
    }
    return count;
}
