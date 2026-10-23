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

// --selftest (S1): one rendered frame, then the s1-status line.
SelftestResult run_selftest(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    Camera& camera = ctx.camera;
    std::vector<Playback>& playbacks = ctx.playbacks;

    update_all_poses(renderer, scene, playbacks, /*dt=*/0.0f, club);
    renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                          /*acquire_swapchain=*/true);
    print_status_line(opts, shell.win);
    return 0;
}

// --screenshot: the capture run (the S3/S4 acceptance stills).
SelftestResult run_screenshot(StageCtx& ctx, RunState& run) {
    const AppOptions& opts = run.opts;
    Shell& shell = run.shell;
    Renderer& renderer = run.renderer;
    Scene& scene = ctx.scene;
    ClubAttachment& club = ctx.club;
    Heightmap& heightmap = ctx.heightmap;
    const bool& follow_mode = ctx.follow_mode;
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

    // A capture run: in manifest mode the player stands at its spawn
    // (ground-followed, camera behind it) while the pose advances with
    // the real frame times; then one PNG is written.
    Uint64 last_ticks = SDL_GetTicks();
    for (int frame = 0; frame < opts.frames; ++frame) {
        const Uint64 now = SDL_GetTicks();
        const float dt = static_cast<float>(now - last_ticks) / 1000.0f;
        last_ticks = now;
        if (follow_mode) {
            const PlayerInput still;
            update_locomotion(playback, scene, still, manual_clip);
            const std::vector<std::vector<Mat4>> palettes =
                update_all_poses(renderer, scene, playbacks, dt, club);
            if (entityRig < scene.rigs.size() && entityRig < palettes.size()) {
                haveEntityChest = entity_chest_position(scene, entityRig,
                                                       palettes[entityRig], entityChest);
            }
            if (targetCamera.active) {
                if (haveEntityChest) {
                    camera_target_update(camera, targetCamera, entityChest, 0.0f, 0.0f,
                                         0.0f);
                }
            } else if (targetCamera.returning) {
                camera_target_return(camera, follow, targetCamera, player.position,
                                     player.yawDeg, 0.0f, 0.0f, 0.0f,
                                     /*moving=*/false, dt);
            } else {
                camera_follow(camera, follow, player.position, player.yawDeg, 0.0f, 0.0f,
                              0.0f, /*moving=*/false, dt);
            }
            player_update(player, still, dt, heightmap, camera_look_yaw(camera));
            // The collision hook: after player_update, before the
            // player transform the frame is drawn with.
            resolve_player_collision(player, entity_capsules(scene, palettes), heightmap);
            apply_player_transform(scene, player_rig_record, player,
                                   active_player_lift(scene, playback,
                                                      player_rig_record));
        } else {
            update_all_poses(renderer, scene, playbacks, dt, club);
        }
        renderer.render_frame(scene, camera_view_proj(camera, shell.win),
                              /*acquire_swapchain=*/true);
    }

    write_screenshot(renderer, shell.win, opts.screenshotPath);
    return 0;
}
