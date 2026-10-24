// S12 Task 6: Gaius's EncounterDesc against his baked glb, the D7 band chain,
// and the generated knight_root.h. The library-dependent cases fail (never
// skip) without SOTC_LIB or without his bake (`make bake-knight` into the
// branch's view; promoted to the base library at S12's merge, D11). No case
// reads out/re/stage: the manifest case writes its own fixture.

#include <doctest.h>

#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "asset_io.h"
#include "encounters/registry.h"
#include "knight_root.h"
#include "manifest.h"

namespace {

using encounters::kKnight;
namespace k = encounters::knight_detail;

constexpr const char* kGlb = "models/shadow/characters/colossus_03_knight/_cmn/colossus_03_knight.glb";

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

const Rig& knight_rig() {
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

const knight_root::RootSeries& root_of(std::string_view clip) {
    for (const knight_root::RootSeries& series : knight_root::kRoots) {
        if (clip == series.clip) {
            return series;
        }
    }
    throw std::runtime_error("no root series for " + std::string(clip));
}

const knight_root::TurnHeading& heading_of(std::string_view clip) {
    for (const knight_root::TurnHeading& heading : knight_root::kTurnHeading) {
        if (clip == heading.clip) {
            return heading;
        }
    }
    throw std::runtime_error("no turn heading for " + std::string(clip));
}

}  // namespace

static_assert(encounters::knight_links_valid());
static_assert(kKnight.bands.size() == k::B::Count);
static_assert(kKnight.weakpoints.size() == k::W::Count);
static_assert(kKnight.behaviour.states.empty());  // the exported graph replaces it (D1)
static_assert(kKnight.behaviour.actions.empty());
static_assert(kKnight.behaviour.gates.empty());
static_assert(kKnight.attacks.empty());           // his damage is the events sidecar's (D6)
static_assert(kKnight.props.empty());             // the sword is his mesh; the corpse stays (D12)
static_assert(kKnight.cues.music == nullptr);     // silent (D10)

TEST_CASE("every joint Gaius's table names resolves in his baked glb") {
    const Rig& rig = knight_rig();
    CHECK(rig.joints.size() == 35);
    std::vector<const char*> names{kKnight.joints.chest, kKnight.joints.footL, kKnight.joints.footR};
    for (const ClimbBand& band : kKnight.bands) {
        names.push_back(band.boneA);
        names.push_back(band.boneB);
    }
    for (const CapsuleDesc& capsule : kKnight.capsules) {
        names.push_back(capsule.from);
        names.push_back(capsule.to);
    }
    for (const char* name : names) {
        CAPTURE(name);
        CHECK(rig.joints.count(name) == 1);
    }
}

TEST_CASE("every weakpoint material and every clip Gaius's table names is in his glb") {
    const Rig& rig = knight_rig();
    CHECK(rig.clips.size() == 62);
    for (const WeakpointDesc& weakpoint : kKnight.weakpoints) {
        CAPTURE(weakpoint.material);
        CHECK(rig.materials.count(weakpoint.material) == 1);
    }
    std::vector<const char*> clips{kKnight.bodyReactionClip, kKnight.death.clip};
    for (const WeakpointDesc& weakpoint : kKnight.weakpoints) {
        clips.push_back(weakpoint.reactionClip);
    }
    for (const ClimbBand& band : kKnight.bands) {
        if (band.enableClip != nullptr) {
            clips.push_back(band.enableClip);
        }
    }
    for (const char* name : clips) {
        CAPTURE(name);
        REQUIRE(rig.clips.count(name) == 1);
        CHECK(rig.clips.at(name) > 0.0f);
    }
    CHECK(rig.clips.at(kKnight.death.clip) ==
          doctest::Approx(kKnight.death.motionSeconds).epsilon(1e-4));
    // The blade window lies inside atk1_3 (95 frames at 30 fps, R3).
    const ClimbBand& blade = kKnight.bands[k::B::Blade];
    CHECK(rig.clips.at(blade.enableClip) * 30.0f == doctest::Approx(94.0f).epsilon(1e-3));
}

TEST_CASE("knight_root.h covers his 62 clips: a root series or flagged rootless, every name in the glb") {
    const Rig& rig = knight_rig();
    std::set<std::string> covered;
    for (const knight_root::RootSeries& series : knight_root::kRoots) {
        CAPTURE(series.clip);
        CHECK(rig.clips.count(series.clip) == 1);
        CHECK(covered.insert(series.clip).second);
        CHECK(series.frames >= 2);
    }
    for (const char* clip : knight_root::kNoRoot) {
        CAPTURE(clip);
        CHECK(rig.clips.count(clip) == 1);
        CHECK(covered.insert(clip).second);
    }
    CHECK(std::size(knight_root::kNoRoot) == 14);
    CHECK(covered.size() == 62);
    for (const knight_root::TurnHeading& heading : knight_root::kTurnHeading) {
        CHECK(rig.clips.count(heading.clip) == 1);
    }
}

TEST_CASE("the band chain: blade -> forearm -> upper arm -> shoulder -> neck -> head, links in range") {
    namespace b = k::B;
    const auto in_range = [](int i) { return i == ClimbBandId::None || (i >= 0 && i < b::Count); };
    for (const ClimbBand& band : kKnight.bands) {
        CAPTURE(band.name);
        CHECK(in_range(band.up));
        CHECK(in_range(band.down));
        CHECK(in_range(band.branch));
        CHECK(in_range(band.carryTo));
        CHECK((band.weakpoint == -1 || (band.weakpoint >= 0 && band.weakpoint < k::W::Count)));
    }
    const int chain[] = {b::Blade, b::Forearm, b::UpperArm, b::Shoulder, b::Neck, b::Head};
    for (std::size_t i = 0; i + 1 < std::size(chain); ++i) {
        CHECK(kKnight.bands[chain[i]].up == chain[i + 1]);
        CHECK(kKnight.bands[chain[i + 1]].down == chain[i]);
    }
    CHECK(kKnight.bands[b::Blade].down == ClimbBandId::None);
    CHECK(kKnight.bands[b::Head].up == ClimbBandId::None);
    // D7: walk / hang / walk / hang / walk after the walked blade.
    CHECK(kKnight.bands[b::Blade].horizontal);
    CHECK(kKnight.bands[b::Forearm].horizontal);
    CHECK_FALSE(kKnight.bands[b::UpperArm].horizontal);
    CHECK(kKnight.bands[b::Shoulder].horizontal);
    CHECK_FALSE(kKnight.bands[b::Neck].horizontal);
    CHECK(kKnight.bands[b::Head].horizontal);
    CHECK(kKnight.bands[b::Head].weakpoint == k::W::HeadSigil);
}

TEST_CASE("the blade band carries the R3 window, the 5 m/s walk, the low-end grab and the carry") {
    const ClimbBand& blade = kKnight.bands[k::B::Blade];
    REQUIRE(blade.enableClip != nullptr);
    CHECK(std::string_view(blade.enableClip) == "knight_a_atk1_3");
    CHECK(blade.enableFrom == 9);   // after the fling volume's f0–8
    CHECK(blade.enableTo == 75);    // KNIGHT_PULLSWD fires at 75.0 (half-open)
    CHECK(blade.walkSpeed == doctest::Approx(5.0f));
    CHECK(blade.carryFraction == doctest::Approx(0.4f));
    CHECK(blade.carryTo == k::B::Forearm);
    CHECK(blade.grabEntryLowEnd);
    // Low end (tip) -> hilt, so bandT >= 1 - carryFraction is the part near his fist.
    CHECK(std::string_view(blade.boneA) == "OptBone_IK_atk");
    CHECK(std::string_view(blade.boneB) == "OptBone_sword");
    CHECK(blade.radius < blade.radiusB);
    // Only the blade has a window; the forearm it hands over to is always there.
    for (int i = k::B::Forearm; i < k::B::Count; ++i) {
        CHECK(kKnight.bands[i].enableClip == nullptr);
        CHECK(kKnight.bands[i].carryTo == ClimbBandId::None);
    }
}

TEST_CASE("knight_root.h: the walk travels 6.667 m/s, turn_r ends at -89.98 deg") {
    const knight_root::RootSeries& walk = root_of("knight_a_walk");
    CHECK(walk.loop);
    const float seconds = static_cast<float>(walk.frames - 1) / walk.fps;
    const float* end = walk.xz[walk.frames - 1];
    const float speed = std::hypot(end[0], end[1]) / seconds;
    CHECK(speed == doctest::Approx(6.667f).epsilon(0.01));
    CHECK(end[1] > 0.0f);  // forward is +z

    const knight_root::TurnHeading& turnR = heading_of("knight_a_turn_r");
    CHECK(turnR.deg[0] == doctest::Approx(0.0f));
    CHECK(turnR.deg[turnR.frames - 1] == doctest::Approx(-89.98f).epsilon(0.1 / 89.98));
    const knight_root::TurnHeading& turnL = heading_of("knight_a_turn_l");
    CHECK(turnL.deg[turnL.frames - 1] == doctest::Approx(90.01f).epsilon(0.1 / 90.01));
}

TEST_CASE("an arena manifest naming knight selects his table") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("sotc-knight-fixture-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "arena_03.manifest";
    std::ofstream(path) << "instance models/a.glb 0 0 0 0 0 0 1 1 1 1\n"
                        << "backdrop models/w.glb 0 0 0 0 0 0 1 1 1 1\n"
                        << "encounter knight\n"
                        << "entity " << kGlb << " -300 47.657 -1420 0 -0.011 0 0.99994 1 1 1 knight_a_stand\n"
                        << "spawn -301 43.33 -1375 178.73\n";
    const SceneManifest manifest = parse_manifest(path, true);
    CHECK(manifest.encounter == "knight");
    CHECK(find_encounter(manifest.encounter) == &kKnight);
    CHECK(manifest.backdrops.size() == 1);
    std::filesystem::remove_all(dir);
}
