#include "selftests.h"

#include "capture.h"
#include "door.h"
#include "encounter_rt.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr float kS4FeetTolerance = 0.10f;    // the entity's lowest posed vertex vs ground
constexpr float kS4PenetrationTolerance = 0.02f;  // allowed capsule penetration (m)
constexpr float kS4StopBeforeTolerance = 1.0f;    // the stop must be before the axis point

// A `key=value` token in the manifest's comment header (the S4 provenance
// labels: `placement=builder-pick`, `spawn=data-position+builder-yaw`). The
// parser ignores comments, so the self-test reads them here; an absent token
// is the empty string.
std::string manifest_header_value(const std::string& path, const std::string& key) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    const std::string prefix = key + "=";
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash == std::string::npos) {
            continue;
        }
        std::size_t pos = hash + 1;
        while (pos < line.size()) {
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
                ++pos;
            }
            const std::size_t start = pos;
            while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
                ++pos;
            }
            const std::string token = line.substr(start, pos - start);
            if (token.rfind(prefix, 0) == 0) {
                return token.substr(prefix.size());
            }
        }
    }
    return {};
}

}  // namespace

// --s4-selftest's first block, in stage_setup before the playbacks print: pin
// the entity at t = 0 (the idle's first frame) so the walk's collision proxy and
// the status bbox sample one known pose.
SelftestResult s4_pin_entity(StageCtx& ctx) {
    Scene& scene = ctx.scene;
    SceneManifest& manifest = ctx.manifest;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;

    if (manifest.entities.empty() || entityRig >= scene.rigs.size() ||
        entityRig >= playbacks.size()) {
        std::fprintf(stderr,
                     "sotc-viewer: --s4-selftest: the manifest has no entity\n");
        return 2;
    }
    Playback& entity_playback = playbacks[entityRig];
    if (entity_playback.index < 0) {
        std::fprintf(stderr,
                     "sotc-viewer: --s4-selftest: the entity has no clip to pin\n");
        return 2;
    }
    entity_playback.time = 0.0f;
    entity_playback.playing = false;
    entity_playback.pinned = true;
    return {};
}

// --s4-selftest: the S4 encounter timeline (its second block; s4_pin_entity is the first).
SelftestResult run_s4(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    SceneManifest& manifest = ctx.manifest;
    Heightmap& heightmap = ctx.heightmap;
    const RigCpu& player_rig_record = *ctx.playerRigRecord;
    Camera& camera = ctx.camera;
    PlayerState& player = ctx.player;
    CameraFollow& follow = ctx.follow;
    CameraTarget& targetCamera = ctx.targetCamera;
    Vec3& entityChest = ctx.entityChest;
    bool& haveEntityChest = ctx.haveEntityChest;
    const std::size_t& entityRig = ctx.entityRig;
    std::vector<Playback>& playbacks = ctx.playbacks;
    Playback& playback = *ctx.playbackPtr;
    const bool& manual_clip = ctx.manual_clip;

    // The S4 acceptance walk: one pinned entity pose (t = 0), a guard
    // that the manifest's spawn is outside every entity capsule, the
    // feet-on-ground measurement, the screen bbox the idle-pair pixel
    // guard uses, and a walk into the nearest leg capsule that must
    // stop with bounded penetration while staying grounded.
    const std::string placement = manifest_header_value(opts.manifestPath, "placement");
    const std::string spawn_provenance = manifest_header_value(opts.manifestPath, "spawn");
    if (placement.empty() || spawn_provenance.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: %s carries no placement=/spawn= "
                     "provenance header\n",
                     opts.manifestPath.c_str());
        return 1;
    }

    const Playback& entity_playback = playbacks[entityRig];
    const AnimationCpu& entity_clip =
        entity_playback.clips[static_cast<std::size_t>(entity_playback.index)];
    const float entity_time = playback_time(entity_playback, entity_clip.duration);
    const std::vector<Mat4> entity_palette =
        animation_pose(scene, entityRig, entity_clip, entity_time);

    const std::vector<Capsule> capsules =
        entity_collision_capsules(scene, entityRig, entity_palette, ctx.enc().capsules);
    if (capsules.empty()) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: the entity rig has no collision capsules\n");
        return 1;
    }

    const Vec3 spawn = player.position;
    const float feet = player.position.y;
    const float head = feet + kPlayerCapsuleHeight;
    float spawn_clearance = std::numeric_limits<float>::infinity();
    for (const Capsule& capsule : capsules) {
        if (!capsule_overlaps_vertically(capsule, feet, head)) {
            continue;
        }
        Vec3 closest{};
        const float distance = capsule_horizontal_distance(capsule, spawn, closest);
        spawn_clearance = std::min(
            spawn_clearance, distance - (std::max(capsule.r, 0.0f) + kPlayerCapsuleRadius));
    }
    const bool capsules_reachable = std::isfinite(spawn_clearance);
    const bool spawn_clear = capsules_reachable && spawn_clearance >= 0.0f;

    const RigBounds entity_bounds = posed_rig_bounds(scene, entityRig, entity_palette);
    const Vec3 entity_position = manifest.entities[0].placement.position;
    const float entity_ground = heightmap_sample(heightmap, entity_position);
    const float lowest = entity_bounds.any ? entity_bounds.min.y : 0.0f;
    const float feet_delta = lowest - entity_ground;
    const bool feet_ok = entity_bounds.any && std::isfinite(entity_ground) &&
                         std::fabs(feet_delta) <= kS4FeetTolerance;

    // The idle pair's camera, reconstructed exactly as the capture path
    // builds it (`--camera-target player` at the manifest spawn): the
    // follow snap puts the orbit target at the player's head
    // (camera.h: playerPos + 1.4 m), the yaw at spawnYaw + 180, pitch 0
    // and distance 4.5 m. It does not depend on the entity's pose, so
    // the two idle captures share it while Valus animates.
    Camera idle_camera{};
    idle_camera.target = Vec3{spawn.x, spawn.y + 1.4f, spawn.z};
    idle_camera.yaw = manifest.spawnYawDeg + 180.0f;
    idle_camera.pitch = 0.0f;
    idle_camera.distance = CameraFollow{}.distance;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(shell.win, &pixel_width, &pixel_height)) {
        std::fprintf(stderr, "sotc-viewer: s4-selftest: %s\n", SDL_GetError());
        return 1;
    }
    const float pixel_aspect =
        static_cast<float>(pixel_width) / static_cast<float>(std::max(pixel_height, 1));
    const Mat4 idle_view_proj =
        mat4_mul(camera_proj(idle_camera, pixel_aspect), camera_view(idle_camera));
    // The entity's *drawn* region is the skinned rig plus the club it
    // holds. The club is a separate static mesh range outside the rig's
    // bbox, and it is what the entity-less presence control removes, so
    // leaving it out would put its pixels outside the guard's region.
    // Place it from the same pinned pose the captures draw.
    ScreenBox entity_bbox = project_rig_bounds(scene, entityRig, entity_palette,
                                               idle_view_proj, pixel_width,
                                               pixel_height);
    const RigCpu& entity_rig_record = rig_at(scene, entityRig);
    if (club.loaded && entity_rig_record.meshBegin < scene.meshes.size()) {
        const Mat4 club_placement =
            club_model(scene, entity_palette, club.swordJoint,
                       scene.meshes[entity_rig_record.meshBegin].model);
        entity_bbox = union_screen_boxes(
            entity_bbox, project_club_bounds(scene, club, club_placement, idle_view_proj,
                                             pixel_width, pixel_height));
    }

    // The walk's aim: the closest point on the nearest vertically
    // reachable capsule axis. Approaching it head-on makes the capsule
    // push exactly opposite the motion, so the stop is a stop, not a
    // slide around the leg. Other manifest entities (none in S4.0) are
    // not part of this aim.
    Vec3 aim{};
    float aim_distance = std::numeric_limits<float>::infinity();
    for (const Capsule& capsule : capsules) {
        if (!capsule_overlaps_vertically(capsule, feet, head)) {
            continue;
        }
        Vec3 closest{};
        const float distance = capsule_horizontal_distance(capsule, spawn, closest);
        if (distance < aim_distance) {
            aim_distance = distance;
            aim = closest;
        }
    }
    const bool have_aim = std::isfinite(aim_distance);
    const float aim_dx = aim.x - spawn.x;
    const float aim_dz = aim.z - spawn.z;
    const float aim_length = std::sqrt(aim_dx * aim_dx + aim_dz * aim_dz);
    if (!have_aim || !(aim_length > 0.0f)) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: no reachable entity capsule to walk into\n");
        return 1;
    }
    const Vec3 aim_dir{aim_dx / aim_length, 0.0f, aim_dz / aim_length};
    const float aim_yaw =
        std::atan2(aim_dx, aim_dz) / kDegToRad;  // the app's yaw convention

    PlayerInput input;
    input.forward = true;
    input.run = true;
    int frames = 0;
    int still_frames = 0;
    bool stopped = false;
    float max_ground_deviation = 0.0f;
    bool grounded = true;
    while (frames < kS4MaxWalkFrames) {
        const Vec3 previous = player.position;
        player_update(player, input, kFixedFrameSeconds, heightmap, aim_yaw);
        update_locomotion(playback, scene, input, manual_clip);
        update_all_poses(renderer, scene, playbacks, kFixedFrameSeconds, club);
        resolve_player_collision(player, capsules, heightmap);

        if (targetCamera.active) {
            if (haveEntityChest) {
                camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f,
                                     ctx.enc());
            }
        } else {
            camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                          0.0f, /*moving=*/true, kFixedFrameSeconds);
        }

        const float ground = heightmap_sample(heightmap, player.position);
        if (!std::isfinite(player.position.y) || !std::isfinite(ground)) {
            grounded = false;
        } else {
            max_ground_deviation =
                std::max(max_ground_deviation, std::fabs(player.position.y - ground));
        }

        const float move_dx = player.position.x - previous.x;
        const float move_dz = player.position.z - previous.z;
        const float moved = std::sqrt(move_dx * move_dx + move_dz * move_dz);
        ++frames;
        if (moved < kS4StopEpsilon) {
            if (++still_frames >= kS4StopFrames) {
                stopped = true;
                break;
            }
        } else {
            still_frames = 0;
        }
    }

    // The final frame is the drawn one: player transform, then the
    // capture (when asked).
    apply_player_transform(scene, player_rig_record, player,
                           active_player_lift(scene, playback, player_rig_record));
    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                          /*acquire_swapchain=*/true);
    if (!opts.screenshotPath.empty()) {
        write_screenshot(renderer, shell.win, opts.screenshotPath);
    }

    float final_clearance = std::numeric_limits<float>::infinity();
    for (const Capsule& capsule : capsules) {
        if (!capsule_overlaps_vertically(capsule, player.position.y,
                                         player.position.y + kPlayerCapsuleHeight)) {
            continue;
        }
        Vec3 closest{};
        const float distance =
            capsule_horizontal_distance(capsule, player.position, closest);
        final_clearance = std::min(
            final_clearance, distance - (std::max(capsule.r, 0.0f) + kPlayerCapsuleRadius));
    }
    const bool clearance_finite = std::isfinite(final_clearance);
    const float penetration = clearance_finite ? std::max(0.0f, -final_clearance) : 0.0f;
    const bool bounded = clearance_finite && penetration <= kS4PenetrationTolerance;
    const float progress = (player.position.x - spawn.x) * aim_dir.x +
                           (player.position.z - spawn.z) * aim_dir.z;
    const float stop_before = aim_length - progress;
    const bool no_pass_through =
        progress > 0.0f && stop_before >= kS4StopBeforeTolerance;
    const bool grounded_ok = grounded && max_ground_deviation <= kS4GroundTolerance;

    std::printf("s4-spawn capsules=%zu clearance=%.3f spawn=%.3f,%.3f,%.3f\n",
                capsules.size(), capsules_reachable ? spawn_clearance : -1.0f, spawn.x,
                spawn.y, spawn.z);
    std::printf("s4-entity clip=%s joints=%zu lowest=%.3f ground=%.3f delta=%.3f "
                "bbox=%d,%d,%d,%d screen=%dx%d\n",
                entity_clip.name.c_str(),
                rig_at(scene, entityRig).skin.joints.size(), lowest, entity_ground,
                feet_delta, entity_bbox.x0, entity_bbox.y0, entity_bbox.x1,
                entity_bbox.y1, pixel_width, pixel_height);
    std::printf("s4-walk frames=%d stop=%s aim=%.3f,%.3f,%.3f pos=%.3f,%.3f,%.3f "
                "clearance=%.3f penetration=%.3f stop_before=%.3f grounded=%.4f\n",
                frames, stopped ? "yes" : "no", aim.x, aim.y, aim.z, player.position.x,
                player.position.y, player.position.z,
                clearance_finite ? final_clearance : -1.0f, penetration, stop_before,
                max_ground_deviation);

    if (!spawn_clear) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: the spawn is inside an entity capsule "
                     "(clearance %.3f m)\n",
                     capsules_reachable ? spawn_clearance : 0.0f);
        return 1;
    }
    if (!feet_ok) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: entity feet are %.3f m off the ground "
                     "(lowest %.3f, ground %.3f; tolerance %.2f)\n",
                     feet_delta, lowest, entity_ground, kS4FeetTolerance);
        return 1;
    }
    if (!entity_bbox.any) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: the entity has no projected screen "
                     "region\n");
        return 1;
    }
    if (!stopped) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: the player did not stop within %d "
                     "frames\n",
                     kS4MaxWalkFrames);
        return 1;
    }
    if (!bounded || !no_pass_through) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: collision stop failed (clearance %.3f, "
                     "penetration %.3f, stop_before %.3f)\n",
                     final_clearance, penetration, stop_before);
        return 1;
    }
    if (!grounded_ok) {
        std::fprintf(stderr,
                     "sotc-viewer: s4-selftest: the player left the ground (max "
                     "deviation %.4f m)\n",
                     max_ground_deviation);
        return 1;
    }

    print_status_line(opts, shell.win);
    std::printf("s4-status instances=%zu rigs=%zu entity=%s placement=%s spawn=%s "
                "feet=%s entity_bbox=%d,%d,%d,%d pos=%.3f,%.3f,%.3f frames=%d\n",
                manifest.instances.size(), scene.rigs.size(), entity_clip.name.c_str(),
                placement.c_str(), spawn_provenance.c_str(), feet_ok ? "ok" : "none",
                entity_bbox.x0, entity_bbox.y0, entity_bbox.x1, entity_bbox.y1,
                player.position.x, player.position.y, player.position.z, frames);
    return 0;
}
