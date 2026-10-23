#pragma once

// The S5 audio seam (S10 Task 6a moved it out of app.cpp).

#include "audio.h"
#include "audio_events.h"
#include "audio_map.h"
#include "math3d.h"

#include <array>
#include <cstdint>
#include <string>

constexpr std::uint32_t kRideSfxSeed = 8u;          // the variant RNG's fixed seed

// The app's one audio object: the mixer, the music lifecycle, and — for
// --s5-selftest — the ordered cue log and the WAV capture. One translation path
// (play_event) serves the interactive loop and the shared scripted timeline, so
// their sound cannot drift. With the engine off (`enabled == false`) every
// method is a no-op, which is what keeps the default S4.2 run byte-identical.
struct AudioSeam {
    AudioEngine engine;
    bool enabled = false;       // the manifest and every mapped clip loaded
    // The encounter whose cues and music the seam plays (S11 M-1): set by
    // `load`, read only while `enabled`.
    const EncounterDesc* encounter = nullptr;
    bool log = false;           // --s5-selftest/--s6-selftest: one line per played cue
    // The log line's tag: `s5-event` for the frozen S5 acceptance, `s6-event`
    // for the S6 selftest. The format is otherwise identical (Task 6 parses it).
    const char* logPrefix = "s5-event";
    bool failed = false;        // a mapped clip went missing at play time
    bool musicStarted = false;  // the music voice is live
    bool musicEverStarted = false;
    bool defeatStop = false;    // --s5-selftest's post-reset defeat phase ran
    float clock = 0.0f;         // simulated seconds since the capture began
    int eventCount = 0;         // s5-event lines emitted

    // The S6 slice's two rows are optional for the interactive game: an
    // S5-only manifest still loads and plays, and a missing S6-only clip warns
    // **once** and stays silent with the mixer running. The S6 selftest
    // demands the rows, so there the same condition is the fatal error it has
    // always been (`requireS6`).
    bool requireS6 = false;
    std::array<bool, static_cast<std::size_t>(Sfx::Count)> warnedMissing{};

    // Loads `libraryDir/audio/audio.manifest` and validates the cue table. The
    // engine names its own manifest failure; a rejected cue names its `Sfx` and
    // the clip. The caller picks the policy (fatal in --s5-selftest, one warning
    // and a silent game otherwise) and the slice: `includeS6` false requires the
    // S5 rows only, so the frozen S5 acceptance's eight-entry manifest stays
    // loadable beside the S6 cue table; true requires every row, including the
    // death and the club swing (Task 5's --s6-selftest). The encounter rows and
    // the music are `desc`'s.
    bool load(const std::string& libraryDir, const EncounterDesc& desc, bool includeS6);

    // The one place a queued cue becomes a voice (and an s5-event line).
    void play_event(const SfxEvent& event);

    // S8 (D15): one ride cue, its variant picked by the seeded RNG (the
    // selftests are deterministic: the seed is fixed per stage). A row the
    // loaded manifest lacks (an S7 acceptance leaves the S6 ten — `make audio`
    // restores the 34) warns once per cue and stays silent.
    std::uint32_t rideRng = kRideSfxSeed;
    int rideEvents = 0;  // ride cues played this stage
    int hooves = 0;      // of which hoof one-shots
    std::array<bool, static_cast<std::size_t>(RideSfx::Count)> rideWarned{};

    void play_ride(RideSfx id, const Vec3& position, bool spatial = true);

    // S9: the sword's two cues (authored, audio_map.h). A row the manifest lacks
    // (an S8 acceptance leaves the 34) warns once per cue and stays silent.
    std::array<bool, static_cast<std::size_t>(SwordSfx::Count)> swordWarned{};

    void play_sword(SwordSfx id, const Vec3& position);

    // Ends the looping shimmer (lowering, R); a no-op when it is not playing.
    void stop_sword();
    // --s9-selftest's s9-audio: sword cues played and live shimmers stopped —
    // run totals (new_stage leaves them).
    std::array<int, static_cast<std::size_t>(SwordSfx::Count)> swordPlays{};
    int shimmerStops = 0;

    void play_all(SfxQueue& queue);

    void start_music();

    void stop_music();

    void reset();

    // The stage loop (S7): each stage after the first starts with the seam's
    // per-stage state cleared and every voice stopped. The engine (its device
    // and mute) persists; a stage that wants sound loads the manifest again
    // (the shrine has no encounter, so it stays silent — design D10).
    void new_stage();

    void toggle_mute();

    void update(float dt, const Vec3& listener, float yaw);
};
