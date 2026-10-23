// The S5 seam (design §7.4): `StepEvents` and the encounter transitions become
// a fixed-capacity cue queue, and the cue table maps every `Sfx` to a shipped
// manifest clip or deliberately to silence. Everything here is pure data — no
// SDL audio device, no playback — except the last case, which loads the real
// manifest (still without opening a device).
#include <doctest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include "audio.h"
#include "audio_events.h"
#include "audio_map.h"
#include "math3d.h"

namespace {

std::filesystem::path test_library() {
    const char* value = std::getenv("SOTC_LIB");
    if (value == nullptr || value[0] == '\0') {
        throw std::runtime_error("Set SOTC_LIB to the converted asset library.");
    }
    std::filesystem::path library = value;
    if (library.is_relative()) {
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        library = repo_root / library;
    }
    return library;
}

bool has_id(const SfxQueue& queue, Sfx id) {
    for (int i = 0; i < queue.count; ++i) {
        if (queue.items[static_cast<std::size_t>(i)].id == id) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("StepEvents hurt flags translate to HurtLight/HurtHeavy") {
    const Vec3 at{1.5f, 8.0f, -2.0f};
    SfxQueue queue;

    StepEvents light;
    light.hurtLight = true;
    append_player_sfx(light, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::HurtLight);
    CHECK(queue.items[0].spatial);
    CHECK(queue.items[0].gain == doctest::Approx(1.0f));
    CHECK(queue.items[0].position.x == doctest::Approx(at.x));
    CHECK(queue.items[0].position.y == doctest::Approx(at.y));
    CHECK(queue.items[0].position.z == doctest::Approx(at.z));

    queue.clear();
    StepEvents heavy;
    heavy.hurtHeavy = true;
    append_player_sfx(heavy, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::HurtHeavy);
    CHECK(queue.items[0].spatial);
    CHECK_FALSE(has_id(queue, Sfx::HurtLight));
}

TEST_CASE("a landed strike translates to Strike + StrikeHit; a whiff to Strike") {
    const Vec3 at{0.0f, 1.0f, 0.0f};
    SfxQueue queue;

    // A whiff still swings the sword: `struck` is the release; a landed hit is
    // the damage event (the state machine's damageDealt is 0 here).
    StepEvents whiff;
    whiff.struck = true;
    append_player_sfx(whiff, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::Strike);
    CHECK_FALSE(queue.items[0].spatial);  // Wander's own swing is centred
    CHECK_FALSE(has_id(queue, Sfx::StrikeHit));

    queue.clear();
    StepEvents landed;
    landed.struck = true;
    landed.damageDealt = 34;  // a landed shell hit reports its damage
    append_player_sfx(landed, at, queue);
    REQUIRE(queue.count == 2);
    // The swing sounds before the hit it lands.
    CHECK(queue.items[0].id == Sfx::Strike);
    CHECK(queue.items[1].id == Sfx::StrikeHit);
    CHECK(queue.items[1].spatial);
    CHECK(queue.items[1].position.y == doctest::Approx(at.y));
}

TEST_CASE("grab/grip break/land translate") {
    const Vec3 at{4.0f, 0.0f, 4.0f};
    SfxQueue queue;

    StepEvents grabbed;
    grabbed.grabbed = true;
    append_player_sfx(grabbed, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::Grab);

    queue.clear();
    StepEvents broke;
    broke.gripBroke = true;
    append_player_sfx(broke, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::GripBreak);

    queue.clear();
    StepEvents landed;
    landed.landed = true;
    append_player_sfx(landed, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::Land);

    // A step that reports nothing pushes nothing.
    queue.clear();
    append_player_sfx(StepEvents{}, at, queue);
    CHECK(queue.count == 0);
}

TEST_CASE("Valus impact translates StompImpact/ClubImpact once per beat") {
    const Vec3 at{-3.0f, 12.0f, 2.0f};
    SfxQueue queue;

    append_encounter_sfx(EncounterSfx::StompStart, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::StompStart);
    CHECK(queue.items[0].spatial);
    CHECK(queue.items[0].position.x == doctest::Approx(at.x));

    queue.clear();
    append_encounter_sfx(EncounterSfx::StompImpact, at, queue);
    REQUIRE(queue.count == 1);  // one beat, one impact cue
    CHECK(queue.items[0].id == Sfx::StompImpact);

    queue.clear();
    append_encounter_sfx(EncounterSfx::ClubStart, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::ClubStart);

    queue.clear();
    append_encounter_sfx(EncounterSfx::ClubSwing, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::ClubSwing);

    queue.clear();
    append_encounter_sfx(EncounterSfx::ClubImpact, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::ClubImpact);
    CHECK_FALSE(has_id(queue, Sfx::StompImpact));

    queue.clear();
    append_encounter_sfx(EncounterSfx::ValusReaction, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::ValusReaction);

    queue.clear();
    append_encounter_sfx(EncounterSfx::ValusDefeat, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::ValusDefeat);

    queue.clear();
    append_encounter_sfx(EncounterSfx::ClubCancel, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::ClubCancel);

    queue.clear();
    append_encounter_sfx(EncounterSfx::Reset, at, queue);
    REQUIRE(queue.count == 1);
    CHECK(queue.items[0].id == Sfx::Reset);
}

TEST_CASE("reset clears the queue") {
    SfxQueue queue;
    StepEvents events;
    events.struck = true;
    events.damageDealt = 34;
    append_player_sfx(events, Vec3{}, queue);
    append_encounter_sfx(EncounterSfx::StompImpact, Vec3{}, queue);
    REQUIRE(queue.count == 3);
    CHECK_FALSE(queue.empty());

    queue.clear();
    CHECK(queue.count == 0);
    CHECK(queue.empty());

    // The fixed buffer is bounded: the 65th cue is dropped, never written out
    // of range.
    for (int i = 0; i < SfxQueue::kCapacity + 5; ++i) {
        queue.push(SfxEvent{Sfx::Strike, 1.0f, Vec3{}, false});
    }
    CHECK(queue.count == SfxQueue::kCapacity);
}

TEST_CASE("the cue table maps the shipped slice and defers the rest") {
    struct Expected {
        Sfx id;
        const char* name;
        const char* clip;  // nullptr = deliberately silent
        float gain;
    };
    const Expected expected[] = {
        {Sfx::Strike, "strike", "player.swing", 0.60f},
        {Sfx::StrikeHit, "strike_hit", "player.hit", 0.60f},
        {Sfx::Grab, "grab", nullptr, 1.0f},
        {Sfx::GripBreak, "grip_break", nullptr, 1.0f},
        {Sfx::Land, "land", nullptr, 1.0f},
        {Sfx::HurtLight, "hurt_light", "player.hurt", 1.0f},
        {Sfx::HurtHeavy, "hurt_heavy", "player.hurt", 1.25f},
        {Sfx::Death, "death", "player.death", 0.80f},
        {Sfx::StompStart, "stomp_start", "valus.telegraph", 0.60f},
        {Sfx::StompImpact, "stomp_impact", "valus.impact", 0.50f},
        {Sfx::ClubStart, "club_start", "valus.telegraph", 0.60f},
        {Sfx::ClubImpact, "club_impact", "valus.impact", 0.50f},
        {Sfx::ClubCancel, "club_cancel", nullptr, 1.0f},
        {Sfx::ValusReaction, "valus_reaction", "valus.hurt", 0.60f},
        {Sfx::ValusDefeat, "valus_defeat", "valus.death", 0.80f},
        {Sfx::FootLeft, "foot_left", nullptr, 1.0f},
        {Sfx::FootRight, "foot_right", nullptr, 1.0f},
        {Sfx::Reset, "reset", nullptr, 1.0f},
        {Sfx::ClubSwing, "club_swing", "valus.club_swing", 0.70f},
    };
    REQUIRE(static_cast<int>(Sfx::Count) ==
            static_cast<int>(sizeof(expected) / sizeof(expected[0])));

    for (const Expected& row : expected) {
        CAPTURE(row.name);
        const SoundCue* cue = cue_for(row.id);
        CHECK(std::string(sfx_name(row.id)) == row.name);
        if (row.clip == nullptr) {
            CHECK(cue == nullptr);
        } else {
            REQUIRE(cue != nullptr);
            CHECK(std::string(cue->clip) == row.clip);
            CHECK(cue->gain == doctest::Approx(row.gain));
            CHECK_FALSE(cue->loop);
        }
    }

    // Every id has a distinct name; Count is not a cue.
    CHECK(std::string(sfx_name(Sfx::Count)) == "unknown");
    CHECK(cue_for(Sfx::Count) == nullptr);
}

TEST_CASE("the death and the club swing resolve to the S6 slice") {
    const SoundCue* death = cue_for(Sfx::ValusDefeat);
    const SoundCue* swing = cue_for(Sfx::ClubSwing);
    REQUIRE(death != nullptr);
    REQUIRE(swing != nullptr);
    CHECK(std::string(death->clip) == "valus.death");
    CHECK(std::string(swing->clip) == "valus.club_swing");
    CHECK(death->s6Only);
    CHECK(swing->s6Only);
}

TEST_CASE("every mapped cue resolves in the shipped audio manifest") {
    const std::filesystem::path library = test_library();
    const std::filesystem::path manifest = library / "audio" / "audio.manifest";
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(manifest),
                    ("missing " + manifest.string()).c_str());

    AudioEngine engine;
    REQUIRE_MESSAGE(engine.load_manifest(manifest.string(), library.string()),
                    engine.last_error());

    // The S5 slice is required of every shipped manifest: the frozen S5
    // acceptance regenerates its eight entries, and the gate must load them
    // beside the S6 cue table.
    const Sfx missing = missing_cue([&engine](const char* clip) { return engine.has(clip); });
    CHECK(missing == Sfx::Count);

    // The S6 slice is required only of a manifest that carries it: the S5
    // acceptance leaves the library at eight, the S6 run at ten, so ctest is
    // green either way without weakening the S6 gate.
    const bool s6_shipped = engine.has("valus.death") && engine.has("valus.club_swing");
    if (s6_shipped) {
        const Sfx missingS6 = missing_cue(
            [&engine](const char* clip) { return engine.has(clip); }, /*includeS6=*/true);
        CHECK(missingS6 == Sfx::Count);
    } else {
        // An eight-entry manifest: asking for the whole slice names the first
        // S6-only row, which is exactly the state the S5 run must tolerate.
        const Sfx missingS6 = missing_cue(
            [&engine](const char* clip) { return engine.has(clip); }, /*includeS6=*/true);
        CHECK(missingS6 == Sfx::ValusDefeat);
        CHECK_FALSE(engine.has("valus.club_swing"));
    }

    // A rejected clip is reported as the event that names it (the load error's
    // `Sfx` name), not as a bare file path.
    const Sfx rejected =
        missing_cue([&engine](const char* clip) {
            return std::string(clip) != "valus.hurt" && engine.has(clip);
        });
    CHECK(rejected == Sfx::ValusReaction);
    CHECK(std::string(sfx_name(rejected)) == "valus_reaction");
}

// --- S8 Task 8: the ride cue table -------------------------------------------

namespace {

// Every `"event": "<name>"` of a curated map's `cues` (in file order).
std::vector<std::string> map_events(const char* relPath) {
    const std::filesystem::path path =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / relPath;
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot read ", path.string());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    text = text.substr(0, text.find("\"deferred\""));  // the `cues` rows only (cues come first)
    const std::regex row("\"event\"\\s*:\\s*\"([^\"]+)\"");
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), row);
         it != std::sregex_iterator(); ++it) {
        out.push_back((*it)[1].str());
    }
    return out;
}

}  // namespace

TEST_CASE("S8: the ride cue table maps exactly the S8 events of s8_map.json") {
    const std::vector<std::string> s8 = map_events("tools/re/audio/s8_map.json");
    const std::vector<std::string> s6 = map_events("tools/re/audio/s6_map.json");
    REQUIRE(s6.size() == 10);
    REQUIRE(s8.size() == 34);
    std::set<std::string> wanted(s8.begin(), s8.end());
    for (const std::string& event : s6) {
        wanted.erase(event);
    }
    REQUIRE(wanted.size() == 24);

    std::set<std::string> mapped;
    for (int i = 0; i < static_cast<int>(RideSfx::Count); ++i) {
        const RideSfx id = static_cast<RideSfx>(i);
        const RideCue& cue = ride_cue(id);
        CAPTURE(ride_sfx_name(id));
        REQUIRE(cue.variants >= 1);
        CHECK(cue.gain > 0.0f);
        for (int v = 0; v < cue.variants; ++v) {
            CHECK(mapped.insert(ride_cue_clip(id, v)).second);
        }
        // A variant index wraps: a seeded pick can never name a missing row.
        CHECK(ride_cue_clip(id, cue.variants) == ride_cue_clip(id, 0));
    }
    CHECK(mapped == wanted);
    CHECK(ride_cue_clip(RideSfx::HoofTrot, 0) == "agro.hoof_trot");
    CHECK(ride_cue_clip(RideSfx::HoofGallop, 4) == "agro.hoof_gallop.4");
    CHECK(std::string(ride_sfx_name(RideSfx::StopHorse)) == "stop_horse");
}

// --- S9 Task 4: the sword's cue table ----------------------------------------

TEST_CASE("S9: the sword cue table covers every SwordSfx; the S5/S6/S8 tables are unchanged") {
    std::set<std::string> clips;
    for (int i = 0; i < static_cast<int>(SwordSfx::Count); ++i) {
        const SwordSfx id = static_cast<SwordSfx>(i);
        const SwordCue& cue = sword_cue(id);
        CAPTURE(sword_sfx_name(id));
        CHECK(cue.clip != nullptr);
        CHECK(cue.gain > 0.0f);
        CHECK(clips.insert(cue.clip).second);
    }
    CHECK(clips == std::set<std::string>{"sword.shimmer", "sword.search"});
    CHECK(std::string(sword_cue(SwordSfx::Shimmer).clip) == "sword.shimmer");
    CHECK(sword_cue(SwordSfx::Shimmer).gain == doctest::Approx(0.6f));
    CHECK(std::string(sword_cue(SwordSfx::Search).clip) == "sword.search");
    CHECK(sword_cue(SwordSfx::Search).gain == doctest::Approx(0.7f));
    CHECK(std::string(sword_sfx_name(SwordSfx::Shimmer)) == "shimmer");
    CHECK(std::string(sword_sfx_name(SwordSfx::Search)) == "search");

    // The earlier tables still hold their pinned sizes and rows.
    CHECK(static_cast<int>(RideSfx::Count) == 8);
    CHECK(std::string(ride_cue(RideSfx::Kick).event) == "player.kick");
    CHECK(ride_cue(RideSfx::HoofWalk).variants == 10);
    CHECK(std::string(sfx_name(Sfx::ValusReaction)) == "valus_reaction");
}
