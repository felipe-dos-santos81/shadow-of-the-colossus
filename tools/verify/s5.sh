#!/usr/bin/env bash
# s5.sh — the runner's adapter for scripts/s5-acceptance.sh (see common.sh).
#   audio: the S5 map decoded into a private library under this section's dir (the shared library
#          holds run.py's decode of the current map), its 8 entries, names = map events, and the
#          per-cue ffprobe/astats gates, verbatim.
#   gpu:   the s5 timeline + mixdown and its ffprobe/astats gates, and the audio-off identity
#          (relational: the s42-* lines of the audio-off run are byte-identical), verbatim.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
wander_glb="$lib/$player"
audio_map="$repo_root/tools/re/audio/s5_map.json"

section_audio() {
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
need diff
need grep
need sed
need sort
need cut
need tr
need tee
need wc

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter decodes the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the S4.2 regression runs it unchanged)"
[[ -f "$audio_map" ]] || fail "audio cue map not found: $audio_map"
lib="$out_dir/lib"   # the private decode (the frozen script decodes into the shared library)
audio_manifest="$lib/audio/audio.manifest"

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
  # One quiet or hot cue must fail on its own: measure this WAV, not the mix.
  # The delivered clips are normalized to peak <= 0.99 (-0.0873 dBFS), so a
  # peak above -0.08 dBFS is a clipping bug, and RMS at/below -40 dBFS is a
  # silent cue the manifest would happily list.
  cue_stats="$(ffmpeg -hide_banner -nostats -i "$wav" \
    -af astats=metadata=1:reset=0 -f null - 2>&1)" ||
    fail "ffmpeg astats failed on $wav"
  cue_overall="$(printf '%s\n' "$cue_stats" | awk '/\] Overall$/ { grab = 1; next } grab')"
  [[ -n "$cue_overall" ]] || fail "ffmpeg astats printed no Overall block for $wav"
  cue_peak="$(printf '%s\n' "$cue_overall" | awk -F': ' '/Peak level dB/ { print $2; exit }')"
  cue_rms="$(printf '%s\n' "$cue_overall" | awk -F': ' '/RMS level dB/ { print $2; exit }')"
  [[ -n "$cue_peak" && -n "$cue_rms" ]] ||
    fail "could not parse the astats Overall block for $wav"
  awk -v v="$cue_rms" 'BEGIN { exit !(v > -40.0 && v < 0.0) }' ||
    fail "cue $name: RMS ${cue_rms} dBFS is outside (-40, 0) — silent or broken"
  awk -v v="$cue_peak" 'BEGIN { exit !(v <= -0.08) }' ||
    fail "cue $name: peak ${cue_peak} dBFS exceeds the 0.99 ceiling (-0.0873 dBFS)"
  cue_rows="${cue_rows}${cue_rows:+, }${name} ${probe_seconds}s/${probe_channels}ch rms ${cue_rms} dB peak ${cue_peak} dB"
  printf 's5-acceptance: cue %-16s %s s  %s ch  %s Hz  rms %s dB  peak %s dB  %s\n' \
    "$name" "$probe_seconds" "$probe_channels" "$probe_rate" "$cue_rms" "$cue_peak" "$file"
done < "$out_dir/manifest-entries.txt"
keep convert "$convert_out"
manifest_entries_txt="$(cat "$out_dir/manifest-entries.txt")"
keep manifest-entries "$manifest_entries_txt"
keep cues "$cue_rows"
}

section_gpu() {
# field LINE KEY — the value after KEY= up to the next space (the frozen script defines it in its
# decode step, which is this adapter's audio section).
field() {
  local line="$1" key="$2" rest
  rest="${line#*"$key"=}"
  [[ "$rest" != "$line" ]] || fail "field $key missing from: $line"
  printf '%s' "${rest%% *}"
}
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
need diff
need grep
need sed
need sort
need cut
need tr
need tee
need wc

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter decodes the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the S4.2 regression runs it unchanged)"
[[ -f "$audio_map" ]] || fail "audio cue map not found: $audio_map"
mix_wav="$out_dir/mix.wav"
manifest_names="$ms_dir/audio/manifest-names.txt"   # the audio section's decode of the S5 map
[[ -s "$manifest_names" ]] || fail "no $manifest_names: the audio section has not run"

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
[[ "$event_total" = "20" ]] || fail "expected 20 s5-event lines, found $event_total"
event_well_formed="$(grep -c -E "$event_re" "$events_file" || true)"
[[ "$event_well_formed" = "20" ]] ||
  fail "expected 20 well-formed s5-event lines (t=%.3f event=... clip=<manifest name> gain=%.2f), $event_well_formed of $event_total match"

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

# event_lines EVENT — every 1-based line number naming EVENT, one per line.
event_lines() {
  awk -v want="$1" '
    $1 == "s5-event" {
      name = $3; sub(/^event=/, "", name)
      if (name == want) print NR
    }
  ' "$events_file"
}

# event_value_at LINE KEY — the KEY= value on a specific s5-event line.
event_value_at() {
  sed -n "${1}p" "$events_file" | sed -n "s/.* $2=\([^ ]*\).*/\1/p"
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
[[ "$(count_events music_start)" = "2" ]] ||
  fail "music_start appears $(count_events music_start) times (expected 2: the reset's score and the R18 defeat phase)"
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

# The timeline has two club cycles (17.250 -> 17.683 and 18.933 -> 19.367).
# Assert both, in order: a regression that inverts only the second must fail.
club_start_count="$(count_events club_start)"
club_impact_count="$(count_events club_impact)"
[[ "$club_start_count" = "2" ]] ||
  fail "expected two club_start lines, found $club_start_count"
[[ "$club_impact_count" = "2" ]] ||
  fail "expected two club_impact lines, found $club_impact_count"
club_start_lines="$(awk '$1 == "s5-event" { name = $3; sub(/^event=/, "", name); if (name == "club_start") print NR }' "$events_file")"
club_impact_lines="$(awk '$1 == "s5-event" { name = $3; sub(/^event=/, "", name); if (name == "club_impact") print NR }' "$events_file")"
club_start_1="$(printf '%s\n' "$club_start_lines" | sed -n 1p)"
club_start_2="$(printf '%s\n' "$club_start_lines" | sed -n 2p)"
club_impact_1="$(printf '%s\n' "$club_impact_lines" | sed -n 1p)"
club_impact_2="$(printf '%s\n' "$club_impact_lines" | sed -n 2p)"
[[ -n "$club_start_1" && -n "$club_start_2" && -n "$club_impact_1" && -n "$club_impact_2" ]] ||
  fail "could not read both club cycles from the event stream"
[[ "$club_start_1" -lt "$club_impact_1" ]] ||
  fail "club cycle 1: club_impact (line $club_impact_1) is not after club_start (line $club_start_1)"
[[ "$club_impact_1" -lt "$club_start_2" ]] ||
  fail "club cycle 1 impact (line $club_impact_1) does not precede club cycle 2's start (line $club_start_2)"
[[ "$club_start_2" -lt "$club_impact_2" ]] ||
  fail "club cycle 2: club_impact (line $club_impact_2) is not after club_start (line $club_start_2)"
for club_line in "$club_start_1" "$club_start_2"; do
  clip="$(sed -n "${club_line}p" "$events_file" | sed 's/.* clip=//; s/ .*//')"
  [[ "$clip" = "valus.telegraph" ]] ||
    fail "club_start on line $club_line plays clip=$clip (expected valus.telegraph)"
done
for club_line in "$club_impact_1" "$club_impact_2"; do
  clip="$(sed -n "${club_line}p" "$events_file" | sed 's/.* clip=//; s/ .*//')"
  [[ "$clip" = "valus.impact" ]] ||
    fail "club_impact on line $club_line plays clip=$clip (expected valus.impact)"
done

# --- the strike, its hit, and the reaction carry their own clips and gains ----
[[ "$(event_field strike clip)" = "player.swing" ]] ||
  fail "strike plays clip=$(event_field strike clip) (expected player.swing)"
[[ "$(event_field strike gain)" = "0.60" ]] ||
  fail "strike gain=$(event_field strike gain) (expected 0.60)"
[[ "$(event_field strike_hit clip)" = "player.hit" ]] ||
  fail "strike_hit plays clip=$(event_field strike_hit clip) (expected player.hit)"
[[ "$(event_field strike_hit gain)" = "0.60" ]] ||
  fail "strike_hit gain=$(event_field strike_hit gain) (expected 0.60)"
[[ "$(event_field valus_reaction clip)" = "valus.hurt" ]] ||
  fail "valus_reaction plays clip=$(event_field valus_reaction clip) (expected valus.hurt)"
[[ "$(event_field valus_reaction gain)" = "0.60" ]] ||
  fail "valus_reaction gain=$(event_field valus_reaction gain) (expected 0.60)"

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
[[ "$(count_events music_stop)" = "2" ]] ||
  fail "music_stop appears $(count_events music_stop) times (expected 2: the reset and the R18 defeat phase)"
[[ "$(event_field music_stop gain)" = "0.00" ]] ||
  fail "music_stop gain=$(event_field music_stop gain) (expected 0.00)"

# --- the R18 post-reset defeat phase ------------------------------------------
# The shared timeline never defeats Valus, so --s5-selftest starts the score a
# second time after the reset, sets `encounter.defeated`, and runs one step of
# the app's own `if (encounter.defeated) audio.stop_music()`: the two events are
# adjacent, share a timestamp, and land after every s42-* line (the phase is
# after the s42-status). This proves the stop wiring, not the defeat itself —
# that is S4.1 (audio off) plus the user's walk.
music_start_lines="$(event_lines music_start)"
music_stop_lines="$(event_lines music_stop)"
second_start_line="$(printf '%s\n' "$music_start_lines" | sed -n 2p)"
first_stop_line="$(printf '%s\n' "$music_stop_lines" | sed -n 1p)"
second_stop_line="$(printf '%s\n' "$music_stop_lines" | sed -n 2p)"
[[ -n "$second_start_line" && -n "$first_stop_line" && -n "$second_stop_line" ]] ||
  fail "could not read both music_start/music_stop pairs from the event stream"
second_start_t="$(event_value_at "$second_start_line" t)"
second_stop_t="$(event_value_at "$second_stop_line" t)"
[[ "$second_start_line" -gt "$first_stop_line" ]] ||
  fail "the defeat phase's music_start (line $second_start_line) does not follow the reset's music_stop (line $first_stop_line)"
[[ "$second_start_t" = "$second_stop_t" ]] ||
  fail "the defeat phase's start ($second_start_t) and stop ($second_stop_t) are not the same step"
[[ "$second_start_t" = "44.566" ]] ||
  fail "the R18 defeat phase is at t=$second_start_t (expected 44.566, the end of the s42 timeline)"
[[ "$second_stop_line" -eq $((second_start_line + 1)) ]] ||
  fail "the defeat music_stop (line $second_stop_line) is not the line after its music_start (line $second_start_line)"
[[ "$(event_value_at "$second_stop_line" clip)" = "music.boss" ]] ||
  fail "the defeat music_stop plays clip=$(event_value_at "$second_stop_line" clip) (expected music.boss)"
[[ "$(event_value_at "$second_stop_line" gain)" = "0.00" ]] ||
  fail "the defeat music_stop gain=$(event_value_at "$second_stop_line" gain) (expected 0.00)"

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
[[ "$status_line" = "s5-status mixdown=$mix_wav events=20 music=1 defeat_stop=1" ]] ||
  fail "s5-status is '$status_line' (expected 's5-status mixdown=$mix_wav events=20 music=1 defeat_stop=1')"
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
# The music alone cannot reach 0.5: its voice gain is 0.45, the music category
# gain 0.7 and the master headroom 0.85, so the music-only ceiling is ~0.27. A
# peak at or below that (plus the stomp window being flat against the
# music-only window) means no SFX voice contributed to the capture at all.
awk -v v="$mix_peak" 'BEGIN { exit !(v >= 0.5) }' ||
  fail "s5-mix peak=$mix_peak is at or below the music-only ceiling (~0.27): the mix carries no SFX"
awk -v v="$mix_window" 'BEGIN { exit !(v > 0.05) }' ||
  fail "s5-mix window_rms=$mix_window is too quiet (need > 0.05)"

# window_rms_db START DURATION — the RMS of one segment, read by astats.
window_rms_db() {
  local start="$1" duration="$2" out overall
  out="$(ffmpeg -hide_banner -nostats -ss "$start" -t "$duration" -i "$mix_wav" \
    -af astats=metadata=1:reset=0 -f null - 2>&1)" ||
    fail "ffmpeg segment astats failed at ${start}s for ${duration}s"
  overall="$(printf '%s\n' "$out" | awk '/\] Overall$/ { grab = 1; next } grab')"
  printf '%s\n' "$overall" | awk -F': ' '/RMS level dB/ { print $2; exit }'
}

# Music and SFX are both present, not just one: a music-only second (5 s, after
# the music starts at 2.05 s and before the next cue at 13.667 s) against the
# stomp impact (16.233 s). A capture that lost every SFX voice has the same
# level in both windows.
music_window_db="$(window_rms_db 5 1)"
impact_window_db="$(window_rms_db 16.233 0.5)"
[[ -n "$music_window_db" && -n "$impact_window_db" ]] ||
  fail "could not measure the music-only and stomp segment RMS"
awk -v v="$music_window_db" 'BEGIN { exit !(v > -40.0) }' ||
  fail "the music-only window (${music_window_db} dBFS) is silent"
awk -v m="$music_window_db" -v i="$impact_window_db" 'BEGIN { exit !(i >= m + 6.0) }' ||
  fail "the stomp window (${impact_window_db} dBFS) is not 6 dB above the music-only window (${music_window_db} dBFS): the SFX voices are missing"
window_delta="$(awk -v m="$music_window_db" -v i="$impact_window_db" \
  'BEGIN { printf "%.1f", i - m }')"
awk -v f="$mix_frames" 'BEGIN { exit !(f >= 2130400 && f <= 2149600) }' ||
  fail "s5-mix frames=$mix_frames is not 44.58 s ± 0.2 s at 48 kHz"

mix_info="$(ffprobe -v error -select_streams a:0 \
  -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$mix_wav")" ||
  fail "ffprobe failed on $mix_wav"
IFS=, read -r mix_codec mix_rate mix_channels <<<"$mix_info"
[[ "$mix_codec" = "pcm_f32le" ]] || fail "mixdown codec is $mix_codec (expected pcm_f32le)"
[[ "$mix_rate" = "48000" ]] || fail "mixdown rate is $mix_rate (expected 48000)"
[[ "$mix_channels" = "2" ]] || fail "mixdown channels is $mix_channels (expected 2)"
mix_duration="$(ffprobe -v error -select_streams a:0 -show_entries format=duration \
  -of default=nw=1:nk=1 "$mix_wav")"
awk -v d="$mix_duration" 'BEGIN { exit !(d >= 44.3833 && d <= 44.7833) }' ||
  fail "mixdown duration is $mix_duration s (expected 44.5833 s ± 0.2)"
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
astats_infs="$(printf '%s\n' "$astats_overall" | awk -F': ' '/Number of Infs/ { print $2; exit }')"
[[ -n "$astats_peak" && -n "$astats_rms" && -n "$astats_nans" && -n "$astats_infs" ]] ||
  fail "could not parse the astats Overall block"
awk -v p="$astats_peak" 'BEGIN { exit !(p < 0.0) }' ||
  fail "astats peak ${astats_peak} dBFS is at or above 0 dBFS"
awk -v r="$astats_rms" 'BEGIN { exit !(r > -40.0) }' ||
  fail "astats RMS ${astats_rms} dBFS is silent"
awk -v n="$astats_nans" 'BEGIN { exit !(n == 0) }' ||
  fail "astats reports ${astats_nans} NaNs in the mixdown"
awk -v n="$astats_infs" 'BEGIN { exit !(n == 0) }' ||
  fail "astats reports ${astats_infs} Infs in the mixdown"

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
stamp "identity: $s42_off_count s42-* lines identical with the S5 audio observer on and off"
keep mixdown "codec=$mix_codec rate=$mix_rate channels=$mix_channels duration=$mix_duration astats_peak=$astats_peak astats_rms=$astats_rms nans=$astats_nans infs=$astats_infs music_window_db=$music_window_db impact_window_db=$impact_window_db"
}

run_section "$1"
