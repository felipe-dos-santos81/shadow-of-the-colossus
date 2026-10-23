// The door fade machine (S8 Task 6, design D14). Pure: the phase walk, the fade
// ramps, the input lock, the single Load request, the box crossing and the reset.
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "app.h"
#include "door.h"

namespace {

DoorState fading_out() { return step_door({}, 0.0f, true, 3, false); }

// First step in Load (fade 1, locked).
DoorState loading() { return step_door(fading_out(), kDoorFadeOut, false, 0, false); }

DoorState fading_in() { return step_door(loading(), 0.5f, false, 0, true); }

}  // namespace

TEST_CASE("Idle stays Idle without entered, whatever else is passed") {
    DoorState s = step_door({}, 5.0f, false, 7, true);
    CHECK(s.phase == DoorPhase::Idle);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.row == -1);
}

TEST_CASE("entered starts FadeOut, locked, clear, with the entered row, not spending dt") {
    DoorState s = step_door({}, 0.5f, true, 3, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.row == 3);
    // entered and row are not re-read afterwards.
    s = step_door(s, 0.25f, true, 9, false);
    CHECK(s.row == 3);
    CHECK(s.t == doctest::Approx(0.25f));
}

TEST_CASE("FadeOut ramps the fade 0 to 1 over 1.0 s, locked") {
    DoorState s = fading_out();
    s = step_door(s, 0.25f, false, 0, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.fade == doctest::Approx(0.25f));
    CHECK(s.inputLocked);
    s = step_door(s, 0.5f, false, 0, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.fade == doctest::Approx(0.75f));
    CHECK(s.inputLocked);
}

TEST_CASE("FadeOut ends in Load at fade 1, requesting the load exactly once") {
    DoorState before = step_door(fading_out(), 0.99f, false, 0, false);
    REQUIRE(before.phase == DoorPhase::FadeOut);
    CHECK_FALSE(door_wants_load(fading_out(), before));
    DoorState after = step_door(before, 0.01f, false, 0, false);
    CHECK(after.phase == DoorPhase::Load);
    CHECK(after.fade == 1.0f);
    CHECK(after.inputLocked);
    CHECK(after.row == 3);
    CHECK(door_wants_load(before, after));
    DoorState next = step_door(after, 100.0f, false, 0, false);
    CHECK(next.phase == DoorPhase::Load);
    CHECK(next.fade == 1.0f);
    CHECK_FALSE(door_wants_load(after, next));
}

TEST_CASE("one long frame crosses FadeOut but stops at Load, which waits for loadDone") {
    DoorState s = step_door(fading_out(), 1000.0f, false, 0, true);
    CHECK(s.phase == DoorPhase::Load);
    for (int i = 0; i < 10; ++i) s = step_door(s, 10.0f, false, 0, false);
    CHECK(s.phase == DoorPhase::Load);
    CHECK(s.fade == 1.0f);
    s = step_door(s, 10.0f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
}

TEST_CASE("loadDone starts FadeIn at fade 1 without spending dt; it ramps 1 to 0 over 1.5 s") {
    DoorState s = step_door(loading(), 0.5f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
    CHECK(s.fade == 1.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.inputLocked);
    s = step_door(s, 0.75f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
    CHECK(s.fade == doctest::Approx(0.5f));
    CHECK(s.inputLocked);
    s = step_door(s, 0.74f, false, 0, true);
    CHECK(s.phase == DoorPhase::FadeIn);
    CHECK(s.fade > 0.0f);
}

TEST_CASE("FadeIn ends back in Idle, unlocked, clear, without a row") {
    DoorState s = step_door(fading_in(), kDoorFadeIn, false, 0, true);
    CHECK(s.phase == DoorPhase::Idle);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.row == -1);
    // and a second door can start.
    s = step_door(s, 0.0f, true, 5, false);
    CHECK(s.phase == DoorPhase::FadeOut);
    CHECK(s.row == 5);
}

TEST_CASE("the door_wants_load is true only on the FadeOut to Load step across a whole run") {
    DoorState s = step_door({}, 0.0f, true, 1, false);
    int loads = 0;
    for (int i = 0; i < 1000; ++i) {
        DoorState n = step_door(s, 0.01f, false, 0, i >= 300);
        if (door_wants_load(s, n)) ++loads;
        s = n;
    }
    CHECK(loads == 1);
    CHECK(s.phase == DoorPhase::Idle);
}

TEST_CASE("a negative or non-finite dt advances nothing") {
    const float bad[] = {-1.0f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
    for (DoorState base : {step_door(fading_out(), 0.25f, false, 0, false),
                           step_door(fading_in(), 0.25f, false, 0, true)}) {
        for (float dt : bad) {
            DoorState s = step_door(base, dt, false, 0, true);
            if (base.phase == DoorPhase::FadeOut || base.phase == DoorPhase::FadeIn) {
                CHECK(s.phase == base.phase);
                CHECK(s.t == base.t);
                CHECK(s.fade == base.fade);
            }
            CHECK(std::isfinite(s.fade));
        }
    }
}

TEST_CASE("reset from every phase equals the initial state") {
    const DoorState fresh{};
    const DoorState phases[] = {{}, fading_out(), loading(), fading_in()};
    bool seen[4] = {};
    for (const DoorState& p : phases) {
        seen[static_cast<int>(p.phase)] = true;
        (void)p;
        DoorState r = reset_door();
        CHECK(r.phase == fresh.phase);
        CHECK(r.t == fresh.t);
        CHECK(r.fade == fresh.fade);
        CHECK(r.inputLocked == fresh.inputLocked);
        CHECK(r.row == -1);
    }
    CHECK((seen[0] && seen[1] && seen[2] && seen[3]));
    // A reset machine is a working one.
    CHECK(step_door(reset_door(), 0.0f, true, 2, false).phase == DoorPhase::FadeOut);
}

TEST_CASE("box_entered: outside to inside only") {
    const Vec3 mn{0, 0, 0}, mx{2, 2, 2};
    const Vec3 out{5, 1, 1}, in{1, 1, 1}, in2{1.5f, 1, 1};
    CHECK(box_entered(out, in, mn, mx));
    CHECK_FALSE(box_entered(in, in2, mn, mx));  // starting inside (a spawn) never fires
    CHECK_FALSE(box_entered(in, in, mn, mx));
    CHECK_FALSE(box_entered(in, out, mn, mx));  // leaving
    CHECK_FALSE(box_entered(out, Vec3{6, 1, 1}, mn, mx));
    // each axis gates on its own
    CHECK_FALSE(box_entered(out, Vec3{1, 3, 1}, mn, mx));
    CHECK_FALSE(box_entered(out, Vec3{1, 1, -0.1f}, mn, mx));
    CHECK(box_entered(Vec3{1, 5, 1}, in, mn, mx));
    // inclusive bounds
    CHECK(box_entered(out, Vec3{2, 2, 2}, mn, mx));
    CHECK_FALSE(box_entered(Vec3{2, 2, 2}, in, mn, mx));
}

TEST_CASE("door_phase_name spells the enum") {
    CHECK(std::strcmp(door_phase_name(DoorPhase::Idle), "Idle") == 0);
    CHECK(std::strcmp(door_phase_name(DoorPhase::FadeOut), "FadeOut") == 0);
    CHECK(std::strcmp(door_phase_name(DoorPhase::Load), "Load") == 0);
    CHECK(std::strcmp(door_phase_name(DoorPhase::FadeIn), "FadeIn") == 0);
}

TEST_CASE("the door table: entry -> row -> arrival reproduces S8's two doors (R16)") {
    REQUIRE(kS8DoorCount == 2);
    REQUIRE(kDoorCount == 4);  // + S9's corridor and arena doors (rows 2, 3)
    const ride_points::Door& ds = ride_points::kDoorShrine;
    const ride_points::Door& dp = ride_points::kDoorPlain;
    // The shrine's front exit: a step into its box on the shrine.
    const Vec3 shrineOut{0.0f, 1.0f, 43.0f}, shrineIn{0.0f, 1.0f, 45.0f};
    const int r0 = door_entered_row(StageId::Shrine, shrineOut, shrineIn);
    REQUIRE(r0 >= 0);
    CHECK(kDoors[r0].from == StageId::Shrine);
    CHECK(kDoors[r0].door == &ds);
    CHECK(kDoors[r0].to == StageId::Plain);
    // A box belongs to its stage: the same step elsewhere enters nothing.
    CHECK(door_entered_row(StageId::Plain, shrineOut, shrineIn) == -1);
    CHECK(door_entered_row(StageId::Arena, shrineOut, shrineIn) == -1);
    CHECK(door_entered_row(StageId::Shrine, shrineIn, shrineIn) == -1);  // a spawn inside
    // The plain's shrine steps.
    const Vec3 plainOut{301.0f, 0.0f, -250.0f}, plainIn{301.0f, 0.0f, -255.0f};
    const int r1 = door_entered_row(StageId::Plain, plainOut, plainIn);
    REQUIRE(r1 >= 0);
    CHECK(r1 != r0);
    CHECK(kDoors[r1].door == &dp);
    CHECK(kDoors[r1].to == StageId::Shrine);

    // The row recorded at entry is the arrival, only in Load and only on its target.
    DoorState s = step_door({}, 0.0f, true, r0, false);
    CHECK(s.row == r0);
    CHECK(arriving_door(s, StageId::Plain) == nullptr);  // still FadeOut
    s = step_door(s, kDoorFadeOut, false, -1, false);
    REQUIRE(s.phase == DoorPhase::Load);
    const DoorRow* a = arriving_door(s, StageId::Plain);
    REQUIRE(a == &kDoors[r0]);
    CHECK(a->door->arrive[0] == 301.0f);  // S8: the plain arrival from the shrine
    CHECK(a->door->arrive[1] == -2.333f);
    CHECK(a->door->arrive[2] == -245.0f);
    CHECK(a->door->arriveYawDeg == 0.0f);
    CHECK(arriving_door(s, StageId::Shrine) == nullptr);
    CHECK(arriving_door(s, StageId::Arena) == nullptr);

    s = step_door(step_door({}, 0.0f, true, r1, false), kDoorFadeOut, false, -1, false);
    a = arriving_door(s, StageId::Shrine);
    REQUIRE(a == &kDoors[r1]);
    CHECK(a->door->arrive[0] == 0.0f);  // S8: the shrine arrival from the plain
    CHECK(a->door->arrive[1] == 9.79f);
    CHECK(a->door->arrive[2] == 34.0f);
    CHECK(a->door->arriveYawDeg == 180.0f);

    // An out-of-range row never arrives.
    DoorState bad = s;
    bad.row = kDoorCount;
    CHECK(arriving_door(bad, StageId::Shrine) == nullptr);
    bad.row = -1;
    CHECK(arriving_door(bad, StageId::Shrine) == nullptr);
}

TEST_CASE("door_stage_start keeps only a door arrival (R16: R on a door-entry frame)") {
    // The door entered on the frame R ended the plain: FadeOut, locked; the arena
    // that loads next starts with the machine idle and the input free.
    const DoorState entering = step_door({}, 0.0f, true, 1, false);
    REQUIRE(entering.inputLocked);
    DoorState a = door_stage_start(entering, StageId::Arena);
    CHECK(a.phase == DoorPhase::Idle);
    CHECK_FALSE(a.inputLocked);
    CHECK(a.fade == 0.0f);
    CHECK(a.row == -1);
    // A Load whose row leads elsewhere is not an arrival either.
    const DoorState load = step_door(step_door({}, 0.0f, true, 0, false), kDoorFadeOut, false,
                                     -1, false);
    CHECK(door_stage_start(load, StageId::Arena).phase == DoorPhase::Idle);
    CHECK(door_stage_start(load, StageId::Shrine).phase == DoorPhase::Idle);
    // The arrival keeps the machine as it is (the stage completes Load itself).
    const DoorState kept = door_stage_start(load, StageId::Plain);
    CHECK(kept.phase == DoorPhase::Load);
    CHECK(kept.row == 0);
    CHECK(kept.inputLocked);
}

namespace {

bool inside_xz(const ride_points::Door& d, float x, float z) {
    return x >= d.min[0] && x <= d.max[0] && z >= d.min[2] && z <= d.max[2];
}

}  // namespace

TEST_CASE("S9: the corridor and arena doors are rows 2 and 3, live only with the S9 rows") {
    const ride_points::Door& dc = road_points::kDoorCorridor;
    const ride_points::Door& da = road_points::kDoorArena;
    // The corridor's dead end on the road (the Plain stage) -> the arena.
    const Vec3 corridorOut{304.0f, 3.0f, 284.0f}, corridorIn{304.0f, 3.0f, 288.0f};
    const int rc = door_entered_row(StageId::Plain, corridorOut, corridorIn);
    REQUIRE(rc == 2);
    CHECK(kDoors[rc].from == StageId::Plain);
    CHECK(kDoors[rc].door == &dc);
    CHECK(kDoors[rc].to == StageId::Arena);
    // The arena's way back -> the road.
    const Vec3 arenaOut{305.0f, 29.0f, 313.0f}, arenaIn{305.0f, 29.0f, 309.0f};
    const int ra = door_entered_row(StageId::Arena, arenaOut, arenaIn);
    REQUIRE(ra == 3);
    CHECK(kDoors[ra].from == StageId::Arena);
    CHECK(kDoors[ra].door == &da);
    CHECK(kDoors[ra].to == StageId::Plain);
    // A frozen (S8) run passes the S8 rows only: neither S9 door enters.
    CHECK(door_entered_row(StageId::Plain, corridorOut, corridorIn, kS8DoorCount) == -1);
    CHECK(door_entered_row(StageId::Arena, arenaOut, arenaIn, kS8DoorCount) == -1);
    // S8's own doors still enter under the S8 rows.
    CHECK(door_entered_row(StageId::Plain, Vec3{301.0f, 0.0f, -250.0f},
                           Vec3{301.0f, 0.0f, -255.0f}, kS8DoorCount) == 1);

    // Two doors lead into the Plain: the row says which (no doorFrom), and each
    // arrives at its own spawn.
    DoorState s = step_door(step_door({}, 0.0f, true, ra, false), kDoorFadeOut, false, -1, false);
    const DoorRow* a = arriving_door(s, StageId::Plain);
    REQUIRE(a == &kDoors[ra]);
    CHECK(a->from == StageId::Arena);
    CHECK(a->door->arrive[0] == 304.0f);
    CHECK(a->door->arrive[2] == 282.0f);
    CHECK(a->door->arriveYawDeg == 180.0f);
    s = step_door(step_door({}, 0.0f, true, rc, false), kDoorFadeOut, false, -1, false);
    a = arriving_door(s, StageId::Arena);
    REQUIRE(a == &kDoors[rc]);
    CHECK(a->door->arrive[0] == 353.0f);  // the arena spawn
    CHECK(a->door->arrive[1] == 27.951f);
    CHECK(a->door->arrive[2] == 345.0f);
    CHECK(a->door->arriveYawDeg == 49.24f);
}

TEST_CASE("S9: every box has positive extent; no arrival lands inside a door of its stage") {
    for (int i = 0; i < kDoorCount; ++i) {
        const ride_points::Door& d = *kDoors[i].door;
        for (int k = 0; k < 3; ++k) CHECK(d.max[k] > d.min[k]);
    }
    // A spawn inside a box of the stage it lands on would re-fire (in x/z — the
    // floor snap moves only y).
    for (int i = 0; i < kDoorCount; ++i) {
        const ride_points::Door& from = *kDoors[i].door;
        for (int j = 0; j < kDoorCount; ++j) {
            if (kDoors[j].from != kDoors[i].to) continue;
            CAPTURE(i);
            CAPTURE(j);
            CHECK_FALSE(inside_xz(*kDoors[j].door, from.arrive[0], from.arrive[2]));
        }
    }
}
