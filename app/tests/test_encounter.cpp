// S11 Task 7: Valus's EncounterDesc equals the literals the code carried before
// the table existed (copied verbatim below as the oracle: grab.cpp's band table,
// collision.cpp's capsules, valus.h's attack constants, death.h/camera.h's death
// bundle, audio_map.h's cue rows, encounter_rt.h's clip/glb names, camera.h's
// target camera, app.h's max health), field by field and order by order.

#include <doctest.h>

#include <cstddef>
#include <cstring>
#include <iterator>
#include <type_traits>

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
    CHECK(find_encounter("quadratus") == nullptr);
    CHECK(find_encounter("") == nullptr);
    CHECK(find_encounter("Valus") == nullptr);
}
