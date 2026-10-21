#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "math3d.h"
#include "player_state.h"
#include "valus.h"

// The S5 sound seam (design §7.4): the simulation never calls SDL. The player
// state machine and the app's encounter helpers only report what changed; this
// header turns those reports into a small fixed-capacity cue queue that the app
// drains once per frame. Pure data and arithmetic — no SDL, no I/O, no
// allocation — so the headless tests and the acceptance timeline drive exactly
// the translation the interactive loop uses.
//
// The same queue is translated by one app-side function (cue_for + play), which
// is what keeps the interactive loop and the shared scripted timeline from
// drifting apart.

enum class Sfx : std::uint16_t {
    Strike,
    StrikeHit,
    Grab,
    GripBreak,
    Land,
    HurtLight,
    HurtHeavy,
    Death,
    StompStart,
    StompImpact,
    ClubStart,
    ClubImpact,
    ClubCancel,
    ValusReaction,
    ValusDefeat,
    FootLeft,
    FootRight,
    Reset,
    Count
};

// One cue. `gain` scales the cue table's own gain; `spatial` selects a world
// voice (distance attenuation and pan) or a centred one; `position` is the
// world anchor a spatial cue is attenuated from.
struct SfxEvent {
    Sfx id = Sfx::Count;
    float gain = 1.0f;
    Vec3 position{};
    bool spatial = false;
};

// The fixed pool a frame's cues accumulate in. A 64-cue step is far beyond a
// real frame (the whole fight emits a handful per frame); overflow drops the
// extra cue rather than growing, so the per-frame path never allocates.
struct SfxQueue {
    static constexpr int kCapacity = 64;
    std::array<SfxEvent, kCapacity> items{};
    int count = 0;

    void push(const SfxEvent& event) {
        if (count >= kCapacity) {
            return;
        }
        items[static_cast<std::size_t>(count)] = event;
        ++count;
    }

    void clear() { count = 0; }
    bool empty() const { return count == 0; }
};

// The encounter transitions the app reports. The plan's one-argument
// `Encounter` snapshot cannot tell a telegraph from an impact, or a cancel from
// a defeat; the app knows which transition it just made, so it names it.
enum class EncounterSfx : std::uint8_t {
    StompStart,
    StompImpact,
    ClubStart,
    ClubImpact,
    ClubCancel,
    ValusReaction,
    ValusDefeat,
    Reset
};

// Appends the cue an encounter transition banks. All encounter cues are world
// sounds: `at` is the entity's (or the striking joint's) world position.
void append_encounter_sfx(EncounterSfx event, const Vec3& at, SfxQueue& out);

// Appends one step's player cues. `playerPosition` anchors the spatial ones
// (a landed hit, hurt, death); the swing itself is centred and always audible.
// Nothing is appended for a step that reported nothing.
void append_player_sfx(const StepEvents& events, const Vec3& playerPosition, SfxQueue& out);

// --- Definitions (header-only: the seam has no translation unit) -------------

inline void append_encounter_sfx(EncounterSfx event, const Vec3& at, SfxQueue& out) {
    Sfx id = Sfx::Count;
    switch (event) {
        case EncounterSfx::StompStart:
            id = Sfx::StompStart;
            break;
        case EncounterSfx::StompImpact:
            id = Sfx::StompImpact;
            break;
        case EncounterSfx::ClubStart:
            id = Sfx::ClubStart;
            break;
        case EncounterSfx::ClubImpact:
            id = Sfx::ClubImpact;
            break;
        case EncounterSfx::ClubCancel:
            id = Sfx::ClubCancel;
            break;
        case EncounterSfx::ValusReaction:
            id = Sfx::ValusReaction;
            break;
        case EncounterSfx::ValusDefeat:
            id = Sfx::ValusDefeat;
            break;
        case EncounterSfx::Reset:
            id = Sfx::Reset;
            break;
    }
    if (id == Sfx::Count) {
        return;
    }
    out.push(SfxEvent{id, 1.0f, at, /*spatial=*/true});
}

inline void append_player_sfx(const StepEvents& events, const Vec3& playerPosition,
                              SfxQueue& out) {
    // The swing first: when a strike lands, its whoosh is the downbeat and the
    // hit is the accent.
    if (events.struck) {
        out.push(SfxEvent{Sfx::Strike, 1.0f, playerPosition, /*spatial=*/false});
    }
    if (events.damageDealt > 0) {
        // The state machine reports a landed shell hit as its one damage
        // event; a whiff carries no damage and only the swing.
        out.push(SfxEvent{Sfx::StrikeHit, 1.0f, playerPosition, /*spatial=*/true});
    }
    if (events.hurtLight) {
        out.push(SfxEvent{Sfx::HurtLight, 1.0f, playerPosition, /*spatial=*/true});
    }
    if (events.hurtHeavy) {
        out.push(SfxEvent{Sfx::HurtHeavy, 1.0f, playerPosition, /*spatial=*/true});
    }
    if (events.died) {
        out.push(SfxEvent{Sfx::Death, 1.0f, playerPosition, /*spatial=*/true});
    }
    if (events.grabbed) {
        out.push(SfxEvent{Sfx::Grab, 1.0f, playerPosition, /*spatial=*/false});
    }
    if (events.gripBroke) {
        out.push(SfxEvent{Sfx::GripBreak, 1.0f, playerPosition, /*spatial=*/false});
    }
    if (events.landed) {
        out.push(SfxEvent{Sfx::Land, 1.0f, playerPosition, /*spatial=*/false});
    }
}
