// S13: cases land with the task that fills the module (docs/superpowers/plans/2026-10-24-s13-phaedra.md).
// Task 11 (root half): the generated kirin_root.h. Header-only numbers: no library needed.
// Task 13 adds the table half.
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <string_view>

#include "encounters/kirin.h"  // compiled here until Task 13 fills it
#include "kirin_root.h"

namespace {

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
