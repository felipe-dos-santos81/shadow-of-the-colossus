#pragma once

#include <cstddef>

#include "audio_events.h"

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

struct SoundCue {
    const char* clip;  // manifest name, never null for a mapped cue
    float gain;        // fractional voice gain (HurtHeavy is a 1.25 boost)
    bool loop;         // the clip's own loop flag is authoritative; informational
    bool s6Only = false;  // required only by the S6 slice's load gate (death, club swing)
};

// The fight music is not an `Sfx`: the app owns its lifecycle through
// AudioEngine::play_music / stop_music (D5: starts on the first landed hit,
// loops, stops on defeat and reset).
inline constexpr const char* kMusicClip = "music.boss";
inline constexpr float kMusicGain = 0.45f;

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
    {"valus.telegraph", 0.60f, false},  // StompStart
    {"valus.impact", 0.50f, false},     // StompImpact
    {"valus.telegraph", 0.60f, false},  // ClubStart
    {"valus.impact", 0.50f, false},     // ClubImpact
    {nullptr, 1.00f, false},            // ClubCancel: deferred
    {"valus.hurt", 0.60f, false, false},       // ValusReaction
    {"valus.death", 0.80f, false, true},       // ValusDefeat: the S6 death roar
    {nullptr, 1.00f, false, false},            // FootLeft: deferred
    {nullptr, 1.00f, false, false},            // FootRight: deferred
    {nullptr, 1.00f, false, false},            // Reset: deferred
    {"valus.club_swing", 0.70f, false, true},  // ClubSwing: the S6 club whoosh
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
