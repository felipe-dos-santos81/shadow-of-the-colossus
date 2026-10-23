#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

#include "audio_events.h"
#include "encounters/valus.h"

// The S5 cue table: the curated `Sfx` -> shipped manifest clip mapping (design
// §7.3/§7.4, ruling R15). The table is deliberately narrow — the attribution
// gate's seven cues plus the fight music in S5, extended in S6 by Valus's
// defeat and the club swing (design §8.4). A null cue is a deferred event, not
// a missing one: grabbing, the grip break, landing, the club cancel, footsteps
// and the reset are silent.
//
// The names are the R1 manifest's `name=` values, loaded once at start. Every
// non-null clip must exist in the loaded manifest: `missing_cue` names the
// first offending `Sfx`, and the app turns that into a load error (fatal in
// --s5-selftest, one warning and a silent game in interactive play).

// `SoundCue` lives in encounter_desc.h: the encounter's rows below are Valus's
// EncounterDesc `cues` (S11 Task 7).

// The fight music is not an `Sfx`: the app owns its lifecycle through
// AudioEngine::play_music / stop_music (D5: starts on the first landed hit,
// loops, stops on defeat and reset).
inline constexpr const char* kMusicClip = encounters::kValus.cues.music;
inline constexpr float kMusicGain = encounters::kValus.cues.musicGain;

namespace audio_map_detail {

// Indexed by `Sfx`; the order is the enum's and the static_assert keeps them in
// step. A nullptr clip is a deliberately deferred event. `s6Only` marks the two
// rows the S6 slice adds: the S5 load gate does not require them, so the frozen
// S5 acceptance's eight-entry manifest loads unchanged.
inline constexpr SoundCue kCues[] = {
    {"player.swing", 0.60f, false},    // Strike: Wander's swing
    {"player.hit", 0.60f, false},      // StrikeHit: the blade landing
    {nullptr, 1.00f, false},           // Grab: deferred
    {nullptr, 1.00f, false},           // GripBreak: deferred
    {nullptr, 1.00f, false},           // Land: deferred
    {"player.hurt", 1.00f, false},     // HurtLight
    {"player.hurt", 1.25f, false},     // HurtHeavy: the same clip, pushed
    {"player.death", 0.80f, false},    // Death
    encounters::kValus.cues.stompStart,   // StompStart
    encounters::kValus.cues.stompImpact,  // StompImpact
    encounters::kValus.cues.clubStart,    // ClubStart
    encounters::kValus.cues.clubImpact,   // ClubImpact
    encounters::kValus.cues.clubCancel,   // ClubCancel: deferred
    encounters::kValus.cues.reaction,     // ValusReaction
    encounters::kValus.cues.defeat,       // ValusDefeat: the S6 death roar
    encounters::kValus.cues.footLeft,     // FootLeft: deferred
    encounters::kValus.cues.footRight,    // FootRight: deferred
    {nullptr, 1.00f, false, false},       // Reset: deferred
    encounters::kValus.cues.clubSwing,    // ClubSwing: the S6 club whoosh
};

static_assert(sizeof(kCues) / sizeof(kCues[0]) == static_cast<std::size_t>(Sfx::Count),
              "the cue table must cover every Sfx");

}  // namespace audio_map_detail

// The cue for `id`, or nullptr when the event is deliberately silent (or `id`
// is Count/out of range).
inline const SoundCue* cue_for(Sfx id) {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= static_cast<std::size_t>(Sfx::Count)) {
        return nullptr;
    }
    const SoundCue& cue = audio_map_detail::kCues[index];
    return cue.clip == nullptr ? nullptr : &cue;
}

// The stable lowercase token the `s5-event` log uses (`strike`, `hurt_heavy`,
// `valus_reaction`, ...), and `unknown` for Count/out of range.
inline const char* sfx_name(Sfx id) {
    switch (id) {
        case Sfx::Strike:
            return "strike";
        case Sfx::StrikeHit:
            return "strike_hit";
        case Sfx::Grab:
            return "grab";
        case Sfx::GripBreak:
            return "grip_break";
        case Sfx::Land:
            return "land";
        case Sfx::HurtLight:
            return "hurt_light";
        case Sfx::HurtHeavy:
            return "hurt_heavy";
        case Sfx::Death:
            return "death";
        case Sfx::StompStart:
            return "stomp_start";
        case Sfx::StompImpact:
            return "stomp_impact";
        case Sfx::ClubStart:
            return "club_start";
        case Sfx::ClubImpact:
            return "club_impact";
        case Sfx::ClubCancel:
            return "club_cancel";
        case Sfx::ValusReaction:
            return "valus_reaction";
        case Sfx::ValusDefeat:
            return "valus_defeat";
        case Sfx::FootLeft:
            return "foot_left";
        case Sfx::FootRight:
            return "foot_right";
        case Sfx::Reset:
            return "reset";
        case Sfx::ClubSwing:
            return "club_swing";
        case Sfx::Count:
            break;
    }
    return "unknown";
}

// The first `Sfx` whose non-null cue names a clip `has_clip` rejects, or
// `Sfx::Count` when the loaded manifest satisfies the whole table. The app
// reports that `Sfx` by name, so a missing clip fails as an event, not as an
// anonymous file.
//
// `includeS6` selects the slice the caller requires: the S5 gate (and the
// pre-S6 interactive game) asks only for the non-S6 rows, so the frozen S5
// acceptance's eight-entry manifest stays loadable; the S6 gate asks for every
// row, so the ten-entry slice must carry `valus.death` and `valus.club_swing`.
template <typename HasClip>
Sfx missing_cue(const HasClip& has_clip, bool includeS6 = false) {
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        const Sfx id = static_cast<Sfx>(i);
        const SoundCue* cue = cue_for(id);
        if (cue == nullptr || (cue->s6Only && !includeS6)) {
            continue;
        }
        if (!has_clip(cue->clip)) {
            return id;
        }
    }
    return Sfx::Count;
}

// --- S8: the ride cue table (design D15) --------------------------------------
//
// The S8 sounds are not `Sfx` (that list stays as pinned): the plain plays them
// from its own table. Each cue is the base event of `tools/re/audio/s8_map.json`;
// a cue with alternatives is one manifest row per variant (`<event>.<n>`, rows
// carrying `variant_of`), and the app picks the variant per trigger with a
// seeded RNG. The rows are audition picks (`method: audition`, low/medium
// confidence); the gains are authored (source: authored).
enum class RideSfx {
    HoofWalk, HoofTrot, HoofGallop, Neigh, Whistle, Mount, Kick, StopHorse, Count
};

struct RideCue {
    const char* event;  // the base event; a single-variant cue is the manifest name itself
    int variants;       // 1 = no `.<n>` suffix
    float gain;         // authored
};

namespace audio_map_detail {

inline constexpr RideCue kRideCues[] = {
    {"agro.hoof_walk", 10, 0.45f},    // HoofWalk: one per walk touchdown
    {"agro.hoof_trot", 1, 0.50f},     // HoofTrot
    {"agro.hoof_gallop", 5, 0.60f},   // HoofGallop
    {"agro.neigh", 1, 0.70f},         // Neigh: the whistle's arrival, a refused kick
    {"player.whistle", 3, 0.70f},     // Whistle
    {"agro.mount", 1, 0.60f},         // Mount
    {"player.kick", 1, 0.70f},        // Kick: the "hyah" of a counted kick
    {"player.stop_horse", 2, 0.70f},  // StopHorse: the "whoa" as a stop begins
};

static_assert(sizeof(kRideCues) / sizeof(kRideCues[0]) == static_cast<std::size_t>(RideSfx::Count),
              "the ride cue table must cover every RideSfx");

}  // namespace audio_map_detail

inline const RideCue& ride_cue(RideSfx id) {
    const std::size_t index =
        std::min(static_cast<std::size_t>(id), static_cast<std::size_t>(RideSfx::Count) - 1);
    return audio_map_detail::kRideCues[index];
}

// The manifest name of `variant` (wrapped into the cue's variant count).
inline std::string ride_cue_clip(RideSfx id, int variant) {
    const RideCue& cue = ride_cue(id);
    if (cue.variants <= 1) {
        return cue.event;
    }
    const int v = ((variant % cue.variants) + cue.variants) % cue.variants;
    return std::string(cue.event) + "." + std::to_string(v);
}

inline const char* ride_sfx_name(RideSfx id) {
    switch (id) {
        case RideSfx::HoofWalk: return "hoof_walk";
        case RideSfx::HoofTrot: return "hoof_trot";
        case RideSfx::HoofGallop: return "hoof_gallop";
        case RideSfx::Neigh: return "neigh";
        case RideSfx::Whistle: return "whistle";
        case RideSfx::Mount: return "mount";
        case RideSfx::Kick: return "kick";
        case RideSfx::StopHorse: return "stop_horse";
        case RideSfx::Count: break;
    }
    return "unknown";
}

// --- S9: the sword's cue table -------------------------------------
//
// Authored behaviour: the shimmer (a `loop=1` manifest row) starts when the
// sword leaves Lowered and is stopped with `AudioEngine::stop` when it reaches
// Lowering; the search cue plays once on each rising edge of `locked`. The
// gains are authored.
enum class SwordSfx { Shimmer, Search, Count };

struct SwordCue {
    const char* clip;
    float gain;  // authored
};

namespace audio_map_detail {

inline constexpr SwordCue kSwordCues[] = {
    {"sword.shimmer", 0.6f},  // Shimmer
    {"sword.search", 0.7f},   // Search
};

static_assert(sizeof(kSwordCues) / sizeof(kSwordCues[0]) ==
                  static_cast<std::size_t>(SwordSfx::Count),
              "the sword cue table must cover every SwordSfx");

}  // namespace audio_map_detail

inline const SwordCue& sword_cue(SwordSfx id) {
    const std::size_t index =
        std::min(static_cast<std::size_t>(id), static_cast<std::size_t>(SwordSfx::Count) - 1);
    return audio_map_detail::kSwordCues[index];
}

inline const char* sword_sfx_name(SwordSfx id) {
    switch (id) {
        case SwordSfx::Shimmer: return "shimmer";
        case SwordSfx::Search: return "search";
        case SwordSfx::Count: break;
    }
    return "unknown";
}
