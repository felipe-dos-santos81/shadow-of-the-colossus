#include "selftests.h"

#include "capture.h"
#include "door.h"
#include "encounter_rt.h"
#include "ride_points.h"
#include "road_points.h"
#include "shrine_points.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

// The `s3-status` line the W1.5 acceptance script greps: the manifest's
// instance count, the chosen spawn, the player's position after the simulated
// frames, and whether the ground under it is real (a finite heightmap sample).
void print_s3_status(const SceneManifest& manifest, Vec3 spawn, Vec3 position, bool ground_ok) {
    std::printf("s3-status instances=%zu spawn=%.3f,%.3f,%.3f pos=%.3f,%.3f,%.3f ground=%s "
                "frames=%d\n",
                manifest.instances.size(), spawn.x, spawn.y, spawn.z, position.x, position.y,
                position.z, ground_ok ? "ok" : "none", kSelftestFrames);
}

}  // namespace

// --s3-selftest: the pinned W1.4 walk.
SelftestResult run_s3(StageCtx& ctx, RunState& run) {
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

    // The pinned W1.4 self-test: 240 fixed 1/60 s frames of
    // forward+run (camera-relative movement, the follow camera
    // recentring), then one rendered frame and the s3-status line. With
    // --screenshot the frame is also captured (the F3/wave check).
    const Vec3 spawn = player.position;
    PlayerInput input;
    input.forward = true;
    input.run = true;
    for (int frame = 0; frame < kSelftestFrames; ++frame) {
        if (targetCamera.active) {
            if (haveEntityChest) {
                camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
            }
        } else {
            camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f, 0.0f,
                          /*moving=*/true, kFixedFrameSeconds);
        }
        player_update(player, input, kFixedFrameSeconds, heightmap,
                      camera_look_yaw(camera));
        update_locomotion(playback, scene, input, manual_clip);
        const std::vector<std::vector<Mat4>> palettes =
            update_all_poses(renderer, scene, playbacks, kFixedFrameSeconds, club);
        // The collision hook: after this frame's pose/playback update,
        // before the player transform that is drawn below the loop.
        resolve_player_collision(player, entity_capsules(scene, palettes), heightmap);
        if (entityRig < scene.rigs.size() && entityRig < palettes.size()) {
            haveEntityChest = entity_chest_position(scene, entityRig, palettes[entityRig],
                                                   entityChest);
            if (targetCamera.active && haveEntityChest) {
                camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
            }
        }
    }
    if (targetCamera.active && haveEntityChest) {
        camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f, 0.0f);
    }
    apply_player_transform(scene, player_rig_record, player,
                           active_player_lift(scene, playback, player_rig_record));

    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                          /*acquire_swapchain=*/true);
    if (!opts.screenshotPath.empty()) {
        write_screenshot(renderer, shell.win, opts.screenshotPath);
    }
    print_status_line(opts, shell.win);
    print_s3_status(manifest, spawn, player.position,
                    std::isfinite(heightmap_sample(heightmap, player.position)));
    return 0;
}
