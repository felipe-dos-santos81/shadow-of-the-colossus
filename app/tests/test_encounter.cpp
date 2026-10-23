// S11 Task 7: Valus's EncounterDesc equals the literals the code carried before
// the table existed (copied verbatim below as the oracle: grab.cpp's band table,
// collision.cpp's capsules, valus.h's attack constants, death.h/camera.h's death
// bundle, audio_map.h's cue rows, encounter_rt.h's clip/glb names, camera.h's
// target camera, app.h's max health), field by field and order by order.

#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "audio_map.h"
#include "camera.h"
#include "death.h"
#include "encounter_rt.h"
#include "encounters/registry.h"
#include "valus.h"

namespace {

using encounters::kValus;

bool same(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return std::strcmp(a, b) == 0;
}

void check_cue(const SoundCue& cue, const char* clip, float gain, bool s6Only) {
    CHECK(same(cue.clip, clip));
    CHECK(cue.gain == gain);
    CHECK_FALSE(cue.loop);
    CHECK(cue.s6Only == s6Only);
}

}  // namespace

// The shape (§13.10): 9 bands with int links, 8 capsules, 2 weakpoints in their
// own table, 3 attacks + None.
static_assert(kValus.bands.size() == 9);
static_assert(kValus.bands.size() == static_cast<std::size_t>(ClimbBandId::Count));
static_assert(kValus.capsules.size() == 8);
static_assert(kValus.weakpoints.size() == 2);
static_assert(kValus.attacks.size() == 3);
static_assert(static_cast<int>(ValusAttack::None) == 0 &&
              static_cast<int>(ValusAttack::Club) == static_cast<int>(kValus.attacks.size()));
static_assert(kValus.props.size() == 3);
static_assert(std::is_same_v<decltype(ClimbBand::up), int> &&
              std::is_same_v<decltype(ClimbBand::down), int> &&
              std::is_same_v<decltype(ClimbBand::branch), int>);
static_assert(ClimbBandId::None == -1);
static_assert(behaviour_valid(kValus.behaviour));

TEST_CASE("Valus's bands are grab.cpp's table, in order") {
    struct Row {
        const char* a;
        const char* b;
        float radius;
        int up;
        int down;
        int weakpoint;
        int branch;
        const char* name;
    };
    // grab.cpp's kClimbBands (Count -> -1) and climb_band_name's tokens.
    const Row oracle[] = {
        {"Bone_TUMASAKI_L", "Bone_TOE_L", 1.7f, 1, -1, -1, -1, "left_foot"},
        {"Bone_TOE_L", "Bone_MOMO_L", 1.6f, 2, 0, -1, -1, "left_shin"},
        {"Bone_MOMO_L", "Bone_KOSI", 1.7f, 3, 1, -1, -1, "left_thigh"},
        {"Bone_KOSI", "Bone_HARA", 2.0f, 4, 2, -1, -1, "waist"},
        {"Bone_HARA", "Bone_MUNE", 2.0f, 5, 3, -1, 7, "chest"},
        {"Bone_MUNE", "Bone_KUBI", 1.6f, 6, 4, -1, -1, "neck"},
        {"Bone_KUBI", "Bone_KAO", 1.75f, -1, 5, 0, -1, "head"},
        {"Bone_MUNE", "Bone_KATA_R", 1.5f, 8, 4, -1, -1, "right_shoulder"},
        {"Bone_KATA_R", "Bone_UDE_R", 1.2f, -1, 7, 1, -1, "right_arm"},
    };
    REQUIRE(kValus.bands.size() == std::size(oracle));
    for (std::size_t i = 0; i < std::size(oracle); ++i) {
        CAPTURE(i);
        const ClimbBand& band = kValus.bands[i];
        CHECK(same(band.boneA, oracle[i].a));
        CHECK(same(band.boneB, oracle[i].b));
        CHECK(band.radius == oracle[i].radius);
        CHECK(band.up == oracle[i].up);
        CHECK(band.down == oracle[i].down);
        CHECK(band.weakpoint == oracle[i].weakpoint);
        CHECK(band.branch == oracle[i].branch);
        CHECK_FALSE(band.horizontal);
        CHECK(same(band.name, oracle[i].name));
    }
    CHECK(ClimbBandId::Chest == 4);
    CHECK(ClimbBandId::RightShoulder == 7);
}

TEST_CASE("Valus's capsules are collision.cpp's table, in order") {
    const CapsuleDesc oracle[] = {
        {"Bone_MOMO_L", "Bone_SUNE_L", 1.7f},  {"Bone_SUNE_L", "Bone_TOE_L", 1.6f},
        {"Bone_TOE_L", "Bone_TUMASAKI_L", 1.7f}, {"Bone_MOMO_R", "Bone_SUNE_R", 1.7f},
        {"Bone_SUNE_R", "Bone_TOE_R", 1.6f},  {"Bone_TOE_R", "Bone_TUMASAKI_R", 1.7f},
        {"Bone_HARA", "Bone_MUNE", 2.0f},     {"Bone_MUNE", "Bone_KUBI", 2.0f},
    };
    REQUIRE(kValus.capsules.size() == std::size(oracle));
    for (std::size_t i = 0; i < std::size(oracle); ++i) {
        CAPTURE(i);
        CHECK(same(kValus.capsules[i].from, oracle[i].from));
        CHECK(same(kValus.capsules[i].to, oracle[i].to));
        CHECK(kValus.capsules[i].radius == oracle[i].radius);
    }
}

TEST_CASE("Valus's identity, joints, props, weakpoints and health") {
    CHECK(same(kValus.id, "valus"));
    CHECK(kValus.maxHealth == 100.0f);
    CHECK(same(kValus.joints.chest, "Bone_MUNE"));
    CHECK(same(kValus.joints.footL, "Bone_TUMASAKI_L"));
    CHECK(same(kValus.joints.footR, "Bone_TUMASAKI_R"));

    REQUIRE(kValus.props.size() == 3);
    CHECK(kValus.props[0].role == PropRole::Weapon);
    CHECK(same(kValus.props[0].glb,
               "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a_weapon.glb"));
    CHECK(same(kValus.props[0].socketJoint, "OptBone_sword"));
    CHECK(kValus.props[1].role == PropRole::Corpse);
    CHECK(same(kValus.props[1].glb, "models/shadow/characters/colossus_01_minotaur_a_deadbody/"
                                    "_cmn/c01_minotaur_a_dead_body.glb"));
    CHECK(kValus.props[1].socketJoint == nullptr);
    CHECK(kValus.props[2].role == PropRole::Ground);
    CHECK(same(kValus.props[2].glb, "models/shadow/characters/colossus_01_minotaur_a_deadbody/"
                                    "_cmn/c01_minotaur_a_dead_dirtmound.glb"));
    CHECK(kValus.props[2].socketJoint == nullptr);
    CHECK(encounter_prop(kValus, PropRole::Weapon) == &kValus.props[0]);
    CHECK(encounter_prop(kValus, PropRole::Ground) == &kValus.props[2]);

    CHECK(same(kValus.weakpoints[0].material, "weakpoint_03"));
    CHECK(same(kValus.weakpoints[0].reactionClip, "minotaur_a_add_head_damage"));
    CHECK(same(kValus.weakpoints[1].material, "weakpoint_04"));
    CHECK(same(kValus.weakpoints[1].reactionClip, "minotaur_a_add_ude_r_damage"));
    CHECK(same(kValus.bodyReactionClip, "minotaur_a_add_mune_r_damage"));
    CHECK(kValus.turnDegPerSec == 30.0f);
}

TEST_CASE("Valus's attacks are valus.h's constants, indexed ValusAttack - 1") {
    const AttackDesc& stompL = kValus.attacks[static_cast<int>(ValusAttack::StompL) - 1];
    const AttackDesc& stompR = kValus.attacks[static_cast<int>(ValusAttack::StompR) - 1];
    const AttackDesc& club = kValus.attacks[static_cast<int>(ValusAttack::Club) - 1];
    CHECK(same(stompL.clip, "minotaur_a_atk_leg_l"));
    CHECK(same(stompR.clip, "minotaur_a_atk_leg_r"));
    CHECK(same(club.clip, "minotaur_a_atk_weapon2_a"));
    CHECK(same(stompL.damageJoint, "Bone_TUMASAKI_L"));
    CHECK(same(stompR.damageJoint, "Bone_TUMASAKI_R"));
    CHECK(same(club.damageJoint, "OptBone_hit"));
    for (const AttackDesc* stomp : {&stompL, &stompR}) {
        CHECK(stomp->reachMeters == 4.5f);
        CHECK(stomp->facingDot == -1.0f);
        CHECK(stomp->hitRangeMeters == 2.5f);
        CHECK(stomp->waveRangeMeters == 6.0f);
        CHECK(stomp->cooldownSeconds == 4.0f);
        CHECK(stomp->impactBegin == 0.55f);
        CHECK(stomp->impactEnd == 0.70f);
        CHECK(stomp->swingSeconds == -1.0f);
        CHECK(stomp->damage == 35.0f);
        CHECK(stomp->waveDamage == 15.0f);
        CHECK_FALSE(stomp->heavy);
    }
    CHECK(club.reachMeters == 14.0f);
    CHECK(club.facingDot == 0.2f);
    CHECK(club.hitRangeMeters == 2.5f);
    CHECK(club.waveRangeMeters == 0.0f);
    CHECK(club.cooldownSeconds == 5.0f);
    CHECK(club.impactBegin == 0.45f);
    CHECK(club.impactEnd == 0.65f);
    CHECK(club.swingSeconds == 0.30f);
    CHECK(club.damage == 45.0f);
    CHECK(club.waveDamage == 0.0f);
    CHECK(club.heavy);
}

TEST_CASE("Valus's death bundle is death.h's timeline and camera.h's 16 keys") {
    const DeathDesc& d = kValus.death;
    CHECK(same(d.clip, "minotaur_a_death_front"));
    CHECK(d.motionSeconds == 6.166877f);
    CHECK(d.tendrilStartSeconds == 0.4f);
    CHECK(d.tendrilEndSeconds == 3.0f);
    CHECK(d.endSeconds == 8.0f);
    CHECK(d.roots == 2);
    CHECK(d.fallbackChestHeight == 8.0f);
    const DeathCameraDesc& c = d.camera;
    CHECK(c.pullSeconds == 0.4f);
    CHECK(c.lowSeconds == 3.0f);
    CHECK(c.wideSeconds == 6.2f);
    CHECK(c.followSeconds == 8.0f);
    CHECK(c.pullDistance == 12.0f);
    CHECK(c.pullEyeHeight == 4.0f);
    CHECK(c.pullTargetHeight == 9.0f);
    CHECK(c.pullPitchDeg == -24.6243f);
    CHECK(c.lowTargetHeight == 10.0f);
    CHECK(c.lowDistance == 8.0f);
    CHECK(c.lowPitchDeg == -40.0f);
    CHECK(c.lowYawOffsetDeg == 25.0f);
    CHECK(c.wideTargetHeight == 2.0f);
    CHECK(c.wideDistance == 24.0f);
    CHECK(c.widePitchDeg == 12.0f);
    CHECK(c.wideYawOffsetDeg == 45.0f);
}

TEST_CASE("Valus's cues are audio_map.h's rows and the target camera camera.h's") {
    const CueDesc& c = kValus.cues;
    check_cue(c.stompStart, "valus.telegraph", 0.60f, false);
    check_cue(c.stompImpact, "valus.impact", 0.50f, false);
    check_cue(c.clubStart, "valus.telegraph", 0.60f, false);
    check_cue(c.clubImpact, "valus.impact", 0.50f, false);
    check_cue(c.clubCancel, nullptr, 1.00f, false);
    check_cue(c.reaction, "valus.hurt", 0.60f, false);
    check_cue(c.defeat, "valus.death", 0.80f, true);
    check_cue(c.footLeft, nullptr, 1.00f, false);
    check_cue(c.footRight, nullptr, 1.00f, false);
    check_cue(c.clubSwing, "valus.club_swing", 0.70f, true);
    CHECK(same(c.music, "music.boss"));
    CHECK(c.musicGain == 0.45f);

    const TargetCamDesc& t = kValus.targetCam;
    CHECK(t.distanceMin == 22.0f);
    CHECK(t.distanceMax == 30.0f);
    CHECK(t.defaultDistance == 26.0f);
    CHECK(t.defaultPitchDeg == 10.0f);
    CHECK(t.pitchLimitDeg == 80.0f);
}

TEST_CASE("Valus's behaviour is the one-state machine: stand + three attacks, both shells damage") {
    const BehaviourDesc& b = kValus.behaviour;
    REQUIRE(b.states.size() == 1);
    CHECK(b.states[0].clip == 0);
    CHECK(b.states[0].clipEndNext == kBehaviourNone);
    CHECK(b.states[0].turnRateDegPerSecond == kValus.turnDegPerSec);
    CHECK(b.states[0].firstAction == 0);
    CHECK(b.states[0].actionCount == 3);
    REQUIRE(b.actions.size() == 3);
    for (int i = 0; i < 3; ++i) {
        CHECK(b.actions[static_cast<std::size_t>(i)].clip == i + 1);
        CHECK(b.actions[static_cast<std::size_t>(i)].cooldownSeconds ==
              kValus.attacks[static_cast<std::size_t>(i)].cooldownSeconds);
    }
    REQUIRE(b.gates.size() == 2);
    for (int w = 0; w < 2; ++w) {
        CHECK(behaviour_gate(b, 0, w).kind == GateKind::Damage);
    }
}

TEST_CASE("the encounter id selects the table; an unknown id has none") {
    CHECK(find_encounter("valus") == &kValus);
    CHECK(find_encounter("quadratus") == &encounters::kQuadratus);
    CHECK(find_encounter("") == nullptr);
    CHECK(find_encounter("Valus") == nullptr);
}

// --- Every runtime reader takes the selected table (S11 M-1) -----------------
//
// A non-Valus table whose every number differs from Valus's: each reader below
// is fed it and must answer with its values, so a reader that still reads
// Valus's constants fails here even though Valus's goldens cannot see it.

namespace {

inline constexpr std::array<WeakpointDesc, 3> kOtherWeakpoints{{
    {"qa", "qa_react"},
    {"qb", "qb_react"},
    {"qc", "qc_react"},
}};

inline constexpr std::array<AttackDesc, 3> kOtherAttacks{{
    {"other_stomp_l", "Q_FOOT_L", 2.0f, -1.0f, 1.0f, 3.0f, 7.0f, 0.2f, 0.3f, -1.0f, 11.0f, 5.0f,
     false},
    {"other_stomp_r", "Q_FOOT_R", 2.0f, -1.0f, 1.0f, 3.0f, 7.0f, 0.2f, 0.3f, -1.0f, 11.0f, 5.0f,
     false},
    {"other_club", "Q_CLUB", 20.0f, 0.9f, 1.5f, 0.0f, 9.0f, 1.0f, 1.2f, 0.8f, 21.0f, 0.0f, false},
}};

inline constexpr EncounterDesc kOther{
    .id = "other",
    .maxHealth = 50.0f,
    .joints = {"Q_CHEST", "Q_FOOT_L", "Q_FOOT_R"},
    .props = {},
    .weakpoints = kOtherWeakpoints,
    .bodyReactionClip = "other_body",
    .bands = {},
    .capsules = {},
    .turnDegPerSec = 10.0f,
    .attacks = kOtherAttacks,
    .death = {"other_death", 5.333f, 1.0f, 2.0f, 9.0f, 1, 5.0f,
              {1.0f, 2.0f, 4.0f, 9.0f, 7.0f, 2.0f, 3.0f, -10.0f, 4.0f, 5.0f, -20.0f, 30.0f, 1.0f,
               15.0f, 5.0f, 60.0f}},
    .cues = {{"other.telegraph", 0.3f, false},
             {"other.impact", 0.3f, false},
             {"other.telegraph", 0.3f, false},
             {"other.impact", 0.3f, false},
             {nullptr, 1.0f, false},
             {"other.hurt", 0.3f, false},
             {"other.death", 0.3f, false, true},
             {nullptr, 1.0f, false},
             {nullptr, 1.0f, false},
             {nullptr, 1.0f, false},  // no club swing
             "other.music",
             0.25f},
    .targetCam = {10.0f, 40.0f, 15.0f, 20.0f, 60.0f},
};

// The same table with two tendril roots (the root order needs a second slot).
inline constexpr EncounterDesc kOtherTwoRoots = [] {
    EncounterDesc desc = kOther;
    desc.death.roots = 2;
    return desc;
}();

// Only a left stomp: the club and the right stomp have no row.
inline constexpr EncounterDesc kStompOnly = [] {
    EncounterDesc desc = kOther;
    desc.attacks = std::span<const AttackDesc>(kOtherAttacks.data(), 1);
    return desc;
}();

ValusAttackInput attack_input(Vec3 player) {
    ValusAttackInput input;
    input.playerGrounded = true;
    input.playerPos = player;
    input.footL = Vec3{0.0f, 0.0f, 7.0f};
    input.footR = Vec3{1000.0f, 0.0f, 1000.0f};
    return input;  // the entity at the origin, facing +Z
}

DeathState death_at(float total, const EncounterDesc& desc) {
    return step_death(step_death({}, 0.0f, true, desc), total, true, desc);
}

}  // namespace

TEST_CASE("select_valus_attack reads the selected table's reach and facing") {
    // 3 m from the left foot, 10 m in front: Valus stomps (4.5 m), the other
    // table's 2 m stomp misses and its 20 m club (dot > 0.9) answers.
    CHECK(select_valus_attack(attack_input({0.0f, 0.0f, 10.0f}), kValus) == ValusAttack::StompL);
    CHECK(select_valus_attack(attack_input({0.0f, 0.0f, 10.0f}), kOther) == ValusAttack::Club);
    // 16 m: past Valus's 14 m club.
    CHECK(select_valus_attack(attack_input({0.0f, 0.0f, 16.0f}), kValus) == ValusAttack::None);
    CHECK(select_valus_attack(attack_input({0.0f, 0.0f, 16.0f}), kOther) == ValusAttack::Club);
    // A facing dot of 0.8: inside Valus's 0.2 gate, outside the other's 0.9.
    CHECK(select_valus_attack(attack_input({6.0f, 0.0f, 8.0f}), kValus) == ValusAttack::Club);
    CHECK(select_valus_attack(attack_input({6.0f, 0.0f, 8.0f}), kOther) == ValusAttack::None);
    // No club row: never a club; the one stomp row still answers.
    CHECK(select_valus_attack(attack_input({0.0f, 0.0f, 16.0f}), kStompOnly) == ValusAttack::None);
    CHECK(select_valus_attack(attack_input({0.0f, 0.0f, 8.0f}), kStompOnly) == ValusAttack::StompL);
}

TEST_CASE("attack_impact_due, the cooldown and the row lookup read the selected table") {
    CHECK(attack_impact_due(ValusAttack::StompL, 0.25f, false, kOther));
    CHECK_FALSE(attack_impact_due(ValusAttack::StompL, 0.25f, false, kValus));
    CHECK_FALSE(attack_impact_due(ValusAttack::StompR, 0.6f, false, kOther));
    CHECK(attack_impact_due(ValusAttack::StompR, 0.6f, false, kValus));
    CHECK(attack_impact_due(ValusAttack::Club, 1.1f, false, kOther));
    CHECK_FALSE(attack_impact_due(ValusAttack::Club, 1.1f, false, kValus));
    CHECK_FALSE(attack_impact_due(ValusAttack::Club, 1.1f, false, kStompOnly));

    CHECK(valus_attack_cooldown_seconds(ValusAttack::StompR, kOther) == 7.0f);
    CHECK(valus_attack_cooldown_seconds(ValusAttack::Club, kOther) == 9.0f);
    CHECK(valus_attack_cooldown_seconds(ValusAttack::Club, kStompOnly) == 0.0f);

    REQUIRE(valus_attack_desc(ValusAttack::StompL, kOther) != nullptr);
    CHECK(same(valus_attack_desc(ValusAttack::StompL, kOther)->clip, "other_stomp_l"));
    CHECK(valus_attack_desc(ValusAttack::Club, kStompOnly) == nullptr);
}

TEST_CASE("step_death and death_tendrils_active follow the selected death bundle") {
    CHECK(death_at(0.0f, kOther).roots == 1);
    CHECK(death_at(5.5f, kOther).phase == DeathPhase::Collapse);  // past 5.333 s
    CHECK(death_at(5.5f, kValus).phase == DeathPhase::Motion);    // before 6.167 s
    CHECK(death_at(8.5f, kOther).phase == DeathPhase::Collapse);  // before 9 s
    CHECK(death_at(8.5f, kValus).phase == DeathPhase::Done);      // past 8 s
    const DeathState done = death_at(20.0f, kOther);
    CHECK(done.phase == DeathPhase::Done);
    CHECK(done.total == 9.0f);

    CHECK_FALSE(death_tendrils_active(death_at(0.5f, kOther), kOther));
    CHECK(death_tendrils_active(death_at(0.5f, kValus), kValus));
    CHECK(death_tendrils_active(death_at(1.5f, kOther), kOther));
    CHECK_FALSE(death_tendrils_active(death_at(2.5f, kOther), kOther));
    CHECK(death_tendrils_active(death_at(2.5f, kValus), kValus));
}

TEST_CASE("death_cue_due and death_camera_key read the selected death bundle") {
    bool fired = false;
    CHECK_FALSE(death_cue_due(death_at(0.5f, kOther), fired, kOther));  // the burst is at 1 s
    CHECK_FALSE(fired);
    CHECK(death_cue_due(death_at(1.0f, kOther), fired, kOther));
    CHECK(fired);
    bool valusFired = false;
    CHECK(death_cue_due(death_at(0.5f, kValus), valusFired, kValus));

    CHECK(std::string(death_camera_key(0.5f, kOther)) == "pull");
    CHECK(std::string(death_camera_key(0.5f, kValus)) == "low");
    CHECK(std::string(death_camera_key(2.5f, kOther)) == "wide");
    CHECK(std::string(death_camera_key(2.5f, kValus)) == "low");
    CHECK(std::string(death_camera_key(5.0f, kOther)) == "follow");
    CHECK(std::string(death_camera_key(5.0f, kValus)) == "wide");
}

TEST_CASE("advance_death_frame clamps the death clip to the selected motion") {
    Scene scene;
    Playback playback;
    playback.clips.push_back(AnimationCpu{"other_death", 10.0f, {}});
    playback.index = 0;
    DeathState death = death_at(0.0f, kOther);
    advance_death_frame(death, 7.0f, playback, 0, CorpseAttachment{}, ClubAttachment{}, scene, 1,
                        kOther);
    CHECK(playback.time == 5.333f);  // Valus's clamp would be 6.166877
    CHECK(death.phase == DeathPhase::Collapse);
}

TEST_CASE("camera_death_pose keys on the selected death camera") {
    Camera base{};
    base.distance = 5.0f;
    const Vec3 entity{0.0f, 0.0f, 0.0f};
    const Vec3 player{3.0f, 0.0f, 0.0f};
    const Camera pull = camera_death_pose(base, entity, player, 1.0f, kOther);
    CHECK(pull.distance == doctest::Approx(7.0f));
    CHECK(pull.pitch == doctest::Approx(-10.0f));
    CHECK(pull.target.y == doctest::Approx(3.0f));
    const Camera low = camera_death_pose(base, entity, player, 2.0f, kOther);
    CHECK(low.distance == doctest::Approx(5.0f));
    CHECK(low.yaw == doctest::Approx(30.0f));
    CHECK(low.pitch == doctest::Approx(-20.0f));
    CHECK(low.target.y == doctest::Approx(4.0f));
    const Camera wide = camera_death_pose(base, entity, player, 4.0f, kOther);
    CHECK(wide.distance == doctest::Approx(15.0f));
    CHECK(wide.yaw == doctest::Approx(60.0f));
}

TEST_CASE("the target camera reads the selected table's limits and defaults") {
    Camera camera{};
    CameraFollow follow;
    CameraTarget target;
    const Vec3 chest{0.0f, 5.0f, 0.0f};
    camera_target_toggle(camera, follow, target, chest, kOther);
    CHECK(camera.distance == doctest::Approx(15.0f));
    CHECK(camera.pitch == doctest::Approx(20.0f));
    camera_target_update(camera, target, chest, 0.0f, 10000.0f, 0.0f, kOther);
    CHECK(camera.pitch == doctest::Approx(60.0f));
    camera_target_update(camera, target, chest, 0.0f, 0.0f, -1000.0f, kOther);
    CHECK(camera.distance == doctest::Approx(40.0f));
    camera_target_update(camera, target, chest, 0.0f, 0.0f, 1000.0f, kOther);
    CHECK(camera.distance == doctest::Approx(10.0f));

    CameraTarget valus;
    camera_target_enter(camera, follow, valus, chest, kValus);
    CHECK(camera.distance == doctest::Approx(26.0f));
    CHECK(camera.pitch == doctest::Approx(10.0f));
}

TEST_CASE("cue_for and missing_cue read the selected table's cues") {
    REQUIRE(cue_for(Sfx::StompStart, kOther) != nullptr);
    CHECK(same(cue_for(Sfx::StompStart, kOther)->clip, "other.telegraph"));
    CHECK(same(cue_for(Sfx::ValusReaction, kOther)->clip, "other.hurt"));
    CHECK(same(cue_for(Sfx::Strike, kOther)->clip, "player.swing"));  // the player's own row
    CHECK(cue_for(Sfx::ClubSwing, kOther) == nullptr);
    CHECK(cue_for(Sfx::ClubSwing, kValus) != nullptr);

    const auto reject = [](const char* banned) {
        return [banned](const char* clip) { return std::strcmp(clip, banned) != 0; };
    };
    CHECK(missing_cue(reject("other.hurt"), kOther) == Sfx::ValusReaction);
    CHECK(missing_cue(reject("valus.hurt"), kOther) == Sfx::Count);
    CHECK(missing_cue(reject("valus.hurt"), kValus) == Sfx::ValusReaction);
}

TEST_CASE("apply_valus_impact deals the selected attack row's damage") {
    PlayerFrame frame;
    Vec3 foot{0.5f, 0.0f, 0.0f};
    frame.jointWorld = [&foot](std::string_view name, Vec3& out) {
        if (name != "Q_FOOT_L") {
            return false;
        }
        out = foot;
        return true;
    };
    const CombatTuning tuning;
    const PlayerState player;

    PlayerCombat direct;
    apply_valus_impact(direct, player, frame, tuning, ValusAttack::StompL, kOther, nullptr,
                       nullptr);
    CHECK(direct.health == doctest::Approx(100.0f - 11.0f));

    foot = Vec3{2.0f, 0.0f, 0.0f};  // outside the 1 m hit, inside the 3 m wave
    PlayerCombat wave;
    apply_valus_impact(wave, player, frame, tuning, ValusAttack::StompL, kOther, nullptr,
                       nullptr);
    CHECK(wave.health == doctest::Approx(100.0f - 5.0f));
}

TEST_CASE("the reactions route over the selected table's N weakpoints") {
    Playback playback;
    for (const char* name : {"stand", "qa_react", "qb_react", "qc_react", "other_body",
                             "other_death", "other_stomp_l", "other_stomp_r", "other_club"}) {
        playback.clips.push_back(AnimationCpu{name, 1.0f, {}});
    }
    const EntityClips clips = resolve_entity_clips(playback, "stand", kOther);
    CHECK(clips.reactions == std::vector<int>{1, 2, 3});
    CHECK(clips.body == 4);
    CHECK(clips.death == 5);
    CHECK(clips.club == 8);

    Encounter encounter;
    encounter.desc = &kOther;
    encounter.health = kOther.maxHealth;
    StepEvents hit;
    hit.damageDealt = 10;
    apply_combat_events(encounter, playback, clips, hit, /*weakpoint=*/2, nullptr);
    CHECK(playback.index == 3);  // the third shell's reaction: Valus has no weakpoint 2
    CHECK(encounter.health == doctest::Approx(40.0f));

    encounter.reactionClip = -1;
    apply_combat_events(encounter, playback, clips, hit, /*weakpoint=*/-1, nullptr);
    CHECK(playback.index == 4);  // off every shell: the body

    hit.damageDealt = 100;
    apply_combat_events(encounter, playback, clips, hit, /*weakpoint=*/2, nullptr);
    CHECK(encounter.defeated);
    CHECK(encounter.deathWeakpoint == 2);
    CHECK(playback.index == 5);
}

TEST_CASE("build_effect_frame roots the hit shell first, then the others, up to death.roots") {
    // Rig 1: one identity joint (the chest at the origin) and three one-vertex
    // shells qa (+x), qb (+y), qc (+z).
    Scene scene;
    scene.rigs.emplace_back();
    RigCpu rig;
    rig.skin.joints.resize(1);
    rig.skin.joints[0].name = "Q_CHEST";
    rig.skin.joints[0].parent = -1;
    rig.skin.joints[0].inverseBind = mat4_identity();
    rig.meshBegin = 0;
    rig.meshEnd = 3;
    scene.rigs.push_back(rig);
    const std::array<Vec3, 3> shells{{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    for (int i = 0; i < 3; ++i) {
        MeshCpu mesh;
        mesh.materialIndex = i;
        mesh.rig = 1;
        Vertex vertex{};
        vertex.px = shells[static_cast<std::size_t>(i)].x;
        vertex.py = shells[static_cast<std::size_t>(i)].y;
        vertex.pz = shells[static_cast<std::size_t>(i)].z;
        vertex.w[0] = 1.0f;
        mesh.vertices.push_back(vertex);
        scene.meshes.push_back(mesh);
        MaterialCpu material{};
        material.name = kOtherWeakpoints[static_cast<std::size_t>(i)].material;
        scene.materials.push_back(material);
    }
    const std::vector<std::vector<Mat4>> palettes{{}, {mat4_identity()}};

    Encounter encounter;
    encounter.desc = &kOtherTwoRoots;
    encounter.deathWeakpoint = 2;
    const EffectFrame two =
        build_effect_frame(scene, 1, palettes, encounter, death_at(1.5f, kOtherTwoRoots));
    REQUIRE(two.active);
    REQUIRE(two.rootCount == 2);
    CHECK(two.roots[0].position.z == doctest::Approx(1.0f));  // qc, the hit shell
    CHECK(two.roots[1].position.x == doctest::Approx(1.0f));  // qa, the first other

    encounter.desc = &kOther;  // one root
    const EffectFrame one =
        build_effect_frame(scene, 1, palettes, encounter, death_at(1.5f, kOther));
    CHECK(one.rootCount == 1);
    CHECK(one.roots[0].position.z == doctest::Approx(1.0f));
    // Outside the selected window there is no frame (Valus's 0.4-3.0 s would be).
    CHECK_FALSE(build_effect_frame(scene, 1, palettes, encounter, death_at(2.5f, kOther)).active);
}

namespace {

// kOther's club with no swing cue (swingSeconds -1, encounter_desc.h's "none").
inline constexpr std::array<AttackDesc, 3> kNoSwingAttacks{{
    kOtherAttacks[0],
    kOtherAttacks[1],
    {"other_club", "Q_CLUB", 20.0f, 0.9f, 1.5f, 0.0f, 9.0f, 1.0f, 1.2f, -1.0f, 21.0f, 0.0f, false},
}};
inline constexpr EncounterDesc kNoSwing = [] {
    EncounterDesc desc = kOther;
    desc.attacks = kNoSwingAttacks;
    return desc;
}();

}  // namespace

TEST_CASE("maybe_club_swing never whooshes a club row whose swing time is -1") {
    Encounter encounter;
    encounter.attack = ValusAttack::Club;
    SfxQueue sfx;
    encounter.desc = &kNoSwing;
    for (const float t : {0.0f, 0.5f, 5.0f}) {
        encounter.attackSeconds = t;
        maybe_club_swing(encounter, &sfx);
        CHECK_FALSE(encounter.attackSwinged);
    }
    encounter.desc = &kOther;  // 0.8 s: fires once at its swing time
    encounter.attackSeconds = 0.5f;
    maybe_club_swing(encounter, &sfx);
    CHECK_FALSE(encounter.attackSwinged);
    encounter.attackSeconds = 0.8f;
    maybe_club_swing(encounter, &sfx);
    CHECK(encounter.attackSwinged);
}
