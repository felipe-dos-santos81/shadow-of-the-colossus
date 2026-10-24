// Gaius wired (S12 Task 9): the pure core of knight_fight — his real exported graph run
// from a scripted ScrWorld, the motion layer, and the bound (Review Focus 5). The fixture
// is the graph export (knight_fixture.h); the clip lengths are knight_root.h's.

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "knight_fight.h"
#include "knight_fixture.h"
#include "knight_root.h"

namespace {

using namespace knight_fixture;
namespace ka = encounters::knight_detail;

// The clips' lengths: knight_root's frames for the clips with a root series, 1 s for
// the rest (the rootless ones are never played).
std::vector<float> lengths(const std::vector<std::string>& names) {
    std::vector<float> out;
    for (const std::string& name : names) {
        float s = 1.0f;
        for (const knight_root::RootSeries& r : knight_root::kRoots) {
            if (name == r.clip) s = static_cast<float>(r.frames - 1) / r.fps;
        }
        out.push_back(s);
    }
    return out;
}

KnightCore loaded_core() {
    KnightCore core;
    const std::vector<std::string> names = export_clips(knight_json());
    const std::vector<std::string> judges = {
        "KNIGHT_LEG_R_ATK", "KNIGHT_LEG_R_ATK_FIX", "KNIGHT_WEAPON_ATK", "KNIGHT_WEAPON_ATK_FIX",
        "KNIGHT_LEG_L_ATK_FIX", "KNIGHT_LEG_L_ATK", "KNIGHT_MOVE_BACK", "KNIGHT_LEG_R_ATK_ATTAIN",
        "KNIGHT_LEG_L_ATK_ATTAIN", "KNIGHT_WEAPON_ATK_ATTAIN", "KNIGHT_WEAPON_ATK2",
        "KNIGHT_WEAPON_ATK2_FIX", "KNIGHT_WEAPON_ATK2_ATTAIN"};
    std::string why;
    const bool ok = knight_core_load(core, knight_json(), names, lengths(names), judges, "knight_a_stand", why);
    INFO(why);
    REQUIRE(ok);
    core.boundCentre = {ka::kArena.boundCentreX, 0.0f, ka::kArena.boundCentreZ};
    core.boundRadius = ka::kArena.boundRadius;
    return core;
}

}  // namespace

TEST_CASE("a scripted ScrWorld (player 30 m ahead, rays clear) drives the graph to the slam within 10 s") {
    KnightCore core = loaded_core();
    knight_core_reset(core, 0.0f);
    CHECK(core.rt.pending == 3);  // the retry start mode (§13.8)
    CHECK(std::string(knight_clip_name(core, core.motion.clip)) == "knight_a_stand");

    // Gaius at the bound centre facing +z on flat ground, the player 30 m ahead on the
    // arena ground, inside the slam's volumes (the judge stub), every ray clear.
    ScrWorld w;
    w.bossPosition = core.boundCentre;
    w.playerPosition = {core.boundCentre.x, 0.0f, core.boundCentre.z + 30.0f};
    w.playerGroundPosition = w.playerPosition;
    w.playerOnArenaGround = true;
    w.groundAt = [](float, float, float& h) {
        h = 0.0f;
        return true;
    };
    w.playerInJudge = [](std::string_view name) { return name.find("WEAPON_ATK") != std::string_view::npos; };

    bool slamRequested = false;
    std::string clip;
    float at = -1.0f;
    for (int frame = 0; frame < 600 && at < 0.0f; ++frame) {  // 10 s at 1/60 s
        const KnightThink t = knight_think(core, w, 1.0f / 60.0f);
        slamRequested = slamRequested ||
                        std::find(t.step.controls.begin(), t.step.controls.end(), scr_knight::kCtrlSlam) !=
                            t.step.controls.end();
        if (slamRequested && t.clipAfter != t.clipBefore) {
            clip = knight_clip_name(core, t.clipAfter);
            at = static_cast<float>(frame + 1) / 60.0f;
        }
    }
    CHECK(slamRequested);
    CHECK(clip == "knight_a_atk1_start");  // the slam's control, through the motion graph
    CHECK(at > 0.0f);
    CHECK(at <= 10.0f);
    CHECK(core.motion.controlMode == 6);  // KNIGHT_ATTACK

    // The chain advances on clip ends (the else edges) to the stuck sword.
    std::vector<std::string> chain{clip};
    for (int frame = 0; frame < 600 && chain.back() != "knight_a_atk1_3"; ++frame) {
        const KnightThink t = knight_think(core, w, 1.0f / 60.0f);
        if (t.clipAfter != t.clipBefore) chain.push_back(knight_clip_name(core, t.clipAfter));
    }
    CHECK(chain == std::vector<std::string>{"knight_a_atk1_start", "knight_a_atk1", "knight_a_atk1_2",
                                            "knight_a_atk1_3"});
}

TEST_CASE("a goal outside the 36 m bound is clamped") {
    const Vec3 c{ka::kArena.boundCentreX, 47.0f, ka::kArena.boundCentreZ};
    const float r = ka::kArena.boundRadius;
    const Vec3 out = knight_clamp_bound({c.x + 30.0f, 50.0f, c.z - 40.0f}, c, r);
    CHECK(std::hypot(out.x - c.x, out.z - c.z) == doctest::Approx(r));
    CHECK(out.y == 50.0f);
    CHECK((out.x - c.x) / (out.z - c.z) == doctest::Approx(30.0f / -40.0f));  // along the same bearing
    const Vec3 in{c.x + 10.0f, 1.0f, c.z + 5.0f};
    const Vec3 kept = knight_clamp_bound(in, c, r);
    CHECK(kept.x == in.x);
    CHECK(kept.z == in.z);

    // In the think: a point goal 60 m out is pulled onto the bound before he steers at it.
    KnightCore core = loaded_core();
    knight_core_reset(core, 0.0f);
    ScrWorld w;
    w.bossPosition = core.boundCentre;
    w.playerPresent = false;
    core.rt.hasGoal = true;
    core.rt.goal = {core.boundCentre.x, 0.0f, core.boundCentre.z + 60.0f};
    knight_think(core, w, 1.0f / 60.0f);
    CHECK(std::hypot(core.rt.goal.x - core.boundCentre.x, core.rt.goal.z - core.boundCentre.z) <=
          doctest::Approx(r));
}
