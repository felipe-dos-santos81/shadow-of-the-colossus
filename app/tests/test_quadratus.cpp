// S11 Task 9: Quadratus's EncounterDesc against his baked glb and the mode
// graph. The library-dependent cases fail (never skip) without SOTC_LIB or
// without his bake (`make bake-quadratus`; promoted to the base out/library at
// S11's merge, ruling A16). No case reads out/re/stage: the manifest case
// writes its own fixture.

#include <doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "asset_io.h"
#include "encounters/registry.h"
#include "manifest.h"

namespace {

using encounters::kQuadratus;
namespace q = encounters::quadratus_detail;

constexpr const char* kGlb = "models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.glb";

const std::filesystem::path kRepoRoot =
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();

// Same contract as test_asset_io.cpp's helper: fail loudly when unset.
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    return library.is_relative() ? kRepoRoot / library : library;
}

struct Rig {
    std::set<std::string> joints;
    std::set<std::string> materials;
    std::map<std::string, float> clips;  // name -> duration (s)
};

const Rig& quadratus_rig() {
    static const Rig rig = [] {
        const Scene scene = load_scene(test_library(), kGlb);
        if (scene.rigs.empty()) {
            throw std::runtime_error(std::string(kGlb) + " has no rig");
        }
        Rig out;
        for (const JointCpu& joint : scene.rigs[0].skin.joints) {
            out.joints.insert(joint.name);
        }
        for (const MaterialCpu& material : scene.materials) {
            out.materials.insert(material.name);
        }
        for (const AnimationCpu& clip : scene.rigs[0].animations) {
            out.clips[clip.name] = clip.duration;
        }
        return out;
    }();
    return rig;
}

float clip_seconds(const char* name) {
    const Rig& rig = quadratus_rig();
    const auto it = rig.clips.find(name);
    if (it == rig.clips.end()) {
        throw std::runtime_error(std::string("no clip ") + name);
    }
    return it->second;
}

// The behaviour's clip table as durations, by kClips index (from the glb).
std::vector<float> state_clip_durations() {
    std::vector<float> durations;
    for (const char* name : q::kClips) {
        durations.push_back(clip_seconds(name));
    }
    return durations;
}

BehaviourRuntime in_state(int state) {
    BehaviourRuntime rt;
    rt.state = state;
    return rt;
}

// One kneel, timed on the glb's clip lengths: the hit -> start -> loop until
// 20 s -> end -> mode 14's rest until 30 s -> stand. Mid-kneel and in the rest
// a hoof hit is ignored and a sigil hit still damages.
void check_kneel(int weakpoint, int start, int loop, int end, int rest) {
    const BehaviourDesc& b = kQuadratus.behaviour;
    const std::vector<float> durations = state_clip_durations();

    const BehaviourHit hit = behaviour_hit(b, in_state(q::S::Walk), weakpoint);
    REQUIRE(hit.kind == GateKind::Transition);
    CHECK(hit.runtime.state == start);

    BehaviourRuntime rt = step_behaviour(b, hit.runtime, 2.1f, durations);
    CHECK(rt.state == loop);
    for (int hoof = q::W::HoofRL; hoof <= q::W::HoofFR; ++hoof) {
        CHECK(behaviour_hit(b, rt, hoof).kind == GateKind::Ignore);
    }
    CHECK(behaviour_hit(b, rt, q::W::BackSigil).kind == GateKind::Damage);
    rt = step_behaviour(b, rt, 17.8f, durations);  // 19.9 s after the hit
    CHECK(rt.state == loop);
    rt = step_behaviour(b, rt, 0.2f, durations);  // 20.1 s: the end clip
    CHECK(rt.state == end);
    CHECK(behaviour_hit(b, rt, weakpoint).kind == GateKind::Ignore);
    rt = step_behaviour(b, rt, 9.8f, durations);  // 29.9 s: mode 14's rest
    CHECK(rt.state == rest);
    CHECK(behaviour_hit(b, rt, q::W::HoofRR).kind == GateKind::Ignore);
    CHECK(behaviour_hit(b, rt, q::W::HeadSigil).kind == GateKind::Damage);
    rt = step_behaviour(b, rt, 0.2f, durations);  // 30.1 s
    CHECK(rt.state == q::S::Stand);
}

}  // namespace

TEST_CASE("a rear-left hoof hit kneels him: 20 s of sprain, 10 s of rest, then stand") {
    check_kneel(q::W::HoofRL, q::S::DownBLStart, q::S::DownBLLoop, q::S::DownBLEnd,
                q::S::RiseReturnB);
}

TEST_CASE("a front-right hoof hit kneels him: 20 s of sprain, 10 s of rest, then stand") {
    check_kneel(q::W::HoofFR, q::S::DownFRStart, q::S::DownFRLoop, q::S::DownFREnd,
                q::S::RiseReturnF);
}

TEST_CASE("Quadratus's gates, state by state, against the mode graph") {
    namespace s = q::S;
    // Literal (not read back from the table): the kneels (no re-hit), mode 14's
    // rest, mode 19's swings and death ignore the soles; every other state
    // kneels him on the struck hoof.
    const std::set<int> noHoofGate{
        s::DownBLStart, s::DownBLLoop, s::DownBLEnd, s::DownBRStart, s::DownBRLoop, s::DownBREnd,
        s::DownFLStart, s::DownFLLoop, s::DownFLEnd, s::DownFRStart, s::DownFRLoop, s::DownFREnd,
        s::RiseReturnB, s::RiseReturnF, s::RefuseHead, s::RefuseShoulder, s::RefuseArmL,
        s::RefuseArmR, s::RefuseLegL, s::RefuseLegR, s::Death};
    const std::pair<int, int> kneelOf[] = {{q::W::HoofRL, s::DownBLStart},
                                           {q::W::HoofRR, s::DownBRStart},
                                           {q::W::HoofFL, s::DownFLStart},
                                           {q::W::HoofFR, s::DownFRStart}};
    const BehaviourDesc& b = kQuadratus.behaviour;
    for (int state = 0; state < s::Count; ++state) {
        CAPTURE(state);
        for (const auto& [hoof, kneel] : kneelOf) {
            const WeakpointGate gate = behaviour_gate(b, state, hoof);
            if (noHoofGate.count(state) != 0) {
                CHECK(gate.kind == GateKind::Ignore);
            } else {
                CHECK(gate.kind == GateKind::Transition);
                CHECK(gate.target == kneel);
            }
        }
        const GateKind sigil = state == s::Death ? GateKind::Ignore : GateKind::Damage;
        CHECK(behaviour_gate(b, state, q::W::BackSigil).kind == sigil);
        CHECK(behaviour_gate(b, state, q::W::HeadSigil).kind == sigil);
    }
}

TEST_CASE("every state but the loops (stand, walk) and death runs back to stand or walk") {
    const BehaviourDesc& b = kQuadratus.behaviour;
    const std::vector<float> durations = state_clip_durations();
    for (int state = 0; state < q::S::Count; ++state) {
        if (state == q::S::Stand || state == q::S::Walk || state == q::S::Death) {
            continue;
        }
        CAPTURE(state);
        BehaviourRuntime rt = in_state(state);
        for (int i = 0; i < 400 && rt.state != q::S::Stand && rt.state != q::S::Walk; ++i) {
            rt = step_behaviour(b, rt, 0.1f, durations);  // up to 40 s
        }
        CHECK((rt.state == q::S::Stand || rt.state == q::S::Walk));
    }
    // The loops hold, and Death is entered only by the app (HP 0): no row leads
    // there, and it holds its last frame.
    for (const BehaviourStateDesc& st : b.states) {
        CHECK(st.clipEndNext != q::S::Death);
        CHECK(st.timerNext != q::S::Death);
    }
    CHECK(q::kHoldLastFrameState == q::S::Death);
    CHECK(step_behaviour(b, in_state(q::S::Death), 60.0f, durations).state == q::S::Death);
}

static_assert(behaviour_valid(kQuadratus.behaviour));
static_assert(encounters::quadratus_links_valid());
static_assert(kQuadratus.behaviour.states.size() == q::S::Count);
static_assert(q::kClips.size() == q::C::Count);
static_assert(kQuadratus.bands.size() == 10);
static_assert(kQuadratus.weakpoints.size() == 6);
static_assert(kQuadratus.capsules.size() == 17);
static_assert(kQuadratus.attacks.size() == 2);
static_assert(kQuadratus.props.empty());
static_assert(kQuadratus.behaviour.actions.empty());
static_assert(kQuadratus.cues.music == nullptr);

TEST_CASE("every joint Quadratus's table names resolves in his baked glb") {
    const Rig& rig = quadratus_rig();
    CHECK(rig.joints.size() == 34);
    std::vector<const char*> names{kQuadratus.joints.chest, kQuadratus.joints.footL,
                                   kQuadratus.joints.footR};
    for (const ClimbBand& band : kQuadratus.bands) {
        names.push_back(band.boneA);
        names.push_back(band.boneB);
    }
    for (const CapsuleDesc& capsule : kQuadratus.capsules) {
        names.push_back(capsule.from);
        names.push_back(capsule.to);
    }
    for (const AttackDesc& attack : kQuadratus.attacks) {
        names.push_back(attack.damageJoint);
    }
    for (const q::HoofDesc& hoof : q::kHooves) {
        names.push_back(hoof.joint);
    }
    for (const char* name : names) {
        CAPTURE(name);
        CHECK(rig.joints.count(name) == 1);
    }
}

TEST_CASE("every weakpoint material and every clip Quadratus's table names is in his glb") {
    const Rig& rig = quadratus_rig();
    for (const WeakpointDesc& weakpoint : kQuadratus.weakpoints) {
        CAPTURE(weakpoint.material);
        CHECK(rig.materials.count(weakpoint.material) == 1);
    }
    std::vector<const char*> clips(q::kClips.begin(), q::kClips.end());
    for (const WeakpointDesc& weakpoint : kQuadratus.weakpoints) {
        clips.push_back(weakpoint.reactionClip);
    }
    clips.push_back(kQuadratus.bodyReactionClip);
    clips.push_back(kQuadratus.death.clip);
    for (const AttackDesc& attack : kQuadratus.attacks) {
        clips.push_back(attack.clip);
    }
    for (const char* name : clips) {
        CAPTURE(name);
        REQUIRE(rig.clips.count(name) == 1);
        CHECK(rig.clips.at(name) > 0.0f);
    }
    // The measured durations the table derives its timers from.
    CHECK(clip_seconds(kQuadratus.death.clip) == doctest::Approx(kQuadratus.death.motionSeconds).epsilon(1e-4));
    for (const char* start : {"boss_a_down_b_l_start", "boss_a_down_b_r_start",
                              "boss_a_down_f_l_start", "boss_a_down_f_r_start"}) {
        CHECK(clip_seconds(start) == doctest::Approx(q::kDownStartSeconds).epsilon(1e-4));
    }
    CHECK(clip_seconds("boss_a_down_b_l_end") == doctest::Approx(q::kDownBEndSeconds).epsilon(1e-4));
    CHECK(clip_seconds("boss_a_down_b_r_end") == doctest::Approx(q::kDownBEndSeconds).epsilon(1e-4));
    CHECK(clip_seconds("boss_a_down_f_l_end") == doctest::Approx(q::kDownFEndSeconds).epsilon(1e-4));
    CHECK(clip_seconds("boss_a_down_f_r_end") == doctest::Approx(q::kDownFEndSeconds).epsilon(1e-4));
}

TEST_CASE("both stomp chains run start -> attack -> landing -> stop -> stand on his clip lengths") {
    const BehaviourDesc& b = kQuadratus.behaviour;
    const std::vector<float> durations = state_clip_durations();
    for (const q::AttackChain& chain : q::kAttackChains) {
        CAPTURE(chain.first);
        float total = 0.0f;
        for (int s = chain.first; s <= chain.last; ++s) {
            total += durations[static_cast<std::size_t>(b.states[static_cast<std::size_t>(s)].clip)];
        }
        CHECK(total == doctest::Approx(2.1006f + 1.8335f + 0.0333f + 3.4667f).epsilon(1e-3));
        BehaviourRuntime rt = behaviour_enter(b, in_state(q::S::Stand), chain.first);
        rt = step_behaviour(b, rt, total - 0.05f, durations);
        CHECK(rt.state == chain.last);
        rt = step_behaviour(b, rt, 0.1f, durations);
        CHECK(rt.state == q::S::Stand);
    }
}

TEST_CASE("an arena manifest naming quadratus selects his table; an unknown id has none") {
    // The real out/re/stage/arena_02.manifest is checked by Task 10's selftest;
    // a unit test reads its own fixture through the same parser and registry.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("sotc-quadratus-fixture-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const auto write = [&dir](const std::string& encounterLine) {
        const std::filesystem::path path = dir / "arena_02.manifest";
        std::ofstream(path) << "instance models/a.glb 0 0 0 0 0 0 1 1 1 1\n"
                            << encounterLine << "\n"
                            << "entity " << kGlb
                            << " 240 -96.741 -925 0 0.188087 0 0.982152 1 1 1 boss_a_stand\n"
                            << "spawn 273 -98.308 -842 -158.317708\n";
        return path;
    };
    const SceneManifest manifest = parse_manifest(write("encounter quadratus"), true);
    CHECK(manifest.encounter == "quadratus");
    CHECK(find_encounter(manifest.encounter) == &kQuadratus);
    REQUIRE(manifest.entities.size() == 1);
    CHECK(manifest.entities[0].placement.glbRelPath == kGlb);
    CHECK(manifest.entities[0].clip == q::kClips[q::C::Stand]);

    // stage_setup's "unknown encounter" error is find_encounter's nullptr.
    CHECK(find_encounter(parse_manifest(write("encounter quadratu"), true).encounter) == nullptr);
    std::filesystem::remove_all(dir);
}
