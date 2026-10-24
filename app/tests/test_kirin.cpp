// S13: cases land with the task that fills the module (docs/superpowers/plans/2026-10-24-s13-phaedra.md).
// Task 11 (root half): the generated kirin_root.h. Header-only numbers: no library needed.
// Task 13 (table half): Phaedra's EncounterDesc against her baked glb (fails, never skips, without
// SOTC_LIB or `make bake-kirin`) and her grip regions (tools/re/clb/boss_regions.py, run once per
// test run into the build directory; fails without the game tree).
#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "asset_io.h"
#include "encounters/registry.h"
#include "grab.h"
#include "json_reader.h"
#include "kirin_root.h"
#include "knight_fixture.h"  // read_file, kRepoRoot

namespace {

using encounters::kKirin;
namespace k = encounters::kirin_detail;

constexpr const char* kGlb = "models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.glb";

std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    return library.is_relative() ? knight_fixture::kRepoRoot / library : library;
}

struct Rig {
    std::set<std::string> joints;
    std::set<std::string> materials;
    std::map<std::string, float> clips;  // name -> duration (s)
};

const Rig& kirin_rig() {
    static const Rig rig = [] {
        const Scene scene = load_scene(test_library(), kGlb);
        if (scene.rigs.empty()) throw std::runtime_error(std::string(kGlb) + " has no rig");
        Rig out;
        for (const JointCpu& joint : scene.rigs[0].skin.joints) out.joints.insert(joint.name);
        for (const MaterialCpu& material : scene.materials) out.materials.insert(material.name);
        for (const AnimationCpu& clip : scene.rigs[0].animations) out.clips[clip.name] = clip.duration;
        return out;
    }();
    return rig;
}

// Her grip regions per bone (`{"Bone_HEAD": ["0x10"], ...}`), generated once per run.
const std::map<std::string, std::set<int>>& grip_regions() {
    static const std::map<std::string, std::set<int>> census = [] {
        namespace fs = std::filesystem;
        const fs::path out = fs::path(SOTC_TEST_BUILD_DIR) / "fixtures" / "kirin_regions.json";
        fs::create_directories(out.parent_path());
        const std::string cmd = "python3 -I '" +
                                (knight_fixture::kRepoRoot / "tools/re/clb/boss_regions.py").string() +
                                "' col/kirin_a_coli.clb skel/kirin_a.skb > '" + out.string() + "'";
        if (std::system(cmd.c_str()) != 0) {
            throw std::runtime_error("boss_regions.py failed (is SOTC_GAME_ROOT set?): " + cmd);
        }
        const Json json = json_parse(knight_fixture::read_file(out));
        std::map<std::string, std::set<int>> regions;
        for (const auto& [bone, codes] : json.object) {
            for (const Json& code : codes.array) regions[bone].insert(std::stoi(code.string, nullptr, 16));
        }
        return regions;
    }();
    return census;
}

bool leg_bone(std::string_view name) {
    for (const char* part : {"MOMO", "SUNE", "TOE", "UDE", "JOWAN", "IKFoot"}) {
        if (name.find(part) != std::string_view::npos) return true;
    }
    return false;
}

bool tail_enabled(std::string_view clip, int frame) {
    bool any = false;
    for (int i : {k::B::TailStart, k::B::TailLoop, k::B::TailEnd}) any |= band_enabled(kKirin.bands[i], clip, frame);
    return any;
}

const kirin_root::RootSeries& root_of(std::string_view clip) {
    for (const kirin_root::RootSeries& r : kirin_root::kRoots)
        if (clip == r.clip) return r;
    FAIL("no root series for " << clip);
    return kirin_root::kRoots[0];
}

const kirin_root::TurnHeading& heading_of(std::string_view clip) {
    for (const kirin_root::TurnHeading& h : kirin_root::kTurnHeading)
        if (clip == h.clip) return h;
    FAIL("no turn heading for " << clip);
    return kirin_root::kTurnHeading[0];
}

}  // namespace

TEST_CASE("kirin_root.h: the walk travels 5.00 m/s, the turns end at +40.00 and -29.98 deg") {
    const kirin_root::RootSeries& walk = root_of("kirin_a_walk");
    CHECK(walk.loop);
    const float seconds = static_cast<float>(walk.frames - 1) / walk.fps;
    const float* end = walk.xz[walk.frames - 1];
    CHECK(std::hypot(end[0], end[1]) / seconds == doctest::Approx(5.00f).epsilon(0.01));
    CHECK(end[1] > 0.0f);  // forward is +z

    const kirin_root::TurnHeading& turnL = heading_of("kirin_a_turn_l");
    CHECK(turnL.deg[0] == doctest::Approx(0.0f));
    CHECK(turnL.deg[turnL.frames - 1] == doctest::Approx(40.00f).epsilon(0.1 / 40.0));
    const kirin_root::TurnHeading& walkTurnR = heading_of("kirin_a_walk_turn_r");
    CHECK(walkTurnR.deg[walkTurnR.frames - 1] == doctest::Approx(-29.98f).epsilon(0.1 / 29.98));
}

TEST_CASE("kirin_root.h: 49 clips carry a root, the 25 rootless and boot are flagged, the death drop") {
    CHECK(std::size(kirin_root::kRoots) == 49);
    CHECK(std::size(kirin_root::kNoRoot) == 25);
    CHECK(std::size(kirin_root::kUnusable) == 1);
    CHECK(std::string_view(kirin_root::kUnusable[0]) == "kirin_a_boot");
    for (const kirin_root::RootSeries& r : kirin_root::kRoots) CHECK(std::string_view(r.clip) != "kirin_a_boot");
    bool death = false;
    for (const char* name : kirin_root::kNoRoot) death |= std::string_view(name) == "kirin_a_death";
    CHECK(death);
    CHECK(kirin_root::kDeathRoot.kFrom == doctest::Approx(9.86f));
    CHECK(kirin_root::kDeathRoot.kTo == doctest::Approx(2.44f));
}

// --- Task 13: the table half ---------------------------------------------------

static_assert(encounters::kirin_links_valid());
static_assert(kKirin.bands.size() == k::B::Count);
static_assert(kKirin.weakpoints.size() == k::W::Count);
static_assert(kKirin.behaviour.states.empty());  // her exported graph drives her
static_assert(kKirin.attacks.empty());           // her damage is the sidecar's
static_assert(kKirin.props.empty());             // no corpse swap: death_loop is held
static_assert(kKirin.cues.music == nullptr);     // silent

TEST_CASE("kirin: the registry selects her table by the manifest's id") {
    CHECK(find_encounter("kirin") == &kKirin);
    CHECK(std::string_view(kKirin.id) == "kirin");
}

TEST_CASE("kirin: every joint her table names resolves in her baked glb") {
    const Rig& rig = kirin_rig();
    CHECK(rig.joints.size() == 38);
    std::vector<const char*> names{kKirin.joints.chest, kKirin.joints.footL, kKirin.joints.footR};
    for (const ClimbBand& band : kKirin.bands) {
        names.push_back(band.boneA);
        names.push_back(band.boneB);
    }
    for (const CapsuleDesc& capsule : kKirin.capsules) {
        names.push_back(capsule.from);
        names.push_back(capsule.to);
    }
    for (const char* name : names) {
        CAPTURE(name);
        CHECK(rig.joints.count(name) == 1);
    }
}

TEST_CASE("kirin: every material and clip her table names is in her glb") {
    const Rig& rig = kirin_rig();
    CHECK(rig.clips.size() == 75);
    for (const WeakpointDesc& weakpoint : kKirin.weakpoints) {
        CAPTURE(weakpoint.material);
        CHECK(rig.materials.count(weakpoint.material) == 1);
    }
    std::vector<const char*> clips{kKirin.bodyReactionClip, kKirin.death.clip, k::kDeathLoopClip};
    for (const WeakpointDesc& weakpoint : kKirin.weakpoints) {
        if (weakpoint.reactionClip != nullptr) clips.push_back(weakpoint.reactionClip);
    }
    for (const ClimbBand& band : kKirin.bands) {
        if (band.enableClip != nullptr) clips.push_back(band.enableClip);
    }
    for (const char* name : clips) {
        CAPTURE(name);
        CHECK(rig.clips.count(name) == 1);
    }
    CHECK(rig.clips.at(kKirin.death.clip) == doctest::Approx(kKirin.death.motionSeconds).epsilon(1e-4));
    // Each window's frames lie inside its clip (15 fps).
    for (const ClimbBand& band : kKirin.bands) {
        if (band.enableClip == nullptr) continue;
        CAPTURE(band.enableClip);
        const int last = static_cast<int>(std::lround(rig.clips.at(band.enableClip) * k::kClipFps));
        CHECK(band.enableFrom <= last);
        CHECK(band.enableTo <= last);
    }
    // The death drop is the generated header's (D12).
    CHECK(k::kDeathRootY.kFrom == doctest::Approx(9.86f));
    CHECK(k::kDeathRootY.kTo == doctest::Approx(2.44f));
}

TEST_CASE("kirin: the band chain tail -> back -> withers -> neck low -> neck high -> head") {
    namespace b = k::B;
    const auto in_range = [](int i) { return i == ClimbBandId::None || (i >= 0 && i < b::Count); };
    for (const ClimbBand& band : kKirin.bands) {
        CAPTURE(band.name);
        CHECK(in_range(band.up));
        CHECK(in_range(band.down));
        CHECK(in_range(band.branch));
        CHECK(in_range(band.carryTo));
        CHECK((band.weakpoint == -1 || (band.weakpoint >= 0 && band.weakpoint < k::W::Count)));
    }
    for (int tail : {b::TailStart, b::TailLoop, b::TailEnd}) {
        CHECK(kKirin.bands[tail].up == b::Back);
        CHECK_FALSE(kKirin.bands[tail].horizontal);  // hung
    }
    const int chain[] = {b::Back, b::Withers, b::NeckLow, b::NeckHigh, b::Head};
    for (std::size_t i = 0; i + 1 < std::size(chain); ++i) {
        CHECK(kKirin.bands[chain[i]].up == chain[i + 1]);
        CHECK(kKirin.bands[chain[i + 1]].down == chain[i]);
    }
    CHECK(kKirin.bands[b::Head].up == ClimbBandId::None);
    CHECK(kKirin.bands[b::Back].horizontal);
    CHECK(kKirin.bands[b::Withers].horizontal);
    CHECK_FALSE(kKirin.bands[b::NeckLow].horizontal);
    CHECK(kKirin.bands[b::Head].horizontal);
    CHECK(kKirin.bands[b::Head].weakpoint == k::W::HeadSigil);
    CHECK(kKirin.bands[b::NeckLow].weakpoint == k::W::NeckTrigger);
}

TEST_CASE("kirin: each band's region is one of its bones' grip regions; no band on a leg") {
    const auto& census = grip_regions();
    CHECK(census.at("Bone_HEAD").count(0x10) == 1);  // the census itself is sane
    for (int i = 0; i < k::B::Count; ++i) {
        const ClimbBand& band = kKirin.bands[i];
        CAPTURE(band.name);
        CAPTURE(band.boneA);
        CAPTURE(band.boneB);
        std::set<int> regions;
        for (const char* bone : {band.boneA, band.boneB}) {
            if (const auto it = census.find(bone); it != census.end()) regions.insert(it->second.begin(), it->second.end());
        }
        CHECK(regions.count(k::kBandRegion[i]) == 1);
        CHECK_FALSE(leg_bone(band.boneA));
        CHECK_FALSE(leg_bone(band.boneB));
    }
    // Her legs carry no grip face (data): what "leg" means above.
    for (const auto& [bone, regions] : census) {
        if (leg_bone(bone)) {
            CAPTURE(bone);
            CHECK(regions.empty());
        }
    }
}

TEST_CASE("kirin: the strike targets — the head sigil's pool of 160, the neck trigger deals none") {
    CHECK(kKirin.maxHealth == doctest::Approx(160.0f));
    CHECK(k::kWeakpointRegion[k::W::HeadSigil] == 0x10);
    CHECK(k::kWeakpointPool[k::W::HeadSigil] == doctest::Approx(160.0f));
    CHECK(k::kWeakpointRegion[k::W::NeckTrigger] == 0x20);
    CHECK(k::kWeakpointPool[k::W::NeckTrigger] == 0.0f);
    CHECK(k::kTriggerHitKind == 0);  // a sword hit
    CHECK(std::string_view(kKirin.weakpoints[k::W::NeckTrigger].material) == "trigger_01");
}

TEST_CASE("kirin: the tail is grabbed only while she crouches (watch_*)") {
    for (int f = 0; f < 31; ++f) {
        CAPTURE(f);
        CHECK(tail_enabled("kirin_a_watch_loop", f));
        CHECK_FALSE(tail_enabled("kirin_a_walk", f));
        CHECK_FALSE(tail_enabled("kirin_a_stand", f));
    }
    CHECK_FALSE(tail_enabled("kirin_a_watch_start", 0));   // still standing
    CHECK(tail_enabled("kirin_a_watch_start", 45));        // down
    CHECK(tail_enabled("kirin_a_watch_end", 0));           // still down
    CHECK_FALSE(tail_enabled("kirin_a_watch_end", 40));    // up again
    for (int tail : {k::B::TailStart, k::B::TailLoop, k::B::TailEnd}) {
        const ClimbBand& band = kKirin.bands[tail];
        CHECK(band.grabEntryLowEnd);
        CHECK(band.carryFraction == doctest::Approx(1.0f));  // a window closing never drops the rider
        CHECK(std::string_view(band.boneA) == "Bone_TAIL");  // the low end: the tail tip
    }
}

TEST_CASE("kirin: the backstop box over the north road's end; the start and her spawn are outside") {
    CHECK(k::in_backstop(990.0f, 61.0f, -17.0f));
    CHECK(k::in_backstop(986.0f, 60.34f, -19.0f));
    CHECK(k::in_backstop(994.0f, 61.69f, -15.0f));
    CHECK_FALSE(k::in_backstop(k::kArena.playerStartX, k::kArena.playerStartY, k::kArena.playerStartZ));
    CHECK_FALSE(k::in_backstop(k::kArena.spawnX, k::kArena.spawnGroundY, k::kArena.spawnZ));
    CHECK_FALSE(k::in_backstop(990.0f, 61.0f, -10.0f));  // the road before its end
}
