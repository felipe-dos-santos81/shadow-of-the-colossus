#pragma once

// The raised sword (S9 Task 2, design §6/D5/D6): a pure, headless-testable
// module, like `death.h`. It owns the raise state machine, the light and width
// smoothing, the beam's lock curves and the spark cooldown; `app.cpp` owns the
// input, the clip playback, the rays and the drawing. No I/O, no SDL, no clock.
//
//     Lowered --held--> Raising --startS--> Raised --release--> Lowering --endS--> Lowered
//
// Authored values (labelled): the sun, the shade/beam ray lengths, the light
// rate (0.3 per 1/30 s step; the unit is unverified) and the width step unit.

#include <array>

#include "math3d.h"

// The sun (D6, authored): mesh.metal's key_light literal, mirrored here;
// mesh.metal's comment names kSunDir.
inline const Vec3 kSunDir = vec3_normalize(Vec3{0.4f, 0.8f, 0.6f});
inline constexpr float kShadeRayMetres = 300.0f, kBeamRayPerRaise = 150.0f;
inline constexpr float kLightRatePerSecond = 9.0f;  // 0.3 per 1/30 s step (unit unverified, labelled)
inline constexpr float kWidthStepFraction = 0.1f;   // per 1/30 s step: w += (t - w) * (1 - 0.9^(30 dt))
inline constexpr float kMaxLineWidth = 1.35f, kLeanMaxDeg = 75.0f, kLockK = 0.01f, kSparkCooldown = 0.5f;

enum class SwordPhase { Lowered, Raising, Raised, Lowering };

struct SwordClips {
    const char *start, *loop, *end;
    float startS, loopS, endS;
};
inline constexpr SwordClips kSwordFoot{"player_a_hoist_sword_start", "player_a_hoist_sword_loop",
                                       "player_a_hoist_sword_end", 30 / 30.f, 50 / 30.f, 29 / 30.f};
inline constexpr SwordClips kSwordMounted{"player_a_uma_hoist_sword_start", "player_a_uma_hoist_sword_loop",
                                          "player_a_uma_hoist_sword_end", 35 / 30.f, 60 / 30.f, 25 / 30.f};

// allowed: on foot, or mounted at Stand. Losing it mid-raise lowers the sword.
struct SwordInput {
    bool held = false;
    bool mounted = false;
    bool allowed = true;
};

struct SwordBeam {
    float dot, k, leanDeg, widthTarget, rayLength;
    bool locked;
    Vec3 axis;
    std::array<Vec3, 8> lines;
};

struct SwordState {
    SwordPhase phase = SwordPhase::Lowered;
    bool mounted = false;
    const char* clip = nullptr;  // one of the SwordClips names, null while Lowered
    float clipTime = 0, raise = 0, light = 0, width = 0, sparkCooldown = 0;
    int sparks = 0;
    float raiseFrom = 0;  // the raise at the release; the end clip falls linearly from it
};

// The beam's curves for a blade at `tip` aimed along `aim`, with `target` the
// point it is being lined up on (design §6): dot -> k -> lean, width, lock.
SwordBeam sword_beam(Vec3 tip, Vec3 aim, Vec3 target, float raise);

// One frame. sunlit: the shade march found nothing within kShadeRayMetres;
// hitMetres: the locked ray's hit (NaN = none). The frame that leaves Lowered or
// starts lowering also advances by `dt`. A press during Lowering waits for
// Lowered. A negative or non-finite `dt` returns the state unchanged.
SwordState step_sword(SwordState, float dt, const SwordInput&, const SwordBeam&, bool sunlit,
                      float hitMetres);

float sword_intensity(const SwordState&);      // raise * light
bool sword_locks_movement(const SwordState&);  // on foot and not Lowered (turning stays)
SwordState reset_sword();
const char* sword_phase_name(SwordPhase);
