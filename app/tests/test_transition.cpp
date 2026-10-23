// The return-to-shrine phase machine (S7 Task 4, design §8.4). Pure: no scene,
// no window and no clock; the phase walk, the fade ramps, the input lock, the
// single Load request and the reset.
#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "transition.h"

namespace {

ReturnTimings timings() {
    ReturnTimings tm;
    tm.hold = 2.0f;
    tm.fadeOut = 1.0f;
    tm.fadeIn = 1.5f;
    tm.wake = 0.5f;
    return tm;
}

ReturnState holding(const ReturnTimings& tm) { return step_return({}, 0.0f, tm, true, false); }

// Walks to the first step in Load (fade 1, locked).
ReturnState loading(const ReturnTimings& tm) {
    ReturnState s = holding(tm);
    s = step_return(s, tm.hold, tm, true, false);
    s = step_return(s, tm.fadeOut, tm, true, false);
    return s;
}

}  // namespace

TEST_CASE("Idle stays Idle until the death is done") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return({}, 5.0f, tm, false, true);
    CHECK(s.phase == ReturnPhase::Idle);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.fade == 0.0f);
    CHECK(s.t == 0.0f);
}

TEST_CASE("deathDone enters Hold, unlocked and clear, without spending that frame's dt") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return({}, 0.5f, tm, true, false);
    CHECK(s.phase == ReturnPhase::Hold);
    CHECK(s.t == 0.0f);
    CHECK(s.fade == 0.0f);
    CHECK_FALSE(s.inputLocked);
    // deathDone is not re-read afterwards.
    s = step_return(s, 0.5f, tm, false, false);
    CHECK(s.phase == ReturnPhase::Hold);
    CHECK(s.t == doctest::Approx(0.5f));
}

TEST_CASE("Hold lasts `hold`, then FadeOut locks input and ramps the fade linearly") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return(holding(tm), 1.99f, tm, false, false);
    CHECK(s.phase == ReturnPhase::Hold);
    CHECK_FALSE(s.inputLocked);
    s = step_return(s, 0.01f + 0.25f, tm, false, false);
    CHECK(s.phase == ReturnPhase::FadeOut);
    CHECK(s.inputLocked);
    CHECK(s.t == doctest::Approx(0.25f).epsilon(1e-3));
    CHECK(s.fade == doctest::Approx(0.25f).epsilon(1e-3));
    s = step_return(s, 0.5f, tm, false, false);
    CHECK(s.phase == ReturnPhase::FadeOut);
    CHECK(s.fade == doctest::Approx(0.75f).epsilon(1e-3));
}

TEST_CASE("FadeOut ends in Load at fade 1, requesting the load exactly once") {
    const ReturnTimings tm = timings();
    ReturnState before = step_return(holding(tm), tm.hold, tm, false, false);
    REQUIRE(before.phase == ReturnPhase::FadeOut);
    CHECK_FALSE(return_wants_load(holding(tm), before));
    ReturnState after = step_return(before, tm.fadeOut, tm, false, false);
    CHECK(after.phase == ReturnPhase::Load);
    CHECK(after.fade == 1.0f);
    CHECK(after.inputLocked);
    CHECK(return_wants_load(before, after));

    // Staying in Load never asks again, whatever the dt, and never leaves.
    ReturnState next = step_return(after, 100.0f, tm, false, false);
    CHECK(next.phase == ReturnPhase::Load);
    CHECK(next.fade == 1.0f);
    CHECK_FALSE(return_wants_load(after, next));
}

TEST_CASE("one long frame crosses Hold and FadeOut but stops at Load") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return(holding(tm), 1000.0f, tm, false, true);
    CHECK(s.phase == ReturnPhase::Load);
    CHECK(s.fade == 1.0f);
    CHECK(s.inputLocked);
    // Even with loadDone already true, Load is only left by the next step.
    s = step_return(s, 1000.0f, tm, false, true);
    CHECK(s.phase == ReturnPhase::FadeIn);
}

TEST_CASE("loadDone starts FadeIn, which ramps the fade 1 to 0") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return(loading(tm), 0.1f, tm, false, true);
    CHECK(s.phase == ReturnPhase::FadeIn);
    CHECK(s.fade == 1.0f);
    CHECK(s.t == 0.0f);
    CHECK(s.inputLocked);
    s = step_return(s, 0.75f, tm, false, true);
    CHECK(s.phase == ReturnPhase::FadeIn);
    CHECK(s.fade == doctest::Approx(0.5f));
}

TEST_CASE("FadeIn ends in Wake (locked, clear) and Wake ends in Free") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return(loading(tm), 0.0f, tm, false, true);
    s = step_return(s, tm.fadeIn, tm, false, true);
    CHECK(s.phase == ReturnPhase::Wake);
    CHECK(s.fade == 0.0f);
    CHECK(s.inputLocked);
    s = step_return(s, 0.25f, tm, false, true);
    CHECK(s.phase == ReturnPhase::Wake);
    CHECK(s.t == doctest::Approx(0.25f));
    s = step_return(s, 0.25f, tm, false, true);
    CHECK(s.phase == ReturnPhase::Free);
    CHECK_FALSE(s.inputLocked);
    CHECK(s.fade == 0.0f);
}

TEST_CASE("a zero wake passes straight from FadeIn to Free") {
    ReturnTimings tm = timings();
    tm.wake = 0.0f;
    ReturnState s = step_return(loading(tm), 0.0f, tm, false, true);
    s = step_return(s, tm.fadeIn, tm, false, true);
    CHECK(s.phase == ReturnPhase::Free);
    CHECK_FALSE(s.inputLocked);
    CHECK(ReturnTimings{}.wake == 0.0f);
}

TEST_CASE("Free is terminal and every further step returns it unchanged") {
    const ReturnTimings tm = timings();
    ReturnState s = step_return(loading(tm), 0.0f, tm, false, true);
    s = step_return(s, 1000.0f, tm, false, true);
    REQUIRE(s.phase == ReturnPhase::Free);
    ReturnState n = step_return(s, 1000.0f, tm, true, true);
    CHECK(n.phase == ReturnPhase::Free);
    CHECK(n.t == s.t);
    CHECK(n.fade == s.fade);
    CHECK_FALSE(n.inputLocked);
}

TEST_CASE("a negative or non-finite dt advances nothing") {
    const ReturnTimings tm = timings();
    const float bad[] = {-1.0f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
    ReturnState base = step_return(holding(tm), 2.25f, tm, false, false);  // FadeOut, t 0.25
    REQUIRE(base.phase == ReturnPhase::FadeOut);
    for (float dt : bad) {
        ReturnState s = step_return(base, dt, tm, false, false);
        CHECK(s.phase == base.phase);
        CHECK(s.t == base.t);
        CHECK(s.fade == base.fade);
        CHECK(std::isfinite(s.fade));
    }
}

TEST_CASE("reset from any phase equals the initial state") {
    const ReturnTimings tm = timings();
    ReturnState s{};
    const ReturnState fresh{};
    int seen = 0;
    ReturnPhase last = ReturnPhase::Free;
    bool first = true;
    // Walk the whole machine in small steps, resetting at every state on the way.
    for (int i = 0; i < 2000; ++i) {
        ReturnState r = reset_return();
        CHECK(r.phase == fresh.phase);
        CHECK(r.t == fresh.t);
        CHECK(r.fade == fresh.fade);
        CHECK(r.inputLocked == fresh.inputLocked);
        if (first || s.phase != last) {
            ++seen;
            last = s.phase;
            first = false;
        }
        s = step_return(s, 0.01f, tm, i >= 10, i >= 800);
    }
    CHECK(seen == 7);  // every phase was visited, so every phase was reset
    CHECK(s.phase == ReturnPhase::Free);
}

TEST_CASE("return_phase_name spells the enum") {
    CHECK(std::strcmp(return_phase_name(ReturnPhase::Idle), "Idle") == 0);
    CHECK(std::strcmp(return_phase_name(ReturnPhase::Hold), "Hold") == 0);
    CHECK(std::strcmp(return_phase_name(ReturnPhase::FadeOut), "FadeOut") == 0);
    CHECK(std::strcmp(return_phase_name(ReturnPhase::Load), "Load") == 0);
    CHECK(std::strcmp(return_phase_name(ReturnPhase::FadeIn), "FadeIn") == 0);
    CHECK(std::strcmp(return_phase_name(ReturnPhase::Wake), "Wake") == 0);
    CHECK(std::strcmp(return_phase_name(ReturnPhase::Free), "Free") == 0);
}

TEST_CASE("return_unavailable_warning: empty when the shrine manifest and the wake clips are there") {
    CHECK(return_unavailable_warning("out/re/stage/shrine.manifest", true, "w.glb", true).empty());
}

TEST_CASE("return_unavailable_warning names the missing input and every way out (R16)") {
    const std::string noManifest =
        return_unavailable_warning("/nonexistent/shrine.manifest", false, "w.glb", true);
    CHECK(noManifest.find("/nonexistent/shrine.manifest") != std::string::npos);
    CHECK(noManifest.find("wake clips") == std::string::npos);

    const std::string noClips =
        return_unavailable_warning("out/re/stage/shrine.manifest", true, "w.glb", false);
    CHECK(noClips.find("w.glb") != std::string::npos);
    CHECK(noClips.find("wake clips") != std::string::npos);
    CHECK(noClips.find("shrine.manifest") == std::string::npos);

    const std::string both = return_unavailable_warning("s.manifest", false, "w.glb", false);
    for (const std::string& w : {noManifest, noClips, both}) {
        CHECK(w.find("make shrine-manifest") != std::string::npos);
        CHECK(w.find("make bake") != std::string::npos);
        CHECK(w.find("--no-return") != std::string::npos);
        CHECK(std::count(w.begin(), w.end(), '\n') == 1);  // one line
        CHECK(w.back() == '\n');
    }
    CHECK(both.find("s.manifest") != std::string::npos);
    CHECK(both.find("wake clips") != std::string::npos);
}
