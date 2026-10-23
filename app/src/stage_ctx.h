#pragma once

// The stage's state (S10 Task 6b, design §13.9): what one iteration of
// run_app's stage loop composes and every path of it reads — the selftests,
// the capture and the interactive loop. Per-run state (the carry, the first
// stage, the return's availability) lives in RunState; StageCtx is rebuilt
// for each stage. stage_setup() fills it (the code that used to open the
// iteration); the methods are the lambdas the paths shared.

#include "app.h"
#include "audio_seam.h"
#include "camera.h"
#include "death.h"
#include "encounter_rt.h"
#include "heightmap.h"
#include "hud.h"
#include "manifest.h"
#include "player.h"
#include "player_state.h"
#include "renderer.h"
#include "ride.h"
#include "run_flags.h"
#include "scene.h"
#include "sword.h"
#include "transition.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// RAII owner of the SDL shell: window, GPU device, and SDL itself.
struct Shell {
    SDL_Window* win = nullptr;
    SDL_GPUDevice* device = nullptr;
    bool window_claimed = false;

    ~Shell() {
        if (device != nullptr) {
            if (window_claimed) {
                SDL_ReleaseWindowFromGPUDevice(device, win);
            }
            SDL_DestroyGPUDevice(device);
        }
        if (win != nullptr) {
            SDL_DestroyWindow(win);
        }
        SDL_Quit();
    }
};

// --- Manifest mode (S3 W1.4) ------------------------------------------------

constexpr float kHeightmapCellSize = 1.0f;        // metres between ground samples
constexpr float kFixedFrameSeconds = 1.0f / 60.0f;  // the s3-selftest's fixed step
// The player state machine clamps a frame to this; Valus's attack clock and yaw
// use the same ceiling so one long frame cannot skip an impact window.
constexpr float kMaxCombatStepSeconds = 0.05f;
constexpr int kSelftestFrames = 240;                // 4 simulated seconds

// --- S8: the plain, Agro and riding (design D10-D15) --------------------------
// Authored values (source: authored) are labelled; the clip names are data.
constexpr std::array<float, 3> kDoorFadeColour{0.0f, 0.0f, 0.0f};  // authored: the door fades black
constexpr const char* kAgroStandClip = "horse_a_uma_stand1";
constexpr const char* kRiderBone = "OptBone_boy";  // Agro's saddle seat (design §6)
constexpr const char* kWhistleClip = "player_a_whistle";
constexpr const char* kWhistleNearClip = "player_a_whistle_near";
constexpr float kWhistleNearRange = 15.0f;          // D12, authored: `_near` within 15 m
constexpr float kRideCameraNear = 6.0f;             // D13, authored: Stand/Walk/Back
constexpr float kRideCameraTrot = 7.0f;             // D13, authored
constexpr float kRideCameraGallop = 9.0f;           // D13, authored
constexpr float kRideHeadLift = 1.2f;               // D13, authored: over kFollowHeadHeight
constexpr float kRideCameraEase = 8.0f;             // s^-1, the follow camera's own rate
constexpr float kApproachTurnDegPerSecond = 90.0f;  // D12, authored: Agro turning to the player
constexpr float kStickPressThreshold = 0.5f;        // authored: a stick "press" (kick/rein)

constexpr int kS4MaxWalkFrames = 1200;       // 20 simulated seconds at 1/60 s
constexpr int kS4StopFrames = 5;             // consecutive frames below the epsilon
constexpr float kS4StopEpsilon = 1e-3f;      // metres of horizontal travel per frame
constexpr float kS4GroundTolerance = 1e-3f;  // the player's feet vs the heightmap sample

// What crosses the stages of one run (design §13.9): the process's window,
// device, renderer and audio engine, the stage to compose next, the carry
// (StageCarry, app.h) and the per-run latches.
struct RunState {
    const AppOptions& opts;
    Shell& shell;
    Renderer& renderer;
    AudioSeam& audio;  // the S5 audio engine lives for the run; each stage decides what it loads
    // The stage loop (S7 D6): the window, the device, the renderer and the
    // audio engine outlive every stage; each iteration composes one stage,
    // runs it and ends with a StageRequest. The frozen selftests still
    // return from inside the iteration and never request a switch; the
    // return to the shrine (interactive play, --s7-selftest) and R from the
    // shrine do. `carry` is what crosses (design D6).
    StageId stage = StageId::Arena;
    StageCarry carry;
    bool first_stage = true;
    // R16: cleared (once, with one warning) when interactive play finds the
    // shrine manifest or the wake clips missing; the run then plays on
    // without the return, as --no-return would.
    bool return_available = true;
    // S9: the sword pass's textures upload once per run (the renderer keeps
    // them across stages); a failure warns once and the raise draws no light.
    bool sword_fx_ready = false;
    bool sword_fx_warned = false;
    // --s9-selftest: Agro's pose where its script left her (after the
    // dismount at kAgroStairs) — the reference the road's reload is checked
    // against, independent of the production carry.horse store.
    std::optional<HorsePose> s9AgroLeft;
};

// The plain's ride state (S8 R17): everything plain_step carries from frame
// to frame, as one value (a fresh one per stage).
struct PlainRide {
    RideState ride = reset_ride();
    AgroState agro{};
    Vec3 horsePos{};
    float horseYaw = 0.0f;
    Vec3 horseScale{1.0f, 1.0f, 1.0f};
    RigBlend horseBlend;
    RigBlend riderBlend;
    std::string hoofClip;
    float hoofTime = 0.0f;
    int whistleClip = -1;  // the player's whistle one-shot, -1 = none
    float whistleSeconds = 0.0f;
    bool agroProgressed = true;
    // R11: the ground refused last frame's ridden move (fed back as a rein).
    bool rideBlocked = false;
    // R12: the mount's start — the player's on-foot spot, resolved into the
    // seat's frame on the first mounted pose (then mount_offset lerps it).
    bool mountFootPending = false;
    Vec3 mountFootWorld{};
    Vec3 mountFoot{};
    // Last frame's held ride keys: the presses are their edges.
    RideKeys rideKeysBefore;
};

struct StageCtx {
    explicit StageCtx(RunState& run)
        : opts(run.opts), shell(run.shell), renderer(run.renderer), audio(run.audio),
          carry(run.carry), sword_fx_ready(run.sword_fx_ready) {}

    // The run's objects, by reference: the stage's code names them directly.
    const AppOptions& opts;
    Shell& shell;
    Renderer& renderer;
    AudioSeam& audio;
    StageCarry& carry;
    bool& sword_fx_ready;

    StageId here = StageId::Arena;
    const char* stage_name = "";
    // Scene: a single glb (--model alone) or a composed manifest scene
    // (--manifest, with --model as the player's glb). `Scene scene{}` keeps
    // zeroed bounds for a model-less run, so camera_frame stays finite even
    // when there is nothing to draw.
    Scene scene{};
    // The entity prop: loaded only in a composed scene with an entity (see
    // load_club); identity/unloaded everywhere else.
    ClubAttachment club{};
    // The S6 corpse prop and the tendril pass's readiness: both are loaded
    // only when the death is wired (see below).
    CorpseAttachment corpse{};
    bool effects_ready = false;
    SceneManifest manifest{};          // empty when --manifest is absent
    Heightmap heightmap{};             // an empty heightmap = no ground
    // S9 (D6): the road's backdrop ray grid (the non-ground statics), and
    // the sword's static mesh range — both empty off the road.
    Heightmap raygrid{};
    std::size_t swordMeshBegin = 0, swordMeshEnd = 0;
    bool follow_mode = false;
    RunFlags flags{};
    bool death_wired = false;
    bool road = false;
    int door_rows = 0;
    bool return_wired = false;
    const RigCpu* playerRigRecord = nullptr;  // `player_rig_record` in the stage's code
    // The real camera: framed on the scene bounds at load (and again on F).
    Camera camera{};
    Vec3 stage_spawn{};
    float stage_spawn_yaw = 0.0f;
    const DoorRow* arrival = nullptr;
    bool door_arrival = false;
    PlayerState player{};
    // The combat state machine and the encounter's own state (R5) live for
    // the whole run; the climb camera is entered/left by the mode.
    PlayerCombat combat{};
    Encounter encounter{};
    // One tuning table for every step path — the scripted timelines and the
    // interactive loop — and for the HUD's health fractions.
    const CombatTuning tuning{};
    CameraClimb climbCamera{};
    CameraFollow follow;
    // Entity target framing (T): the orbit point is refreshed from the
    // entity's posed chest every frame. The state is inert until entered.
    CameraTarget targetCamera;
    Vec3 entityChest{};
    bool haveEntityChest = false;
    std::size_t playerRig = 0;
    std::size_t entityRig = 0;
    std::vector<Playback> playbacks{};
    Playback* playbackPtr = nullptr;  // `playback` in the stage's code
    std::size_t horseRig = 0;
    // S4.1: a manifest with an entity is an encounter. Every clip the climb
    // and fight name must exist before the run starts — a missing one is a
    // load error naming the glb and the clip, never a silent fallback to the
    // first clip. (S3's arena has no entity and needs none of them.)
    PlayerClips playerClips{};
    EntityClips entityClips{};
    ReturnTimings returnTimings{};
    int wakeFirst = -1;
    int wakeSecond = -1;
    float wakeFirstSeconds = 0.0f;
    bool manual_clip = false;
    bool valus_behavior = false;
    bool s8_wired = false;
    bool plain_ride = false;
    PlainRide pr{};
    // S9 (D5/D6): the raised sword — on the road only, with its six clips
    // and its bone; any of them missing leaves it lowered (one line).
    SwordState sword = reset_sword();
    bool sword_wired = false;
    bool swordSearchLatch = false;  // the search cue's lock edge (once per lock)

    // One HUD view of the run for every set_hud site (design §7.7). The
    // overlay belongs to an encounter, so a manifest with no entity draws
    // none; Valus's bar is zero-width until the first landed hit, so its
    // appearance is itself the feedback that he can be hurt.
    HudState hud_state(bool visible);
    Mat4 horse_model();
    float horse_distance();
    // One plain frame, after this frame's update_all_poses: the presses,
    // the whistle, the ride and Agro's machines, her travel over the
    // ground, both rigs' blended poses, the rider on the saddle, and the
    // cues. `locked` (a door fade) eats every press; a key held through
    // it is not a fresh press when it ends (ride_presses).
    void plain_step(float dt, RideKeys keys, bool locked, std::vector<std::vector<Mat4>>& palettes);
    // The one soft reset (design §7.5 and S6): the player's and Valus's
    // opening state, the death machine back to Idle, the corpse hidden and
    // the live body shown, the cameras back to the follow pose. The
    // interactive `R` handler and the S6 selftest **both call this**, so
    // the acceptance proves the reset the player actually presses
    // (anti-goal A2) instead of a copy of it. `cameraBaseSet` is the
    // interactive path's cinematic-camera latch (the selftest has no such
    // latch and passes nullptr).
    void soft_reset_encounter(DeathState& deathState, bool& cueFired, bool* cameraBaseSet);
    // --- S7: R, the wake, and the return's presentation ------------------
    // `R` anywhere (design D11): the return machine goes back to Idle;
    // in the arena it is the S6 soft reset above, and from the shrine
    // (or anywhere but the arena) it reloads the arena — the new
    // iteration is the opening state. Returns true when the caller must
    // end the iteration with a request for the arena.
    bool press_reset(DeathState& deathState, bool& cueFired, bool* cameraBaseSet);
    // S9 (D5-D7): one sword frame — the sword in the hand, its beam, the
    // shade and the lock, the machine, the cues and the light — after the
    // rig's model is final for this frame (on foot or in the saddle). The
    // interactive loop and --s9-selftest both call it; `fx` is this frame's
    // light (active only while the sword is up and the pass is ready).
    SwordProbe sword_frame(float dt, bool held, bool inputLocked, const std::vector<std::vector<Mat4>>& palettes, SwordFxFrame& fx);
    // S8 (D14): the doors — the shrine's front exit and the plain's shrine
    // steps, S9's corridor and arena doors — fire on entry, on foot, through
    // the door machine; true when its Load must end the iteration for the
    // other stage (this frame still draws, faded). R16: an entry only while
    // the stage still runs (`live`: R or Escape this frame ended it), from
    // the kDoors rows of this stage. The interactive loop and --s9-selftest
    // both call it.
    bool door_frame(float dt, Vec3 positionBefore, bool riding, bool inputLocked, bool live);
    // The wake (D9, ruling R5): FadeIn shows its first frame, Wake plays
    // cut04 then cut05 once, pinned to the machine's clock, in place at
    // the spawn. The clips carry no root series (the pelvis sits at the
    // model origin), so the body is lifted by the pose's own pelvis
    // height: the lowest of the recon's body joints is planted on the
    // floor every frame — the measure `wake.pelvis_m` was taken with
    // (0.10 m lying -> 0.72 m standing; --s7-selftest checks it).
    bool waking();
    float wake_clock();
    void pose_wake();
    // Leaving the wake: the clip clock runs again and the mode's clip
    // (the stand loop) takes over on the next update_combat_clips.
    void leave_wake();
    float wake_plant_lift();
    float player_lift();
};

// Composes stage `run.stage` into `ctx` (the opening of the stage loop's
// iteration). Returns the run's exit code when the stage cannot start (a usage
// error, a missing asset that is fatal for this run), nullopt to go on.
std::optional<int> stage_setup(RunState& run, StageCtx& ctx);
