// The objanim event consumer (S12 Task 8, design D6): synthetic sidecars written in
// the converter's shape (`sotc-objanim-events/1`, glb frame) for the damage, effect
// and judge rules, plus Gaius's real sidecar and glb for the footstep spawn and the
// R4 pose-space anchor. The library cases fail (never skip) without SOTC_LIB or
// without his bake (`make bake-knight` into the branch's view).

#include <doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim.h"
#include "asset_io.h"
#include "objanim_events.h"

namespace {

constexpr const char* kGlb = "models/shadow/characters/colossus_03_knight/_cmn/colossus_03_knight.glb";
constexpr const char* kSidecar =
    "models/shadow/characters/colossus_03_knight/_cmn/colossus_03_knight.events.json";

const std::filesystem::path kRepoRoot =
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();

// Same contract as test_knight.cpp's helper: fail loudly when unset.
std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    return library.is_relative() ? kRepoRoot / library : library;
}

// Writes `json` to a temp file and loads it; the load must succeed.
EventsFile load_fixture(const std::string& json) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("sotc-events-fixture-" + std::to_string(::getpid()) + ".json");
    std::ofstream(path) << json;
    EventsFile file;
    std::string error;
    const bool ok = events_load(path.string(), file, error);
    std::filesystem::remove(path);
    INFO(error);
    REQUIRE(ok);
    return file;
}

// A sidecar with one clip `c` (30 fps, `frames`) holding `damage` and `effects` rows.
std::string sidecar(const std::string& damage, const std::string& effects = "", int frames = 2,
                    const std::string& judge = "") {
    return R"({"format": "sotc-objanim-events/1", "skeleton": "test_a", "frame": "glb", "clips": {"c": {"fps": 30, "frames": )" +
           std::to_string(frames) + R"(, "judge": [)" + judge + R"(], "damage": [)" + damage +
           R"(], "effects": [)" + effects + R"(]}}})";
}

// A static damage row.
std::string damage_row(const std::string& name, const std::string& shape, const std::string& parent,
                       float amount, float size, const std::string& pos = "[0, 0, 0]") {
    const std::string s = std::to_string(size);
    return R"({"name": ")" + name + R"(", "type": 13, "shape": ")" + shape + R"(", "parent": )" + parent +
           R"(, "windows": [[0, 1]], "def": {"amount": )" + std::to_string(amount) +
           R"(, "reaction": 4}, "static": {"pos": )" + pos + R"(, "quat": [0, 0, 0, 1], "size": [)" + s + ", " +
           s + ", " + s + "]}}";
}

Mat4 translate(float x, float y, float z) {
    Mat4 m = mat4_identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

// A pose whose every joint sits at `now` this frame and `prev` last frame.
PoseView hand_pose(Vec3 prev, Vec3 now, int entity = 7) {
    PoseView pose;
    pose.entity = entity;
    pose.now.joint = [now](std::string_view, Mat4& out) { return out = translate(now.x, now.y, now.z), true; };
    pose.prev.joint = [prev](std::string_view, Mat4& out) { return out = translate(prev.x, prev.y, prev.z), true; };
    return pose;
}

EventsClipState at(float frame, int instance = 0) { return EventsClipState{0, frame, instance, 0}; }

const char* kHand = R"({"bone": "Bone_HAND_A_R"})";

}  // namespace

TEST_CASE("a box moving 11.7 m in one frame across a stationary capsule hits; one stopping 1 m short does not") {
    // A 1 m box (half extent 0.5) on the hand; the player's capsule (r 0.35) at the origin.
    const EventsFile file = load_fixture(sidecar(damage_row("SLAM", "box", kHand, 40, 0.5f)));
    const PlayerView player{{0.0f, 0.0f, 0.0f}};

    // Across: x -6 -> 5.7 (11.7 m). Neither end overlaps; only the sweep finds the hit.
    const std::vector<DamageHit> across =
        events_damage(file, at(1), at(0), hand_pose({-6.0f, 1.0f, 0.0f}, {5.7f, 1.0f, 0.0f}), player);
    REQUIRE(across.size() == 1);
    CHECK(across[0].name == "SLAM");
    CHECK(across[0].amount == doctest::Approx(40.0f));
    CHECK(events_damage(file, at(1), at(1), hand_pose({5.7f, 1.0f, 0.0f}, {5.7f, 1.0f, 0.0f}), player).empty());

    // Short: x -13.55 -> -1.85 (11.7 m); the box face stops at -1.35, 1 m from the capsule's -0.35.
    CHECK(events_damage(file, at(1), at(0), hand_pose({-13.55f, 1.0f, 0.0f}, {-1.85f, 1.0f, 0.0f}), player)
              .empty());
}

TEST_CASE("no hit on a player standing on the entity") {
    const EventsFile file = load_fixture(sidecar(damage_row("SHOCK2", "box", kHand, 5, 4.0f)));
    const PoseView pose = hand_pose({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 7);

    CHECK(events_damage(file, at(1), at(0), pose, PlayerView{{0.0f, 0.0f, 0.0f}, -1, 7}).empty());
    CHECK(events_damage(file, at(1), at(0), pose, PlayerView{{0.0f, 0.0f, 0.0f}, 7, -1}).empty());
    CHECK(events_damage(file, at(1), at(0), pose, PlayerView{{0.0f, 0.0f, 0.0f}, -1, 3}).size() == 1);  // another body
    CHECK(events_damage(file, at(1), at(0), pose, PlayerView{{0.0f, 0.0f, 0.0f}}).size() == 1);
}

TEST_CASE("a row hits once per clip instance") {
    const EventsFile file = load_fixture(sidecar(damage_row("SHOCK", "sphere", R"("entity")", 5, 3.0f)));
    const PoseView pose = hand_pose({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    const PlayerView player{{1.0f, 0.0f, 0.0f}};

    EventsClipState prev = at(0), now = at(0);
    std::vector<DamageHit> hits = events_damage(file, now, prev, pose, player);
    REQUIRE(hits.size() == 1);
    for (const DamageHit& h : hits) now.hit |= uint64_t{1} << h.row;

    prev = now;
    now.frame = 1.0f;
    CHECK(events_damage(file, now, prev, pose, player).empty());  // still inside, already hit

    prev = now;
    now = at(0.0f, 1);  // the clip loops: a new instance
    CHECK(events_damage(file, now, prev, pose, player).size() == 1);

    CHECK(events_damage(file, at(2.5f), at(1), pose, player).empty());  // outside the window [0, 1]
}

TEST_CASE("CORE and IMPACT overlapping → the higher amount") {
    // Quadratus's MAM_ATK spheres (recon objanim findings §4): CORE r 6.5 / 50, IMPACT r 8.71 / 25.
    const EventsFile file = load_fixture(sidecar(damage_row("IMPACT", "sphere", R"("entity")", 25, 8.71f) + ", " +
                                                 damage_row("CORE", "sphere", R"("entity")", 50, 6.5f)));
    const PoseView pose = hand_pose({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});

    const std::vector<DamageHit> both = events_damage(file, at(0), at(0), pose, PlayerView{{3.0f, 0.0f, 0.0f}});
    REQUIRE(both.size() == 2);
    CHECK(both.front().name == "CORE");
    CHECK(both.front().amount == doctest::Approx(50.0f));

    const std::vector<DamageHit> rim = events_damage(file, at(0), at(0), pose, PlayerView{{8.0f, 0.0f, 0.0f}});
    REQUIRE(rim.size() == 1);
    CHECK(rim.front().name == "IMPACT");
}

TEST_CASE("a start crossing on a loop wrap fires once") {
    const std::string effects =
        R"({"name": "FIRST", "type": 29, "parent": "entity", "events": [[0, 1]], "static": {"pos": [0, 0, 0], "quat": [0, 0, 0, 1], "vec": [0, 0, 0]}},)"
        R"({"name": "LAST", "type": 29, "parent": "entity", "events": [[45, 45]], "static": {"pos": [0, 0, 0], "quat": [0, 0, 0, 1], "vec": [0, 0, 0]}})";
    const EventsFile file = load_fixture(sidecar("", effects, 46));
    const PoseView pose = hand_pose({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});

    // The wrap itself: 44.5 -> 0.5 crosses LAST (45) and FIRST (0), each once.
    std::vector<EventFire> wrap = events_fired(file, at(0.5f, 1), at(44.5f, 0), pose);
    REQUIRE(wrap.size() == 2);
    CHECK(std::count_if(wrap.begin(), wrap.end(), [](const EventFire& f) { return f.name == "FIRST"; }) == 1);
    CHECK(std::count_if(wrap.begin(), wrap.end(), [](const EventFire& f) { return f.name == "LAST"; }) == 1);
    CHECK(events_fired(file, at(1.5f, 1), at(0.5f, 1), pose).empty());

    // Three loops at 0.7 frames a step: each start fires once per loop.
    EventsClipState prev{-1, 0.0f, 0, 0};
    int first = 0, last = 0;
    for (int step = 0; step <= static_cast<int>(3 * 46 / 0.7f); ++step) {
        const float t = step * 0.7f;
        const EventsClipState now = at(std::fmod(t, 46.0f), static_cast<int>(t / 46.0f));
        for (const EventFire& f : events_fired(file, now, prev, pose)) (f.name == "FIRST" ? first : last) += 1;
        prev = now;
    }
    CHECK(first == 3);
    CHECK(last == 3);
}

TEST_CASE("knight_walk FOOT_R fires at frame 40 and spawns knight_walk_dmg") {
    EventsFile file;
    std::string error;
    REQUIRE_MESSAGE(events_load((test_library() / kSidecar).string(), file, error), error);
    const int walk = events_clip(file, "knight_a_walk");
    REQUIRE(walk >= 0);

    // His right foot 10 m to the side, the left foot opposite.
    PoseView pose;
    pose.entity = 2;
    pose.now.joint = [](std::string_view name, Mat4& out) {
        out = translate(name.find("_R") != std::string_view::npos ? 10.0f : -10.0f, 0.0f, 0.0f);
        return true;
    };
    pose.prev.joint = pose.now.joint;

    std::vector<int> footR;
    EventsSpawnRunner runner;
    EventsClipState prev{-1, 0.0f, 0, 0};
    for (int f = 0; f <= 40; ++f) {
        const EventsClipState now{walk, static_cast<float>(f), 0, 0};
        const std::vector<EventFire> fired = events_fired(file, now, prev, pose);
        for (const EventFire& e : fired) {
            if (e.name == "KNIGHT_FOOT_R") footR.push_back(f);
        }
        runner.fire(file, fired);
        prev = now;
    }
    CHECK(footR == std::vector<int>{40});
    // FOOT_L (f17) and FOOT_R (f40) each spawned one knight_walk_dmg.
    REQUIRE(runner.active.size() == 2);
    CHECK(file.spawned[runner.active.back().spawned].name == "knight_walk_dmg");
    CHECK(runner.active.back().at.m[12] == doctest::Approx(10.0f));  // at the right foot

    // The spawned stomp (window f1–18) hits a player at the right foot once, and never
    // a player standing on him.
    runner.active.erase(runner.active.begin());  // drop FOOT_L's, it is long past
    runner.active.back().frame = 0.0f;
    const PlayerView player{{10.0f, 0.0f, 0.0f}};
    runner.step(file, 1.0f / 30.0f);
    EventsSpawnRunner standing = runner;
    CHECK(standing.damage(file, PlayerView{{10.0f, 0.0f, 0.0f}, -1, 2}, 2).empty());
    const std::vector<DamageHit> hits = runner.damage(file, player, 2);
    REQUIRE(!hits.empty());
    CHECK(hits.front().amount > 0.0f);
    runner.step(file, 1.0f / 30.0f);
    CHECK(runner.damage(file, player, 2).empty());
    runner.step(file, 3.0f);  // 61 frames at 30 fps: done
    CHECK(runner.active.empty());
}

TEST_CASE("the judge box test uses half extents") {
    // Quadratus's t46 judge boxes (recon validate_stomp, glb frame, entity parent): the
    // measured stomp impact (-7.42, 24.15) is inside LEG_R_ATK, not LEG_L_ATK. With full
    // extents (x ±40) LEG_L_ATK would hold it too.
    const std::string judge =
        R"({"name": "MAMMOTH_LEG_R_ATK", "shape": "box", "parent": "entity", "static": {"pos": [-18.0875, 2.2019, 37.2787], "quat": [0, 0, 0, 1], "size": [20, 15, 30]}},)"
        R"({"name": "MAMMOTH_LEG_L_ATK", "shape": "box", "parent": "entity", "static": {"pos": [18.0875, 2.2019, 37.2787], "quat": [0, 0, 0, 1], "size": [20, 15, 30]}})";
    const EventsFile file = load_fixture(sidecar("", "", 61, judge));
    const PoseView pose = hand_pose({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    const PlayerView impact{{-7.42f, 0.0f, 24.15f}};

    CHECK(events_player_in_judge(file, "MAMMOTH_LEG_R_ATK", at(28), pose, impact));
    CHECK_FALSE(events_player_in_judge(file, "MAMMOTH_LEG_L_ATK", at(28), pose, impact));
    CHECK_FALSE(events_player_in_judge(file, "MAMMOTH_LEG_R_ATK", at(28), pose, PlayerView{{-40.0f, 0.0f, 24.15f}}));
    CHECK_FALSE(events_player_in_judge(file, "NO_SUCH_VOLUME", at(28), pose, impact));
}

TEST_CASE("Gaius's default judge sectors open forward; ATTAIN sits 30 m ahead") {
    EventsFile file;
    std::string error;
    REQUIRE_MESSAGE(events_load((test_library() / kSidecar).string(), file, error), error);
    PoseView pose = hand_pose({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    pose.now.hipY = 11.9f;  // the skelton rows sit at y -11.9: the ground under an 11.9 m hip
    const EventsClipState none{-1, 0.0f, 0, 0};
    const auto in = [&](const char* name, float x, float z) {
        return events_player_in_judge(file, name, none, pose, PlayerView{{x, 0.0f, z}});
    };
    // R4: WEAPON_ATK_ATTAIN (r 5) -> (0, 0.76 above the hip line, 30): ahead, not behind.
    CHECK(in("KNIGHT_WEAPON_ATK_ATTAIN", 0.0f, 30.0f));
    CHECK_FALSE(in("KNIGHT_WEAPON_ATK_ATTAIN", 0.0f, -30.0f));
    // LEG_R_ATK (80° sector, inner 0.25 of r 23) opens ahead-right (his right is the glb's -x).
    CHECK(in("KNIGHT_LEG_R_ATK", -10.0f, 10.0f));
    CHECK_FALSE(in("KNIGHT_LEG_R_ATK", 10.0f, 10.0f));
    CHECK(in("KNIGHT_LEG_L_ATK", 10.0f, 10.0f));
    // WEAPON_ATK (90°, inner 0.45 of r 55) opens forward from 6 m ahead: 40 m ahead in, behind out.
    CHECK(in("KNIGHT_WEAPON_ATK", 0.0f, 40.0f));
    CHECK_FALSE(in("KNIGHT_WEAPON_ATK", 0.0f, -40.0f));
}

TEST_CASE("R4 anchor: the slam box at atk1 f1 reaches the measured blade tip") {
    EventsFile file;
    std::string error;
    REQUIRE_MESSAGE(events_load((test_library() / kSidecar).string(), file, error), error);
    const int clip = events_clip(file, "knight_a_atk1");
    REQUIRE(clip >= 0);
    const auto row = std::find_if(file.clips[clip].damage.begin(), file.clips[clip].damage.end(),
                                  [](const EventsVolume& v) { return v.name == "KNIGHT_SWORD_ATK1_DMG"; });
    REQUIRE(row != file.clips[clip].damage.end());

    const Scene scene = load_scene(test_library(), kGlb);
    const auto anim = std::find_if(scene.rigs[0].animations.begin(), scene.rigs[0].animations.end(),
                                   [](const AnimationCpu& a) { return a.name == "knight_a_atk1"; });
    REQUIRE(anim != scene.rigs[0].animations.end());
    const std::vector<Mat4> palette = animation_pose(scene, 0, *anim, 1.0f / 30.0f);
    PoseFrame frame;
    frame.joint = [&](std::string_view name, Mat4& out) {
        return rig_bone_world(scene, 0, palette, std::string(name), out);
    };

    const PosedVolume box = events_pose(*row, 1.0f, frame, {});
    // The box's long axis is its x (half 8.158); the far end is the +z one.
    const Vec3 a{box.centre.x + box.axis[0].x * box.size.x, box.centre.y + box.axis[0].y * box.size.x,
                 box.centre.z + box.axis[0].z * box.size.x};
    const Vec3 b{box.centre.x - box.axis[0].x * box.size.x, box.centre.y - box.axis[0].y * box.size.x,
                 box.centre.z - box.axis[0].z * box.size.x};
    const Vec3 far = a.z > b.z ? a : b;
    MESSAGE("centre (" << box.centre.x << ", " << box.centre.y << ", " << box.centre.z << ") far end (" << far.x
                       << ", " << far.y << ", " << far.z << ")");
    // R4 (root-relative frame at atk1 f1): span z 14.48 … 27.47, far end y -1.51,
    // 0.09 m short of the measured tip z 27.56. Unmirrored, the centre sits 6.6 m off.
    CHECK(far.z == doctest::Approx(27.47f).epsilon(0.005));
    CHECK(far.y == doctest::Approx(-1.51f).epsilon(0.05));
    CHECK(std::fmin(a.z, b.z) == doctest::Approx(14.48f).epsilon(0.01));
    // Root-free: far end - centre in R4's chain frame, (-0.06, -1.51, 31.63) - (-0.27, 3.42, 25.14).
    CHECK(far.x - box.centre.x == doctest::Approx(0.21f).epsilon(0.1));
    CHECK(far.y - box.centre.y == doctest::Approx(-4.93f).epsilon(0.01));
    CHECK(far.z - box.centre.z == doctest::Approx(6.49f).epsilon(0.01));
}
