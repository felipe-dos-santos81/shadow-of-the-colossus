#!/usr/bin/env bash
#
# s5-acceptance.sh — the S5 "The sound of the forbidden land" acceptance pass,
# re-runnable end to end.
#
# Steps (the milestone: the game's own audio decoded at build time, played from
# the existing simulation, and proven by a deterministic headless mixdown):
#
#   1. build the converter solution and the published CLI, and app/ (Release;
#      viewer, tests, shaders copied);
#   2. both suites — `dotnet test tests/BpConvert.Tests` and
#      `ctest --test-dir app/build --output-on-failure`;
#   3. the curated decode — `bp-convert audio` over `tools/re/audio/s5_map.json`
#      into the library: eight manifest entries (the seven attributed cues plus
#      Boss 1), the manifest's names exactly the map's events, every WAV present
#      under the library at 48 kHz with the channel count and duration the
#      manifest claims;
#   4. the deterministic audio timeline — `--s5-selftest --audio-mixdown
#      out/s5/mix.wav` over the S4.2 encounter: exactly 18 ordered `s5-event`
#      lines parsed field by field (music on the first landed hit, a telegraph
#      before every stomp/club impact, hurt light and heavy, the death, the
#      music stop at the reset — not at the death), the `s5-mix` line, and
#      `s5-status` last;
#   5. the mixdown — pcm_f32le, 48 kHz stereo, 44.57 s; RMS/peak/loudest-window
#      from the viewer's own accounting, plus an independent ffmpeg `astats`
#      pass (non-silent, peak below 0 dBFS, no NaN);
#   6. the audio-off identity — `--s42-selftest` with the mixer off, its `s42-*`
#      lines diffed byte for byte against the S5 run's (empty: the audio layer
#      is an additive observer, design D11);
#   7. the S4.2 regression — `scripts/s42-acceptance.sh` unchanged (it runs
#      S4.1, which runs S4, which runs S3), its final `PASS —` and its measured
#      marginal frame cost;
#   8. a summary block with every measured number.
#
# Every viewer and acceptance run is wrapped in `caffeinate -dimsu`: the machine
# suspends idle background work, and an unwrapped 1-frame run has been observed
# at 907 s. Never run two acceptances concurrently.
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (the converter decodes it; required
#                   — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library)
#
# The mixdown, logs and diffs are written into a cleaned out/s5/, so a stale
# mix.wav can never pass as this run's evidence. The decode/attribution gates'
# evidence (out/s5/gate/, out/s5/audition/) belongs to Tasks 0/1 and is kept:
# this script neither reads nor writes it.
#
# Identity is not scriptable: these checks prove presence, order, timing and
# non-silence, not that a cue is the right sound. docs/notes/s5-results.md
# records each attribution method and confidence; the user's ear is the
# identity check, and its listening checklist is PENDING there.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
player="models/shadow/characters/wander/_cmn/wander.glb"
wander_glb="$lib/$player"
audio_map="$repo_root/tools/re/audio/s5_map.json"
audio_manifest="$lib/audio/audio.manifest"
cli="$repo_root/out/bin/bp-convert.dll"
out_dir="$repo_root/out/s5"
mix_wav="$out_dir/mix.wav"
run_log="$repo_root/out/s5-acceptance-run.log"
s42_log="$repo_root/out/s5-s42-regression.log"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s5-acceptance: $*" >&2; exit 1; }

# Every line this script prints lands in the recorded run log as well.
mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

need dotnet
need cmake
need ctest
need caffeinate
need ffprobe
need ffmpeg
need shasum
need awk
need grep
need sed
need sort
need wc

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter decodes the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the S4.2 regression runs it unchanged)"
[[ -f "$audio_map" ]] || fail "audio cue map not found: $audio_map"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"

# --- Step 1: builds -----------------------------------------------------------

stamp "build: BpConvert.sln"
dotnet build "$repo_root/BpConvert.sln" -v q

stamp "publish: src/BpConvert.Cli -> out/bin"
dotnet publish "$repo_root/src/BpConvert.Cli" -c Release -o "$repo_root/out/bin"
[[ -f "$cli" ]] || fail "publish did not produce $cli (invoke it as 'dotnet exec $cli')"

stamp "build: app -> app/build (Release)"
cmake -S "$repo_root/app" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j
[[ -x "$viewer" ]] || fail "build did not produce $viewer"

# --- Step 2: both suites ------------------------------------------------------

stamp "dotnet test: tests/BpConvert.Tests"
dotnet test "$repo_root/tests/BpConvert.Tests" -v q

stamp "ctest: $build_dir"
ctest --test-dir "$build_dir" --output-on-failure

# --- Step 3: the curated decode ----------------------------------------------

# The acceptance's own artifacts are removed first; the gate/audition evidence
# (out/s5/gate, out/s5/audition) is another task's and is left alone.
stamp "clean: $out_dir (except gate/ and audition/)"
mkdir -p "$out_dir"
for stale in "$out_dir"/*; do
  [[ -e "$stale" ]] || continue
  case "$stale" in
    "$out_dir/gate" | "$out_dir/audition") continue ;;
  esac
  rm -rf "$stale"
done

stamp "bp-convert audio: $audio_map"
if ! convert_out="$(caffeinate -dimsu dotnet exec "$cli" audio \
  --root "$game_root" --out "$lib" --map "$audio_map" --force 2>&1)"; then
  printf '%s\n' "$convert_out" >&2
  fail "bp-convert audio exited non-zero"
fi
printf '%s\n' "$convert_out"
grep -q '8 manifest entries' <<<"$convert_out" ||
  fail "bp-convert audio did not report 8 manifest entries"

# field LINE KEY — the value after KEY= up to the next space.
field() {
  local line="$1" key="$2" rest
  rest="${line#*"$key"=}"
  [[ "$rest" != "$line" ]] || fail "field $key missing from: $line"
  printf '%s' "${rest%% *}"
}

stamp "audio manifest: $audio_manifest"
[[ -f "$audio_manifest" ]] || fail "no audio manifest at $audio_manifest after the decode"
manifest_entries="$(grep -c '^name=' "$audio_manifest" || true)"
[[ "$manifest_entries" = "8" ]] ||
  fail "the audio manifest lists $manifest_entries entries (expected 8: seven cues + Boss 1)"

# The manifest must carry exactly the map's curated events: a cue that silently
# failed to decode is a missing name here, not a silent game.
manifest_names="$out_dir/manifest-names.txt"
grep '^name=' "$audio_manifest" | sed 's/^name=//; s/ .*$//' | sort > "$manifest_names"
map_names="$out_dir/map-names.txt"
sed -n '/"cues": \[/,/"deferred": \[/p' "$audio_map" |
  grep -o '"event": "[^"]*"' | sed 's/.*: "//; s/"$//' | sort > "$map_names"
if ! diff -u "$map_names" "$manifest_names" > "$out_dir/manifest-names.diff"; then
  cat "$out_dir/manifest-names.diff" >&2
  fail "the manifest's names are not the map's curated events"
fi
grep '^name=' "$audio_manifest" > "$out_dir/manifest-entries.txt"

cue_rows=""
while IFS= read -r entry; do
  name="$(field "$entry" name)"
  file="$(field "$entry" file)"
  rate="$(field "$entry" rate)"
  channels="$(field "$entry" channels)"
  seconds="$(field "$entry" seconds)"
  wav="$lib/$file"
  [[ -f "$wav" ]] || fail "manifest entry $name: no file at $wav"
  [[ "$rate" = "48000" ]] || fail "manifest entry $name: rate=$rate (expected 48000)"
  info="$(ffprobe -v error -select_streams a:0 \
    -show_entries stream=sample_rate,channels,duration -of csv=p=0 "$wav")" ||
    fail "ffprobe failed on $wav"
  IFS=, read -r probe_rate probe_channels probe_seconds <<<"$info"
  [[ "$probe_rate" = "48000" ]] || fail "ffprobe $wav: ${probe_rate} Hz (expected 48 kHz)"
  [[ "$probe_channels" = "$channels" ]] ||
    fail "ffprobe $wav: ${probe_channels} ch but the manifest claims $channels"
  awk -v a="$probe_seconds" -v b="$seconds" '
    BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 0.05) }' ||
    fail "ffprobe $wav: ${probe_seconds} s but the manifest claims ${seconds} s"
  cue_rows="${cue_rows}${cue_rows:+, }${name} ${probe_seconds}s/${probe_channels}ch"
  printf 's5-acceptance: cue %-16s %s s  %s ch  %s Hz  %s\n' \
    "$name" "$probe_seconds" "$probe_channels" "$probe_rate" "$file"
done < "$out_dir/manifest-entries.txt"

# --- Step 4: the deterministic audio timeline ---------------------------------

stamp "s5-selftest + mixdown: $encounter_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" \
  --s5-selftest --audio-mixdown "$mix_wav" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s5-selftest --audio-mixdown exited non-zero"
fi
printf '%s\n' "$selftest_out"
printf '%s\n' "$selftest_out" > "$out_dir/s5-selftest.txt"

events_file="$out_dir/events.txt"
printf '%s\n' "$selftest_out" | grep -E '^s5-event ' > "$events_file" || true

# t= is %.3f, gain= is %.2f, and clip= is the manifest name (a dotted event
# name, never bank/name — ruling R16), so the anchored pattern rejects a
# malformed or re-namespaced line instead of matching a substring.
event_re='^s5-event t=[0-9]+\.[0-9][0-9][0-9] event=[a-z_]+ clip=[a-z_]+\.[a-z_]+ gain=[0-9]+\.[0-9][0-9]$'
event_total="$(wc -l < "$events_file" | tr -d '[:space:]')"
[[ "$event_total" = "18" ]] || fail "expected 18 s5-event lines, found $event_total"
event_well_formed="$(grep -c -E "$event_re" "$events_file" || true)"
[[ "$event_well_formed" = "18" ]] ||
  fail "expected 18 well-formed s5-event lines (t=%.3f event=... clip=<manifest name> gain=%.2f), $event_well_formed of $event_total match"

# event_line EVENT — the 1-based line number of the first s5-event whose event=
# is EVENT, empty when absent.
event_line() {
  awk -v want="$1" '
    $1 == "s5-event" {
      name = $3; sub(/^event=/, "", name)
      if (name == want) { print NR; exit }
    }
  ' "$events_file"
}

# event_field EVENT KEY — the KEY= value on the first s5-event line for EVENT.
event_field() {
  awk -v want="$1" -v key="$2" '
    $1 == "s5-event" {
      name = $3; sub(/^event=/, "", name)
      if (name != want) next
      for (i = 2; i <= 5; ++i) {
        if (index($i, key "=") == 1) { print substr($i, length(key) + 2); exit }
      }
    }
  ' "$events_file"
}

# count_events EVENT — how many s5-event lines name EVENT (0 when none).
count_events() {
  awk -v want="$1" '
    $1 == "s5-event" {
      name = $3; sub(/^event=/, "", name)
      if (name == want) count += 1
    }
    END { print count + 0 }
  ' "$events_file"
}

for name in strike strike_hit valus_reaction music_start stomp_start stomp_impact \
  hurt_light club_start club_impact hurt_heavy death music_stop; do
  [[ -n "$(event_line "$name")" ]] || fail "missing s5-event $name"
done

# --- music starts on the first landed hit -------------------------------------
first_hit_line="$(event_line strike_hit)"
first_hit_t="$(event_field strike_hit t)"
[[ "$first_hit_t" = "2.050" ]] ||
  fail "the first strike_hit is at t=$first_hit_t (expected 2.050)"
music_start_line="$(event_line music_start)"
music_start_t="$(event_field music_start t)"
[[ "$music_start_t" = "2.050" ]] ||
  fail "music_start is at t=$music_start_t (expected 2.050, the first landed hit)"
[[ "$music_start_line" -gt "$first_hit_line" ]] ||
  fail "music_start (line $music_start_line) is not after the first strike_hit (line $first_hit_line)"
[[ "$(event_field music_start clip)" = "music.boss" ]] ||
  fail "music_start plays clip=$(event_field music_start clip) (expected music.boss)"
[[ "$(count_events music_start)" = "1" ]] ||
  fail "music_start appears $(count_events music_start) times (expected once)"
[[ "$(event_field music_start gain)" = "0.45" ]] ||
  fail "music_start gain=$(event_field music_start gain) (expected 0.45)"

# --- every attack telegraphs before it lands ----------------------------------
stomp_start_line="$(event_line stomp_start)"
stomp_impact_line="$(event_line stomp_impact)"
[[ "$stomp_start_line" -lt "$stomp_impact_line" ]] ||
  fail "stomp_impact (line $stomp_impact_line) is not after stomp_start (line $stomp_start_line)"
[[ "$(event_field stomp_start clip)" = "valus.telegraph" ]] ||
  fail "stomp_start plays clip=$(event_field stomp_start clip) (expected valus.telegraph)"
[[ "$(event_field stomp_impact clip)" = "valus.impact" ]] ||
  fail "stomp_impact plays clip=$(event_field stomp_impact clip) (expected valus.impact)"
club_start_line="$(event_line club_start)"
club_impact_line="$(event_line club_impact)"
[[ "$club_start_line" -lt "$club_impact_line" ]] ||
  fail "club_impact (line $club_impact_line) is not after club_start (line $club_start_line)"
[[ "$(event_field club_start clip)" = "valus.telegraph" ]] ||
  fail "club_start plays clip=$(event_field club_start clip) (expected valus.telegraph)"
[[ "$(event_field club_impact clip)" = "valus.impact" ]] ||
  fail "club_impact plays clip=$(event_field club_impact clip) (expected valus.impact)"

# --- hurt light/heavy land on their impacts, with their own gains --------------
[[ "$(event_field hurt_light t)" = "$(event_field stomp_impact t)" ]] ||
  fail "hurt_light (t=$(event_field hurt_light t)) does not land with stomp_impact (t=$(event_field stomp_impact t))"
[[ "$(event_field hurt_light clip)" = "player.hurt" ]] ||
  fail "hurt_light plays clip=$(event_field hurt_light clip) (expected player.hurt)"
[[ "$(event_field hurt_light gain)" = "1.00" ]] ||
  fail "hurt_light gain=$(event_field hurt_light gain) (expected 1.00)"
[[ "$(event_field hurt_heavy t)" = "$(event_field club_impact t)" ]] ||
  fail "hurt_heavy (t=$(event_field hurt_heavy t)) does not land with club_impact (t=$(event_field club_impact t))"
[[ "$(event_field hurt_heavy clip)" = "player.hurt" ]] ||
  fail "hurt_heavy plays clip=$(event_field hurt_heavy clip) (expected player.hurt)"
[[ "$(event_field hurt_heavy gain)" = "1.25" ]] ||
  fail "hurt_heavy gain=$(event_field hurt_heavy gain) (expected 1.25)"

# --- the death, then the music stop at the reset (not at the death) -----------
death_line="$(event_line death)"
death_t="$(event_field death t)"
[[ "$death_t" = "19.367" ]] || fail "the death is at t=$death_t (expected 19.367)"
[[ "$(event_field death clip)" = "player.death" ]] ||
  fail "the death plays clip=$(event_field death clip) (expected player.death)"
music_stop_line="$(event_line music_stop)"
music_stop_t="$(event_field music_stop t)"
[[ "$music_stop_t" = "20.483" ]] ||
  fail "music_stop is at t=$music_stop_t (expected 20.483, the reset)"
[[ "$music_stop_line" -gt "$death_line" ]] ||
  fail "music_stop (line $music_stop_line) is not after the death (line $death_line)"
[[ "$(printf '%s\n' "$selftest_out" | grep -c -E '^s5-event t=19\.367 event=music_stop ' || true)" = "0" ]] ||
  fail "the music stops at the death (t=19.367); it must stop at the reset"
[[ "$(event_field music_stop clip)" = "music.boss" ]] ||
  fail "music_stop plays clip=$(event_field music_stop clip) (expected music.boss)"
[[ "$(count_events music_stop)" = "1" ]] ||
  fail "music_stop appears $(count_events music_stop) times (expected once)"
[[ "$(event_field music_stop gain)" = "0.00" ]] ||
  fail "music_stop gain=$(event_field music_stop gain) (expected 0.00)"

# --- every clip= is a manifest name (R16), never bank/name --------------------
awk '$1 == "s5-event" { c = $4; sub(/^clip=/, "", c); print c }' "$events_file" |
  sort -u > "$out_dir/event-clips.txt"
while IFS= read -r clip; do
  [[ -n "$clip" ]] || continue
  case "$clip" in
    */*) fail "clip=$clip is bank/name; the log carries the manifest name (R16)" ;;
  esac
  grep -q -x -F "$clip" "$manifest_names" ||
    fail "clip=$clip is not one of the audio manifest's names"
done < "$out_dir/event-clips.txt"

# --- s5-status is last and agrees with the stream -----------------------------
status_line="$(printf '%s\n' "$selftest_out" | grep -E '^s5-status ' || true)"
[[ -n "$status_line" ]] || fail "no s5-status line"
[[ "$(printf '%s\n' "$selftest_out" | grep -c -E '^s5-status ' || true)" = "1" ]] ||
  fail "expected exactly one s5-status line"
[[ "$status_line" = "s5-status mixdown=$mix_wav events=18 music=1" ]] ||
  fail "s5-status is '$status_line' (expected 's5-status mixdown=$mix_wav events=18 music=1')"
last_line="$(printf '%s\n' "$selftest_out" | awk 'NF { line = $0 } END { print line }')"
[[ "$last_line" = "$status_line" ]] ||
  fail "s5-status is not the last line; the last line is '$last_line'"

# --- Step 5: the mixdown ------------------------------------------------------

[[ -s "$mix_wav" ]] || fail "the selftest wrote no mixdown at $mix_wav"
mix_line="$(printf '%s\n' "$selftest_out" | grep -m1 -E '^s5-mix ' || true)"
[[ -n "$mix_line" ]] || fail "no s5-mix line"
mix_frames="$(field "$mix_line" frames)"
mix_rms="$(field "$mix_line" rms)"
mix_peak="$(field "$mix_line" peak)"
mix_window="$(field "$mix_line" window_rms)"
awk -v v="$mix_rms" 'BEGIN { exit !(v > 0.01) }' ||
  fail "s5-mix rms=$mix_rms is silent (need > 0.01)"
awk -v v="$mix_peak" 'BEGIN { exit !(v <= 0.99) }' ||
  fail "s5-mix peak=$mix_peak clips (need <= 0.99)"
awk -v v="$mix_window" 'BEGIN { exit !(v > 0.05) }' ||
  fail "s5-mix window_rms=$mix_window is too quiet (need > 0.05: music and SFX both audible)"
awk -v f="$mix_frames" 'BEGIN { exit !(f >= 2129760 && f <= 2148960) }' ||
  fail "s5-mix frames=$mix_frames is not 44.57 s ± 0.2 s at 48 kHz"

mix_info="$(ffprobe -v error -select_streams a:0 \
  -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$mix_wav")" ||
  fail "ffprobe failed on $mix_wav"
IFS=, read -r mix_codec mix_rate mix_channels <<<"$mix_info"
[[ "$mix_codec" = "pcm_f32le" ]] || fail "mixdown codec is $mix_codec (expected pcm_f32le)"
[[ "$mix_rate" = "48000" ]] || fail "mixdown rate is $mix_rate (expected 48000)"
[[ "$mix_channels" = "2" ]] || fail "mixdown channels is $mix_channels (expected 2)"
mix_duration="$(ffprobe -v error -select_streams a:0 -show_entries format=duration \
  -of default=nw=1:nk=1 "$mix_wav")"
awk -v d="$mix_duration" 'BEGIN { exit !(d >= 44.37 && d <= 44.77) }' ||
  fail "mixdown duration is $mix_duration s (expected 44.57 s ± 0.2)"
awk -v frames="$mix_frames" -v d="$mix_duration" '
  BEGIN { diff = frames - d * 48000; if (diff < 0) diff = -diff; exit !(diff <= 1.0) }' ||
  fail "s5-mix frames=$mix_frames disagrees with the WAV's ${mix_duration} s at 48 kHz"

# The independent decode: astats reads the samples, not the viewer's summary.
astats_out="$(ffmpeg -hide_banner -nostats -i "$mix_wav" \
  -af astats=metadata=1:reset=0 -f null - 2>&1)" || fail "ffmpeg astats failed on $mix_wav"
astats_overall="$(printf '%s\n' "$astats_out" | awk '/\] Overall$/ { grab = 1; next } grab')"
[[ -n "$astats_overall" ]] || fail "ffmpeg astats printed no Overall block"
astats_peak="$(printf '%s\n' "$astats_overall" | awk -F': ' '/Peak level dB/ { print $2; exit }')"
astats_rms="$(printf '%s\n' "$astats_overall" | awk -F': ' '/RMS level dB/ { print $2; exit }')"
astats_nans="$(printf '%s\n' "$astats_overall" | awk -F': ' '/Number of NaNs/ { print $2; exit }')"
[[ -n "$astats_peak" && -n "$astats_rms" && -n "$astats_nans" ]] ||
  fail "could not parse the astats Overall block"
awk -v p="$astats_peak" 'BEGIN { exit !(p < 0.0) }' ||
  fail "astats peak ${astats_peak} dBFS is at or above 0 dBFS"
awk -v r="$astats_rms" 'BEGIN { exit !(r > -40.0) }' ||
  fail "astats RMS ${astats_rms} dBFS is silent"
awk -v n="$astats_nans" 'BEGIN { exit !(n == 0) }' ||
  fail "astats reports ${astats_nans} NaNs in the mixdown"

# --- Step 6: the audio-off identity (the observer changes no s42-* value) -----

stamp "s42-selftest with audio off (identity check)"
if ! s42_off_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" --s42-selftest 2>&1)"; then
  printf '%s\n' "$s42_off_out" >&2
  fail "--s42-selftest with audio off exited non-zero"
fi
printf '%s\n' "$s42_off_out" > "$out_dir/s42-audio-off.txt"
printf '%s\n' "$s42_off_out" | grep -E '^s42-' > "$out_dir/s42-audio-off.lines" || true
printf '%s\n' "$selftest_out" | grep -E '^s42-' > "$out_dir/s42-audio-on.lines" || true
s42_off_count="$(wc -l < "$out_dir/s42-audio-off.lines" | tr -d '[:space:]')"
[[ "$s42_off_count" -ge 10 ]] ||
  fail "the audio-off run printed only $s42_off_count s42-* lines"
if ! diff -u "$out_dir/s42-audio-off.lines" "$out_dir/s42-audio-on.lines" \
  > "$out_dir/s42-identity.diff"; then
  cat "$out_dir/s42-identity.diff" >&2
  fail "the audio layer changed the s42-* lines (design D11): see $out_dir/s42-identity.diff"
fi
stamp "identity: $s42_off_count s42-* lines identical with the mixer on and off"

# --- Step 7: the S4.2 regression (which runs S4.1, S4 and S3) -----------------

stamp "s42 regression: scripts/s42-acceptance.sh (it runs S4.1, S4 and S3)"
if ! SOTC_LIB="$lib" SOTC_GAME_ROOT="$game_root" \
  caffeinate -dimsu "$repo_root/scripts/s42-acceptance.sh" > "$s42_log" 2>&1; then
  tail -40 "$s42_log" >&2
  fail "the S4.2 acceptance regression failed (log: $s42_log)"
fi
grep -q '^s42-acceptance: PASS — ' "$s42_log" ||
  fail "the S4.2 regression did not print its final PASS — line"
s42_perf_line="$(grep -m1 -E '^s42-acceptance: perf ' "$s42_log" || true)"
[[ -n "$s42_perf_line" ]] || fail "the S4.2 regression printed no perf line"
s42_marginal="$(printf '%s\n' "$s42_perf_line" | sed -E 's/.*marginal ([0-9.]+) ms.*/\1/')"
[[ "$s42_marginal" != "$s42_perf_line" ]] ||
  fail "could not parse the marginal frame cost from: $s42_perf_line"
awk -v ms="$s42_marginal" 'BEGIN { exit !(ms <= 16.7) }' ||
  fail "the marginal frame cost ${s42_marginal} ms is outside the 60 fps budget (16.7 ms)"
s41_status_line="$(grep -m1 '^s42-acceptance: s41 status ' "$s42_log" |
  sed 's/^s42-acceptance: s41 status *//' || true)"
s4_status_line="$(grep -m1 '^s42-acceptance: s4 status ' "$s42_log" |
  sed 's/^s42-acceptance: s4 status *//' || true)"
s3_status_line="$(grep -m1 '^s42-acceptance: s3 status ' "$s42_log" |
  sed 's/^s42-acceptance: s3 status *//' || true)"
[[ -n "$s41_status_line" && -n "$s4_status_line" && -n "$s3_status_line" ]] ||
  fail "the S4.2 regression printed an empty S4.1/S4/S3 status line"
printf '%s\n' "$s42_perf_line"

# --- Step 8: summary ----------------------------------------------------------

stamp "summary"
printf 's5-acceptance: library      %s\n' "$lib"
printf 's5-acceptance: map          %s\n' "$audio_map"
printf 's5-acceptance: manifest     %s\n' "$audio_manifest"
printf 's5-acceptance: manifest sha %s\n' "$(shasum -a 256 "$audio_manifest" | cut -d' ' -f1)"
printf 's5-acceptance: mixdown      %s\n' "$mix_wav"
printf 's5-acceptance: mixdown sha  %s\n' "$(shasum -a 256 "$mix_wav" | cut -d' ' -f1)"
printf 's5-acceptance: cues         %s\n' "$cue_rows"
while IFS= read -r event_line_out; do
  printf 's5-acceptance: %s\n' "$event_line_out"
done < "$events_file"
printf 's5-acceptance: mix          %s\n' "$mix_line"
printf 's5-acceptance: astats       peak %s dBFS, RMS %s dBFS, NaNs %s\n' \
  "$astats_peak" "$astats_rms" "$astats_nans"
printf 's5-acceptance: wav          %s  %s Hz  %s ch  %s s\n' \
  "$mix_codec" "$mix_rate" "$mix_channels" "$mix_duration"
printf 's5-acceptance: identity     %s s42-* lines identical with the mixer on and off\n' \
  "$s42_off_count"
printf 's5-acceptance: status       %s\n' "$status_line"
printf 's5-acceptance: s42 perf     %s\n' "$s42_perf_line"
printf 's5-acceptance: s41 status   %s\n' "$s41_status_line"
printf 's5-acceptance: s4 status    %s\n' "$s4_status_line"
printf 's5-acceptance: s3 status    %s\n' "$s3_status_line"
printf 's5-acceptance: PASS — build, both suites, 8 curated clips (7 attributed cues + Boss 1) decoded at 48 kHz (manifest names = map events, every file present), %s ordered s5-events (music on the first landed hit at %s s, telegraph before every stomp/club impact, hurt light/heavy at their impacts, death at %s s, music stop at the reset %s s), mixdown %s frames / %s s %s %s ch (rms %s, peak %s, window_rms %s; astats peak %s dBFS, RMS %s dBFS, no NaN), s42-* identical with audio off, marginal %s ms/frame, S4.2+S4.1+S4+S3 regressions green\n' \
  "$event_total" "$music_start_t" "$death_t" "$music_stop_t" "$mix_frames" \
  "$mix_duration" "$mix_codec" "$mix_channels" "$mix_rms" "$mix_peak" "$mix_window" \
  "$astats_peak" "$astats_rms" "$s42_marginal"
