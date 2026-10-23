#!/usr/bin/env bash
# s6.sh — the runner's adapter for scripts/s6-acceptance.sh (see common.sh).
#   probes: the S6 asset gate (tools/re/s6/s6_asset_gate.py --check).
#   audio:  the S6 map decoded into a private library: 10 entries, names = map events, per-cue gates.
#   gpu:    the death timeline, the five stills, the crop-local pixel guards (heredoc extracted),
#           the s6-perf gates — every gate verbatim. The nested S5 chain is s5.sh … s3.sh.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
wander_glb="$lib/$player"
audio_map="$repo_root/tools/re/audio/s6_map.json"
stills_dir="$out_dir/stills"
selftest_txt="$out_dir/s6-selftest.txt"
pixels_txt="$out_dir/s6-pixels.txt"
pixel_py="$out_dir/s6-pixels.py"   # cleaned below; the heredoc itself is extracted

section_probes() {
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
need tee
need tr
need wc
need python3
need od
need cut

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter decodes the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the frozen S5 acceptance runs it unchanged)"
[[ -f "$audio_map" ]] || fail "audio cue map not found: $audio_map"


# --- Step 2b: the S6 asset gate ----------------------------------------------

# The gate pins the four tendril atlases by SHA-256, the death bundle's bytes
# and the corpse's six-material binding set (docs/notes/s6-recon.md). It is
# read-only on both trees and needs the game root the script already requires.
gate_script="$repo_root/tools/re/s6/s6_asset_gate.py"
[[ -f "$gate_script" ]] || fail "the S6 asset gate is missing: $gate_script"
stamp "S6 asset gate: the death bundle, the corpse and the four tendril atlases"
if ! gate_out="$(python3 "$gate_script" --root "$game_root" --out "$lib" --check 2>&1)"; then
  printf '%s\n' "$gate_out" >&2
  fail "the S6 asset gate failed (the death bundle, the corpse or the tendril atlases)"
fi
printf '%s\n' "$gate_out"
keep s6-asset-gate "$gate_out"
}

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
need tee
need tr
need wc
need python3
need od
need cut

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter decodes the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the frozen S5 acceptance runs it unchanged)"
[[ -f "$audio_map" ]] || fail "audio cue map not found: $audio_map"

lib="$out_dir/lib"   # the private decode (the frozen script decodes into the shared library)
audio_manifest="$lib/audio/audio.manifest"
mkdir -p "$out_dir"

stamp "bp-convert audio: $audio_map"
if ! convert_out="$(caffeinate -dimsu dotnet exec "$cli" audio \
  --root "$game_root" --out "$lib" --map "$audio_map" --force 2>&1)"; then
  printf '%s\n' "$convert_out" >&2
  fail "bp-convert audio exited non-zero"
fi
printf '%s\n' "$convert_out"
grep -q '10 manifest entries' <<<"$convert_out" ||
  fail "bp-convert audio did not report 10 manifest entries"

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
[[ "$manifest_entries" = "10" ]] ||
  fail "the audio manifest lists $manifest_entries entries (expected 10: the S5 eight + valus.death + valus.club_swing)"

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
  # One quiet or hot cue must fail on its own: the delivered clips are
  # normalized to peak <= 0.99, so a peak above -0.08 dBFS is a clipping bug,
  # and an RMS at/below -40 dBFS is a silent cue.
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
  printf 's6-acceptance: cue %-18s %s s  %s ch  %s Hz  rms %s dB  peak %s dB  %s\n' \
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
need tee
need tr
need wc
need python3
need od
need cut

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter decodes the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the frozen S5 acceptance runs it unchanged)"
[[ -f "$audio_map" ]] || fail "audio cue map not found: $audio_map"

manifest_names="$ms_dir/audio/manifest-names.txt"   # the audio section's decode of the S6 map
[[ -s "$manifest_names" ]] || fail "no $manifest_names: the audio section has not run"

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir"
mkdir -p "$stills_dir"
rm -f "$selftest_txt" "$pixels_txt" "$pixel_py"

# --- Step 5: the deterministic death timeline ---------------------------------

stamp "s6-selftest + stills: $encounter_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" --s6-selftest \
  --s6-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s6-selftest --s6-stills exited non-zero"
fi
printf '%s\n' "$selftest_out"
printf '%s\n' "$selftest_out" > "$selftest_txt"

# line_count REGEX — how many lines match (never fails on zero).
line_count() {
  printf '%s\n' "$selftest_out" | grep -c -E "$1" || true
}

# one_line REGEX LABEL — the single matching line, or a loud failure.
one_line() {
  local regex="$1" label="$2" count line
  count="$(line_count "$regex")"
  [[ "$count" = "1" ]] || fail "$label: expected exactly one line, found $count"
  line="$(printf '%s\n' "$selftest_out" | grep -m1 -E "$regex")" || fail "$label: missing"
  printf '%s' "$line"
}

# The thirteen s6-* lines: four phases, five events, the perf line, the death
# status, the status and the reset.
s6_total="$(line_count '^s6-')"
[[ "$s6_total" = "13" ]] ||
  fail "expected 13 s6-* lines (4 phases + 5 events + the perf line + death + status + reset), found $s6_total"
[[ "$(line_count '^s6-phase ')" = "4" ]] || fail "expected four s6-phase lines"
[[ "$(line_count '^s6-event ')" = "5" ]] || fail "expected five s6-event lines"
[[ "$(line_count '^s6-perf ')" = "1" ]] || fail "expected one s6-perf line"
[[ "$(line_count '^s6-death ')" = "1" ]] || fail "expected one s6-death line"
[[ "$(line_count '^s6-status ')" = "1" ]] || fail "expected one s6-status line"
[[ "$(line_count '^s6-reset ')" = "1" ]] || fail "expected one s6-reset line"

# --- the phase machine: Idle -> Motion -> Collapse -> Done --------------------

phase_order="$(printf '%s\n' "$selftest_out" | grep -E '^s6-phase ' |
  awk '{ print $2 }' | sed 's/phase=//' | tr '\n' ' ')"
[[ "$phase_order" = "Idle Motion Collapse Done " ]] ||
  fail "the phase order is '$phase_order' (expected 'Idle Motion Collapse Done ')"

idle_line="$(one_line '^s6-phase phase=Idle ' "s6-phase Idle")"
motion_line="$(one_line '^s6-phase phase=Motion ' "s6-phase Motion")"
collapse_line="$(one_line '^s6-phase phase=Collapse ' "s6-phase Collapse")"
done_line="$(one_line '^s6-phase phase=Done ' "s6-phase Done")"

[[ "$(field "$idle_line" total)" = "0.000" ]] || fail "s6-phase Idle total=$(field "$idle_line" total) (expected 0.000)"
[[ "$(field "$idle_line" lock)" = "0" ]] || fail "s6-phase Idle lock=$(field "$idle_line" lock) (expected 0)"
[[ "$(field "$idle_line" corpse)" = "0" ]] || fail "s6-phase Idle corpse=$(field "$idle_line" corpse) (expected 0)"
[[ "$(field "$idle_line" roots)" = "0" ]] || fail "s6-phase Idle roots=$(field "$idle_line" roots) (expected 0)"

[[ "$(field "$motion_line" total)" = "0.000" ]] || fail "s6-phase Motion total=$(field "$motion_line" total) (expected 0.000)"
[[ "$(field "$motion_line" lock)" = "1" ]] || fail "s6-phase Motion lock=$(field "$motion_line" lock) (expected 1)"
[[ "$(field "$motion_line" corpse)" = "0" ]] || fail "s6-phase Motion corpse=$(field "$motion_line" corpse) (expected 0)"
[[ "$(field "$motion_line" roots)" = "2" ]] || fail "s6-phase Motion roots=$(field "$motion_line" roots) (expected 2)"

# Collapse lands on the death clip's end (6.166877 s); the step that reaches it
# prints the overshoot, so the window is a quarter of a second wide.
collapse_total="$(field "$collapse_line" total)"
awk -v t="$collapse_total" 'BEGIN { exit !(t >= 6.15 && t <= 6.20) }' ||
  fail "s6-phase Collapse total=$collapse_total (expected the clip's end, 6.166877 s +/- 0.02)"
[[ "$(field "$collapse_line" lock)" = "1" ]] || fail "s6-phase Collapse lock=$(field "$collapse_line" lock) (expected 1)"
[[ "$(field "$collapse_line" corpse)" = "1" ]] || fail "s6-phase Collapse corpse=$(field "$collapse_line" corpse) (expected 1)"
[[ "$(field "$collapse_line" roots)" = "2" ]] || fail "s6-phase Collapse roots=$(field "$collapse_line" roots) (expected 2)"

[[ "$(field "$done_line" total)" = "8.000" ]] || fail "s6-phase Done total=$(field "$done_line" total) (expected 8.000)"
[[ "$(field "$done_line" lock)" = "0" ]] || fail "s6-phase Done lock=$(field "$done_line" lock) (expected 0: the control returns)"
[[ "$(field "$done_line" corpse)" = "1" ]] || fail "s6-phase Done corpse=$(field "$done_line" corpse) (expected 1)"
[[ "$(field "$done_line" roots)" = "2" ]] || fail "s6-phase Done roots=$(field "$done_line" roots) (expected 2)"

# --- the five events, in order ------------------------------------------------

events_file="$out_dir/s6-events.txt"
printf '%s\n' "$selftest_out" | grep -E '^s6-event ' > "$events_file" || true

# t= is %.3f, gain= is %.2f and clip= is the manifest name (a dotted event
# name, never bank/name — ruling R16), so the anchored pattern rejects a
# malformed or re-namespaced line instead of matching a substring.
event_re='^s6-event t=[0-9]+\.[0-9][0-9][0-9] event=[a-z_]+ clip=[a-z_]+\.[a-z_]+ gain=[0-9]+\.[0-9][0-9]$'
event_well_formed="$(grep -c -E "$event_re" "$events_file" || true)"
[[ "$event_well_formed" = "5" ]] ||
  fail "expected 5 well-formed s6-event lines (t=%.3f event=... clip=<manifest name> gain=%.2f), $event_well_formed of 5 match"

event_order="$(awk '{ print $3 }' "$events_file" | sed 's/event=//' | tr '\n' ' ')"
[[ "$event_order" = "club_start club_swing music_start music_stop valus_defeat " ]] ||
  fail "the event order is '$event_order' (expected 'club_start club_swing music_start music_stop valus_defeat ')"

# event_field EVENT KEY — the KEY= value on the s6-event line for EVENT.
event_field() {
  awk -v want="$1" -v key="$2" '
    $1 == "s6-event" {
      name = $3; sub(/^event=/, "", name)
      if (name != want) next
      for (i = 2; i <= 5; ++i) {
        if (index($i, key "=") == 1) { print substr($i, length(key) + 2); exit }
      }
    }
  ' "$events_file"
}

[[ "$(event_field club_start clip)" = "valus.telegraph" ]] ||
  fail "club_start plays clip=$(event_field club_start clip) (expected valus.telegraph)"
[[ "$(event_field club_start gain)" = "0.60" ]] ||
  fail "club_start gain=$(event_field club_start gain) (expected 0.60)"
[[ "$(event_field club_swing clip)" = "valus.club_swing" ]] ||
  fail "club_swing plays clip=$(event_field club_swing clip) (expected valus.club_swing)"
[[ "$(event_field club_swing gain)" = "0.70" ]] ||
  fail "club_swing gain=$(event_field club_swing gain) (expected 0.70)"
[[ "$(event_field club_swing t)" = "0.300" ]] ||
  fail "club_swing is at t=$(event_field club_swing t) (expected 0.300, the swing phase)"
[[ "$(event_field music_start clip)" = "music.boss" ]] ||
  fail "music_start plays clip=$(event_field music_start clip) (expected music.boss)"
[[ "$(event_field music_start gain)" = "0.45" ]] ||
  fail "music_start gain=$(event_field music_start gain) (expected 0.45)"
[[ "$(event_field music_stop clip)" = "music.boss" ]] ||
  fail "music_stop plays clip=$(event_field music_stop clip) (expected music.boss)"
[[ "$(event_field music_stop gain)" = "0.00" ]] ||
  fail "music_stop gain=$(event_field music_stop gain) (expected 0.00)"
[[ "$(event_field valus_defeat clip)" = "valus.death" ]] ||
  fail "valus_defeat plays clip=$(event_field valus_defeat clip) (expected valus.death)"
[[ "$(event_field valus_defeat gain)" = "0.80" ]] ||
  fail "valus_defeat gain=$(event_field valus_defeat gain) (expected 0.80)"
[[ "$(event_field valus_defeat t)" = "0.400" ]] ||
  fail "valus_defeat is at t=$(event_field valus_defeat t) (expected 0.400, the tendril burst)"

# The score stops at the defeat (the death clock's 0.000), not later. This is
# true by construction in the selftest (the scripted defeat starts and stops
# the score back to back), so it is a *consistency* check, not the proof that
# the score stops on a real defeat: that proof is the frozen S5 acceptance's
# R18 defeat phase (a second music_stop after a reset), which the S5 run above
# asserts.
[[ "$(event_field music_start t)" = "$(event_field music_stop t)" ]] ||
  fail "the score's start ($(event_field music_start t)) and its stop ($(event_field music_stop t)) are not the same step"

# Every clip= is a manifest name (R16), never bank/name.
awk '$1 == "s6-event" { c = $4; sub(/^clip=/, "", c); print c }' "$events_file" |
  sort -u > "$out_dir/event-clips.txt"
while IFS= read -r clip; do
  [[ -n "$clip" ]] || continue
  case "$clip" in
    */*) fail "clip=$clip is bank/name; the log carries the manifest name (R16)" ;;
  esac
  grep -q -x -F "$clip" "$manifest_names" ||
    fail "clip=$clip is not one of the audio manifest's names"
done < "$out_dir/event-clips.txt"

# --- the death status, the status and the reset -------------------------------

death_line="$(one_line '^s6-death ' "s6-death")"
[[ "$(field "$death_line" clip)" = "minotaur_a_death_front" ]] ||
  fail "s6-death clip=$(field "$death_line" clip) (expected minotaur_a_death_front)"
[[ "$(field "$death_line" total)" = "8.000" ]] || fail "s6-death total=$(field "$death_line" total) (expected 8.000)"
[[ "$(field "$death_line" corpse)" = "1" ]] || fail "s6-death corpse=$(field "$death_line" corpse) (expected 1)"
[[ "$(field "$death_line" health)" = "0" ]] || fail "s6-death health=$(field "$death_line" health) (expected 0)"
[[ "$(field "$death_line" camera)" = "follow" ]] ||
  fail "s6-death camera=$(field "$death_line" camera) (expected follow: the camera is handed back)"

status_line="$(one_line '^s6-status ' "s6-status")"
[[ "$status_line" = "s6-status defeated=1 corpse=1 roots=2 player_dead=0" ]] ||
  fail "s6-status is '$status_line' (expected 's6-status defeated=1 corpse=1 roots=2 player_dead=0')"

reset_line="$(one_line '^s6-reset ' "s6-reset")"
[[ "$reset_line" = "s6-reset phase=Idle corpse=0 lock=0 valus=100 player=100" ]] ||
  fail "s6-reset is '$reset_line' (expected 's6-reset phase=Idle corpse=0 lock=0 valus=100 player=100')"

# The window stays windowed and the cursor stays free (anti-goal A5).
status_window="$(one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' "s1-status")"

# --- Step 6: the stills --------------------------------------------------------

# span_for PATH — the largest channel span from the s1-pixels line the viewer
# printed immediately before the s1-screenshot line for PATH.
span_for() {
  local want="$1"
  printf '%s\n' "$selftest_out" | awk -v want="$want" '
    /^s1-pixels / { line = $0 }
    /^s1-screenshot / { if ($2 == "path=" want) { print line; exit } }
  '
}

# size_for PATH — "WIDTH HEIGHT" from the s1-screenshot line for PATH.
size_for() {
  local want="$1"
  printf '%s\n' "$selftest_out" | awk -v want="$want" '
    /^s1-screenshot / {
      if ($2 == "path=" want) {
        w = $3; h = $4;
        sub(/^width=/, "", w); sub(/^height=/, "", h);
        print w, h; exit
      }
    }
  '
}

# check_png NAME PATH — magic, byte floor, 2560x1440, and the viewer's own
# non-uniformity span line.
check_png() {
  local name="$1" path="$2" magic bytes pixels_line min_csv max_csv size w h
  local min_r min_g min_b max_r max_g max_b d largest
  [[ -f "$path" ]] || fail "$name: no PNG at $path"
  magic="$(od -An -tx1 -N4 "$path" | tr -d ' \n')"
  [[ "$magic" = "89504e47" ]] || fail "$name: $path lacks the PNG magic (got $magic)"
  bytes="$(wc -c < "$path" | tr -d '[:space:]')"
  [[ "$bytes" -gt 5000 ]] || fail "$name: $path is only $bytes bytes (need > 5000)"
  size="$(size_for "$path")"
  [[ -n "$size" ]] || fail "$name: no s1-screenshot line for $path"
  read -r w h <<<"$size"
  [[ "$w" = "2560" && "$h" = "1440" ]] ||
    fail "$name: $path is ${w}x${h} (need 2560x1440)"
  pixels_line="$(span_for "$path")"
  [[ -n "$pixels_line" ]] || fail "$name: no s1-pixels line for $path"
  min_csv="${pixels_line#*min=}"; min_csv="${min_csv%% *}"
  max_csv="${pixels_line#*max=}"; max_csv="${max_csv%% *}"
  IFS=, read -r min_r min_g min_b <<<"$min_csv"
  IFS=, read -r max_r max_g max_b <<<"$max_csv"
  largest=$((max_r - min_r))
  d=$((max_g - min_g)); if ((d > largest)); then largest=$d; fi
  d=$((max_b - min_b)); if ((d > largest)); then largest=$d; fi
  [[ "$largest" -ge 30 ]] ||
    fail "$name: uniform frame (largest channel span $largest < 30): $pixels_line"
  printf '%s|%s|%s\n' "$path" "$bytes" "$largest"
}

stills=""
for name in motion camera tendrils corpse tendrils-control; do
  path="$stills_dir/$name.png"
  count="$(line_count "^s1-screenshot path=$path width=2560 height=1440$")"
  [[ "$count" = "1" ]] ||
    fail "still $name: expected one s1-screenshot line for $path, found $count"
  row="$(check_png "$name" "$path")"
  stills="${stills}${stills:+ }$row"
done

# --- Step 7: the pixel guards (crop-local, never whole-frame counts) -----------

stamp "pixel guards: the burst against its control, the corpse, the framings, the HUD"
pixel_py="$(heredoc_py s6 1)"

if ! pixels_out="$(python3 "$pixel_py" "$stills_dir" 2>&1)"; then
  printf '%s\n' "$pixels_out" >&2
  fail "the pixel guards failed"
fi
printf '%s\n' "$pixels_out"
printf '%s\n' "$pixels_out" > "$pixels_txt"
keep pixels "$pixels_out"

if ! tendrils_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s6-tendrils ')"; then
  fail "pixel guards: no s6-tendrils line"
fi
if ! corpse_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s6-corpse ')"; then
  fail "pixel guards: no s6-corpse line"
fi
if ! camera_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s6-camera ')"; then
  fail "pixel guards: no s6-camera line"
fi
if ! motion_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s6-motion ')"; then
  fail "pixel guards: no s6-motion line"
fi
[[ "$(printf '%s\n' "$pixels_out" | grep -c -E '^s6-hud ' || true)" = "4" ]] ||
  fail "pixel guards: expected four s6-hud lines (one per still)"

# --- the burst: the effect pass's own pixels, inside the measured region -----
# Thresholds are the measured values with a wide margin (the measured run:
# 14 597 changed pixels, the violet ribbon 1 943 / 275 strong, 1 828 darkened by > 20 luma;
# the glow-only ribbon before it darkened 59).
tendrils_changed="$(field "$tendrils_line" changed)"
tendrils_bbox="$(field "$tendrils_line" bbox)"
tendrils_violet="$(field "$tendrils_line" violet)"
tendrils_strong="$(field "$tendrils_line" strong)"
tendrils_dark="$(field "$tendrils_line" dark)"
[[ "$tendrils_changed" -ge 8000 ]] ||
  fail "the burst covers only $tendrils_changed pixels (need >= 8000): the effect pass is not drawing"
IFS=, read -r tb_x0 tb_y0 tb_x1 tb_y1 <<<"$tendrils_bbox"
((tb_x0 >= 600 && tb_y0 >= 0 && tb_x1 <= 1800 && tb_y1 <= 1150)) ||
  fail "the burst's pixels reach $tendrils_bbox, outside the measured effect region (600,0)-(1800,1150)"
[[ "$tendrils_violet" -ge 800 ]] ||
  fail "the burst carries only $tendrils_violet violet pixels (need >= 800): the vein ribbon's colour is missing"
[[ "$tendrils_strong" -ge 100 ]] ||
  fail "the burst carries only $tendrils_strong strongly violet pixels (need >= 100)"
[[ "$tendrils_dark" -ge 900 ]] ||
  fail "the burst darkens only $tendrils_dark pixels (need >= 900): the tendrils are not black"

# --- the corpse: the black veins on the bright sand ----------------------------
# (measured: 15 449 veins, 32 527 bright sand pixels in the crop).
corpse_veins="$(field "$corpse_line" veins)"
corpse_sand="$(field "$corpse_line" sand)"
[[ "$corpse_veins" -ge 10000 ]] ||
  fail "the corpse shows only $corpse_veins black vein pixels (need >= 10000): the dead body is not there"
[[ "$corpse_sand" -ge 20000 ]] ||
  fail "the corpse's crop carries only $corpse_sand bright sand pixels (need >= 20000): the wide framing is wrong"

# --- the pull-back framing: low, looking up (the floor barely in frame) ------
# (measured: 101 960 floor pixels of the 742 400-pixel band = 13.7 %, and 27 286
# body pixels in the top band).
camera_floor="$(field "$camera_line" floor)"
camera_top="$(field "$camera_line" top)"
[[ "$camera_floor" -le 185600 ]] ||
  fail "the pull-back still shows $camera_floor floor pixels (need <= 185600 of 742400): the camera is not looking up at the body"
[[ "$camera_top" -ge 20000 ]] ||
  fail "the pull-back still has only $camera_top body pixels at the top edge (need >= 20000)"

# --- the death motion: the follow framing (the floor fills the bottom, the
# body is whole and the sky is above it) ------------------------------------
# (measured: 698 840 floor pixels = 94.1 %, 0 in the top band, 500 826 body).
motion_floor="$(field "$motion_line" floor)"
motion_top="$(field "$motion_line" top)"
motion_body="$(field "$motion_line" body)"
[[ "$motion_floor" -ge 593920 ]] ||
  fail "the motion still shows only $motion_floor floor pixels (need >= 593920 of 742400): the follow framing is wrong"
[[ "$motion_top" -le 5000 ]] ||
  fail "the motion still has $motion_top body pixels at the top edge (need <= 5000): the body is not fully framed"
[[ "$motion_body" -ge 300000 ]] ||
  fail "the motion still shows only $motion_body body pixels (need >= 300000)"

# --- the HUD on every still ---------------------------------------------------
while IFS= read -r hud_line; do
  hud_still="$(field "$hud_line" still)"
  hud_valus="$(field "$hud_line" valus)"
  hud_stamina="$(field "$hud_line" stamina)"
  hud_player="$(field "$hud_line" player)"
  [[ "$hud_valus" = "0" ]] ||
    fail "the $hud_still still draws Valus's health bar ($hud_valus red pixels): the bar must be hidden after the defeat"
  [[ "$hud_stamina" -ge 500 ]] ||
    fail "the $hud_still still's stamina bar is absent ($hud_stamina green pixels)"
  [[ "$hud_player" -ge 500 ]] ||
    fail "the $hud_still still's player health bar is absent ($hud_player amber pixels)"
done <<<"$(printf '%s\n' "$pixels_out" | grep -E '^s6-hud ')"

# --- Step 8: the effect pass's frame cost -------------------------------------

# `frame_ms` is the **whole frame** at the tendril phase (the opaque meshes,
# the effect and the HUD together); the effect pass's own cost is the
# `marginal_ms`, measured against the same 240 frames with the pass
# suppressed. Both are gated: the frame inside the 60 fps budget and the
# marginal under it (the marginal is noise-dominated at +-0.2 ms/frame, so it
# is a ceiling, not a measurement).
perf_line="$(one_line '^s6-perf ' "s6-perf")"
perf_frames="$(field "$perf_line" frames)"
perf_effect="$(field "$perf_line" effect_ms)"
perf_base="$(field "$perf_line" base_ms)"
perf_marginal="$(field "$perf_line" marginal_ms)"
perf_frame="$(field "$perf_line" frame_ms)"
[[ "$perf_frames" = "240" ]] ||
  fail "s6-perf frames=$perf_frames (expected the 240-frame pair)"
awk -v ms="$perf_frame" 'BEGIN { exit !(ms <= 16.7) }' ||
  fail "the frame at the tendril phase costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
awk -v ms="$perf_marginal" 'BEGIN { exit !(ms <= 16.7) }' ||
  fail "the effect pass's marginal cost is ${perf_marginal} ms/frame (outside the 60 fps budget of 16.7 ms)"
awk -v a="$perf_effect" -v b="$perf_base" 'BEGIN { exit !(a > 0.0 && b > 0.0) }' ||
  fail "the effect pass's timing is degenerate (effect ${perf_effect} ms, base ${perf_base} ms)"
}

run_section "$1"
