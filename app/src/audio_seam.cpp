#include "audio_seam.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

bool AudioSeam::load(const std::string& libraryDir, bool includeS6) {
    enabled = false;
    const std::string manifest = libraryDir + "/audio/audio.manifest";
    if (!engine.load_manifest(manifest, libraryDir)) {
        return false;
    }
    const Sfx missing = missing_cue(
        [this](const char* clip) { return engine.has(clip); }, includeS6);
    if (missing != Sfx::Count) {
        const SoundCue* cue = cue_for(missing);
        std::fprintf(stderr,
                     "sotc-viewer: audio: cue %s names clip '%s', which %s does not load\n",
                     sfx_name(missing), cue != nullptr ? cue->clip : "?", manifest.c_str());
        engine.reset();
        return false;
    }
    // The music is not an `Sfx`, so `missing_cue` cannot see it; the score
    // is as mandatory as the fight cues (D5), so gate it here — a manifest
    // without it fails at load, not as "a cue clip was missing at play time".
    if (!engine.has(kMusicClip)) {
        std::fprintf(
            stderr,
            "sotc-viewer: audio: music cue names clip '%s', which %s does not load\n",
            kMusicClip, manifest.c_str());
        engine.reset();
        return false;
    }
    enabled = true;
    return true;
}

void AudioSeam::play_event(const SfxEvent& event) {
    const SoundCue* cue = cue_for(event.id);
    if (cue == nullptr || !enabled) {
        return;  // deferred, or audio off: deliberately silent
    }
    if (!engine.has(cue->clip)) {
        if (cue->s6Only && !requireS6) {
            // The interactive game with an S5-only manifest: the S6 rows
            // are optional there, so the cue warns ONCE and stays silent
            // (the mixer keeps running; the game is otherwise intact).
            // The S6 selftest demands the row, so there the same condition
            // is the fatal error below.
            const std::size_t index = static_cast<std::size_t>(event.id);
            if (index < warnedMissing.size() && !warnedMissing[index]) {
                warnedMissing[index] = true;
                std::fprintf(stderr,
                             "sotc-viewer: audio: cue %s names missing clip '%s' — the cue "
                             "stays silent\n",
                             sfx_name(event.id), cue->clip);
            }
            return;
        }
        std::fprintf(stderr, "sotc-viewer: audio: cue %s names missing clip '%s'\n",
                     sfx_name(event.id), cue->clip);
        failed = true;
        return;
    }
    const float gain = cue->gain * event.gain;
    if (event.spatial) {
        engine.play(cue->clip, event.position, gain);
    } else {
        engine.play_ui(cue->clip, gain);
    }
    if (log) {
        // `clip=` is the R1 manifest `name=` (e.g. `valus.impact`), not
        // `bank/name`: the engine indexes clips by that name and exposes no
        // bank. Ledger R16 makes the manifest name the log contract; Task 6
        // parses these tokens.
        std::printf("%s t=%.3f event=%s clip=%s gain=%.2f\n", logPrefix, clock,
                    sfx_name(event.id), cue->clip, gain);
        ++eventCount;
    }
}

void AudioSeam::play_ride(RideSfx id, const Vec3& position, bool spatial) {
    if (!enabled) {
        return;
    }
    rideRng = rideRng * 1664525u + 1013904223u;  // Numerical Recipes LCG
    const std::string clip = ride_cue_clip(id, static_cast<int>(rideRng >> 16));
    if (!engine.has(clip)) {
        const std::size_t index = static_cast<std::size_t>(id);
        if (index < rideWarned.size() && !rideWarned[index]) {
            rideWarned[index] = true;
            std::fprintf(stderr,
                         "sotc-viewer: audio: ride cue %s names missing clip '%s' — the cue "
                         "stays silent (run `make audio` for the S8 slice)\n",
                         ride_sfx_name(id), clip.c_str());
        }
        return;
    }
    const float gain = ride_cue(id).gain;
    if (spatial) {
        engine.play(clip, position, gain);
    } else {
        engine.play_ui(clip, gain);
    }
    ++rideEvents;
    if (id == RideSfx::HoofWalk || id == RideSfx::HoofTrot || id == RideSfx::HoofGallop) {
        ++hooves;
    }
    if (log) {
        std::printf("%s t=%.3f event=%s clip=%s gain=%.2f\n", logPrefix, clock,
                    ride_sfx_name(id), clip.c_str(), gain);
        ++eventCount;
    }
}

void AudioSeam::play_sword(SwordSfx id, const Vec3& position) {
    if (!enabled) {
        return;
    }
    const SwordCue& cue = sword_cue(id);
    if (!engine.has(cue.clip)) {
        const std::size_t index = static_cast<std::size_t>(id);
        if (index < swordWarned.size() && !swordWarned[index]) {
            swordWarned[index] = true;
            std::fprintf(stderr,
                         "sotc-viewer: audio: sword cue %s names missing clip '%s' — the cue "
                         "stays silent (run `make audio` for the S9 slice)\n",
                         sword_sfx_name(id), cue.clip);
        }
        return;
    }
    engine.play(cue.clip, position, cue.gain);
    ++swordPlays[static_cast<std::size_t>(id)];
    if (log) {
        std::printf("%s t=%.3f event=sword_%s clip=%s gain=%.2f\n", logPrefix, clock,
                    sword_sfx_name(id), cue.clip, cue.gain);
        ++eventCount;
    }
}

void AudioSeam::stop_sword() {
    if (enabled) {
        const char* clip = sword_cue(SwordSfx::Shimmer).clip;
        const bool was = engine.is_playing(clip);
        engine.stop(clip);
        if (was && !engine.is_playing(clip)) {
            ++shimmerStops;
        }
    }
}

void AudioSeam::play_all(SfxQueue& queue) {
    for (int i = 0; i < queue.count; ++i) {
        play_event(queue.items[static_cast<std::size_t>(i)]);
    }
    queue.clear();
}

void AudioSeam::start_music() {
    if (!enabled || musicStarted) {
        return;
    }
    if (!engine.has(kMusicClip)) {
        failed = true;
        return;
    }
    engine.play_music(kMusicClip, kMusicGain);
    musicStarted = true;
    musicEverStarted = true;
    if (log) {
        std::printf("%s t=%.3f event=music_start clip=%s gain=%.2f\n", logPrefix, clock,
                    kMusicClip, kMusicGain);
        ++eventCount;
    }
}

void AudioSeam::stop_music() {
    // Idempotent by design: the defeat path calls this every frame while
    // `encounter.defeated`, and only the transition may touch the mixer or
    // the log. The first stop also leaves the queued SFX alone (audio.cpp).
    if (!enabled || !musicStarted) {
        return;
    }
    engine.stop_music();
    musicStarted = false;
    if (log) {
        std::printf("%s t=%.3f event=music_stop clip=%s gain=0.00\n", logPrefix, clock,
                    kMusicClip);
        ++eventCount;
    }
}

void AudioSeam::reset() {
    if (!enabled) {
        return;
    }
    // D5's reset stop must be observable: the reset emits the same
    // `music_stop` the defeat path does (only while the music is actually
    // live — stop_music is idempotent), then every voice goes.
    stop_music();
    engine.reset();  // every voice, music included
}

void AudioSeam::new_stage() {
    if (enabled) {
        engine.reset();
    }
    enabled = false;
    log = false;
    logPrefix = "s5-event";
    failed = false;
    musicStarted = false;
    musicEverStarted = false;
    defeatStop = false;
    clock = 0.0f;
    eventCount = 0;
    requireS6 = false;
    warnedMissing = {};
    rideRng = kRideSfxSeed;
    rideEvents = 0;
    hooves = 0;
    rideWarned = {};
    swordWarned = {};
}

void AudioSeam::toggle_mute() {
    if (enabled) {
        engine.set_muted(!engine.muted());
    }
}

void AudioSeam::update(float dt, const Vec3& listener, float yaw) {
    if (!enabled) {
        return;
    }
    engine.update(dt, listener, yaw);
    if (std::isfinite(dt) && dt > 0.0f) {
        clock += std::min(dt, 1.0f);  // the engine's own dt clamp
    }
}
