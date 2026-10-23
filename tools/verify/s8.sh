#!/usr/bin/env bash
# s8.sh — the runner's adapter for scripts/s8-acceptance.sh (see common.sh).
#   probes: the arena compose's sha pin, the shrine and plain composes = the pinned manifests, the
#           five probe checks.
#   audio:  the S8 map decoded into a private library (34 entries = the map's events, every file
#           present) and the audition gate.
#   gpu:    the s8 ride timeline + stills, the e2e (skipped by --quick), ride_points.h regenerated
#           byte-identical, the placements JSON cross-check (heredoc extracted), every gate verbatim.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

probe="$repo_root/tools/re/stage/probe.py"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
shrine_manifest="$repo_root/out/re/stage/shrine.manifest"
plain_manifest="$repo_root/out/re/stage/plain.manifest"
placements="$repo_root/tools/re/s8/placements.json"
points_gen="$repo_root/tools/re/s8/ride_points.py"
points_h="$repo_root/app/src/ride_points.h"
audio_map="$repo_root/tools/re/audio/s8_map.json"
audition="$repo_root/tools/re/audio/s5_audition.py"
stills_dir="$out_dir/stills"
audition_dir="$out_dir/audition-gate"
selftest_txt="$out_dir/s8-selftest.txt"
e2e_txt="$out_dir/s8-e2e.txt"
arena_scratch="$out_dir/arena-compose.manifest"
shrine_scratch="$out_dir/shrine-compose.manifest"
plain_scratch="$out_dir/plain-compose.manifest"
points_scratch="$out_dir/ride_points.h"
arena_sha_pin="088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0"

section_probes() {
need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

for c in dotnet cmake ctest caffeinate shasum awk grep sed sort tee tr wc python3 od cmp diff; do
  need "$c"
done

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
[[ -f "$encounter_manifest" ]] || fail "encounter manifest not found: $encounter_manifest"
[[ -f "$shrine_manifest" ]] || fail "shrine manifest not found: $shrine_manifest (probe.py compose --stage shrine)"
[[ -f "$plain_manifest" ]] || fail "plain manifest not found: $plain_manifest (probe.py compose --stage plain)"
[[ -f "$placements" ]] || fail "S8 placements not found: $placements"
[[ -f "$audio_map" ]] || fail "S8 audio map not found: $audio_map"

stamp "probe.py compose (arena, scratch): must hash to the pinned S3 value"
checked compose-arena python3 "$probe" compose --library "$lib" --out "$arena_scratch"
arena_sha="$(shasum -a 256 "$arena_scratch" | cut -d' ' -f1)"
[[ "$arena_sha" = "$arena_sha_pin" ]] ||
  fail "the arena compose hashes to $arena_sha (expected $arena_sha_pin)"

for stage in shrine plain; do
  if [[ "$stage" = "shrine" ]]; then scratch="$shrine_scratch"; pinned="$shrine_manifest"
  else scratch="$plain_scratch"; pinned="$plain_manifest"; fi
  stamp "probe.py compose --stage $stage (scratch): must equal $pinned"
  checked "compose-$stage" python3 "$probe" compose --library "$lib" --stage "$stage" --out "$scratch"
  cmp -s "$scratch" "$pinned" || fail "the $stage compose differs from $pinned"
done
shrine_sha="$(shasum -a 256 "$shrine_manifest" | cut -d' ' -f1)"
plain_sha="$(shasum -a 256 "$plain_manifest" | cut -d' ' -f1)"

stamp "probe.py check-cwks / check-manifest / check-encounter / check-manifest --stage shrine|plain"
checked check-cwks python3 "$probe" check-cwks
checked check-manifest-arena python3 "$probe" check-manifest "$arena_scratch" --library "$lib"
checked check-encounter python3 "$probe" check-encounter "$encounter_manifest" --library "$lib"
checked check-manifest-shrine python3 "$probe" check-manifest "$shrine_manifest" --library "$lib" --stage shrine
checked check-manifest-plain python3 "$probe" check-manifest "$plain_manifest" --library "$lib" --stage plain
keep arena-sha "$arena_sha"
}

section_audio() {
need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

for c in dotnet cmake ctest caffeinate shasum awk grep sed sort tee tr wc python3 od cmp diff; do
  need "$c"
done

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
[[ -f "$encounter_manifest" ]] || fail "encounter manifest not found: $encounter_manifest"
[[ -f "$shrine_manifest" ]] || fail "shrine manifest not found: $shrine_manifest (probe.py compose --stage shrine)"
[[ -f "$plain_manifest" ]] || fail "plain manifest not found: $plain_manifest (probe.py compose --stage plain)"
[[ -f "$placements" ]] || fail "S8 placements not found: $placements"
[[ -f "$audio_map" ]] || fail "S8 audio map not found: $audio_map"
lib="$out_dir/lib"   # the private decode (the frozen script decodes into the shared library)
audio_manifest="$lib/audio/audio.manifest"
rm -rf "$lib" "$audition_dir"

stamp "bp-convert audio: $audio_map (the S7 chain left the S6 ten)"
if ! convert_out="$(caffeinate -dimsu dotnet exec "$cli" audio \
  --root "$game_root" --out "$lib" --map "$audio_map" --force 2>&1)"; then
  printf '%s\n' "$convert_out" >&2
  fail "bp-convert audio exited non-zero"
fi
printf '%s\n' "$convert_out"
grep -q ' 34 manifest entries' <<<"$convert_out" ||
  fail "bp-convert audio did not report 34 manifest entries"

[[ -f "$audio_manifest" ]] || fail "no audio manifest at $audio_manifest after the decode"
manifest_entries="$(grep -c '^name=' "$audio_manifest" || true)"
[[ "$manifest_entries" = "34" ]] ||
  fail "the audio manifest lists $manifest_entries entries (expected 34: the S6 ten + 24 S8 event-variants)"
manifest_names="$out_dir/manifest-names.txt"
grep '^name=' "$audio_manifest" | sed 's/^name=//; s/ .*$//' | sort > "$manifest_names"
map_names="$out_dir/map-names.txt"
sed -n '/"cues": \[/,/"deferred": \[/p' "$audio_map" |
  grep -o '"event": "[^"]*"' | sed 's/.*: "//; s/"$//' | sort > "$map_names"
if ! diff -u "$map_names" "$manifest_names" > "$out_dir/manifest-names.diff"; then
  cat "$out_dir/manifest-names.diff" >&2
  fail "the manifest's names are not the S8 map's cue events"
fi
while IFS= read -r entry; do
  file="${entry#*file=}"; file="${file%% *}"
  [[ -f "$lib/$file" ]] || fail "manifest entry without its file: $entry"
done < <(grep '^name=' "$audio_manifest")
audio_sha="$(shasum -a 256 "$audio_manifest" | cut -d' ' -f1)"

stamp "audition gate: s5_audition.py --check on $audio_map"
if ! audition_out="$(python3 "$audition" --root "$game_root" --map "$audio_map" \
  --out "$audition_dir" --check 2>&1)"; then
  printf '%s\n' "$audition_out" >&2
  fail "the S8 audition gate failed"
fi
audition_line="$(grep -m1 -E '^AUDITION PASSED: 34 cue\(s\)' <<<"$audition_out" || true)"
[[ -n "$audition_line" ]] || fail "the audition gate did not report 34 passing cues"
printf '%s\n' "$audition_line"
keep convert "$convert_out"
manifest_entries_txt="$(grep '^name=' "$audio_manifest")"
keep manifest-entries "$manifest_entries_txt"
keep audition "$audition_line"
}

section_gpu() {
need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

for c in dotnet cmake ctest caffeinate shasum awk grep sed sort tee tr wc python3 od cmp diff; do
  need "$c"
done

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
[[ -f "$encounter_manifest" ]] || fail "encounter manifest not found: $encounter_manifest"
[[ -f "$shrine_manifest" ]] || fail "shrine manifest not found: $shrine_manifest (probe.py compose --stage shrine)"
[[ -f "$plain_manifest" ]] || fail "plain manifest not found: $plain_manifest (probe.py compose --stage plain)"
[[ -f "$placements" ]] || fail "S8 placements not found: $placements"
[[ -f "$audio_map" ]] || fail "S8 audio map not found: $audio_map"

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir" "$audition_dir"
mkdir -p "$stills_dir"
rm -f "$selftest_txt" "$e2e_txt" "$arena_scratch" "$shrine_scratch" "$plain_scratch" "$points_scratch"

# --- Step 6: the deterministic ride -------------------------------------------

stamp "s8-selftest + stills: $plain_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --shrine-manifest "$shrine_manifest" \
  --plain-manifest "$plain_manifest" --model "$player" \
  --s8-selftest --s8-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s8-selftest --s8-stills exited non-zero"
fi
printf '%s\n' "$selftest_out"
printf '%s\n' "$selftest_out" > "$selftest_txt"

# field LINE KEY — the value of KEY=… in LINE. Assign it to a variable first
# (x="$(field …)"): set -e then stops the script when it is missing; used as an
# argument inside "$(…)" its failure would be lost.
field() {
  local line="$1" key="$2" rest
  rest="${line#*"$key"=}"
  [[ "$rest" != "$line" ]] || fail "field $key missing from: $line"
  printf '%s' "${rest%% *}"
}

line_count() {
  printf '%s\n' "$selftest_out" | grep -c -E "$1" || true
}

one_line() {
  local regex="$1" label="$2" count line
  count="$(line_count "$regex")"
  [[ "$count" = "1" ]] || fail "$label: expected exactly one line, found $count"
  line="$(grep -m1 -E "$regex" <<<"$selftest_out")" || fail "$label: missing"
  printf '%s' "$line"
}

# within VALUE EXPECTED FRACTION — |value - expected| <= fraction x |expected|.
# An empty argument is a failure, never a vacuous pass.
within() {
  [[ -n "$1" && -n "$2" && -n "$3" ]] || fail "within: an empty argument ('$1' '$2' '$3')"
  awk -v v="$1" -v e="$2" -v f="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; a = e < 0 ? -e : e; exit !(d <= f * a) }'
}

# load + whistle + mount + 3 gait + steer + stop + back + edge-ride + dismount
# + edge + rider + audio + perf + reset.
s8_total="$(line_count '^s8-')"
[[ "$s8_total" = "16" ]] || fail "expected 16 s8-* lines, found $s8_total"

load_line="$(one_line '^s8-load ' "s8-load")"
[[ "$load_line" =~ ^s8-load\ stage=plain\ instances=[0-9]+\ meshes=[0-9]+\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s8-load is malformed: $load_line"
load_ms="$(field "$load_line" ms)"
awk -v ms="$load_ms" 'BEGIN { exit !(ms <= 15000) }' || fail "the plain load took $load_ms ms (ceiling 15000)"

whistle_line="$(one_line '^s8-whistle ' "s8-whistle")"
whistle_arrived="$(field "$whistle_line" arrived)"
whistle_dist="$(field "$whistle_line" dist)"
whistle_neigh="$(field "$whistle_line" neigh)"
[[ "$whistle_neigh" = "1" ]] || fail "Agro did not neigh: $whistle_line"
awk -v d="$whistle_arrived" 'BEGIN { exit !(d <= 3.5) }' || fail "Agro arrived $whistle_arrived m away (ceiling 3.5)"

mount_line="$(one_line '^s8-mount ' "s8-mount")"
[[ "$mount_line" = "s8-mount side=L clip=player_a_uma_stand_ride_l" ]] ||
  fail "s8-mount is '$mount_line' (expected the left mount, stand_ride_l)"

gait_rows=""
for gait in Walk Trot Gallop; do
  gline="$(one_line "^s8-gait gait=$gait " "s8-gait $gait")"
  gspeed="$(field "$gline" speed)"
  gexpected="$(field "$gline" expected)"
  within "$gspeed" "$gexpected" 0.05 ||
    fail "the $gait loop speed is off by more than 5 %: $gline"
  gait_rows="${gait_rows}${gait_rows:+, }${gait} ${gspeed} m/s (expected ${gexpected})"
done
gait_order="$(printf '%s\n' "$selftest_out" | grep -E '^s8-gait ' | awk '{ print $2 }' | tr '\n' ' ')"
[[ "$gait_order" = "gait=Walk gait=Trot gait=Gallop " ]] || fail "the gait order is '$gait_order'"

steer_line="$(one_line '^s8-steer ' "s8-steer")"
steer_rate="$(field "$steer_line" yaw_rate)"
steer_expected="$(field "$steer_line" expected)"
[[ "$steer_expected" = "-45.00" ]] ||
  fail "s8-steer expected=$steer_expected (D10's gallop rate, right = -45.00)"
within "$steer_rate" -45.00 0.05 || fail "the gallop yaw rate is off by more than 5 %: $steer_line"

stop_line="$(one_line '^s8-stop ' "s8-stop")"
[[ "$stop_line" = "s8-stop from=Gallop clips=run_stop1,run_stop2,run_stop3" ]] ||
  fail "s8-stop is '$stop_line' (expected run_stop1,run_stop2,run_stop3 from Gallop)"

back_line="$(one_line '^s8-back ' "s8-back")"
back_speed="$(field "$back_line" speed)"

edge_ride_line="$(one_line '^s8-edge-ride ' "s8-edge-ride")"
edge_ride_blocked="$(field "$edge_ride_line" blocked)"
edge_ride_clips="$(field "$edge_ride_line" clips)"
edge_ride_stopped="$(field "$edge_ride_line" stopped)"
[[ "$edge_ride_blocked" = "1" ]] || fail "the ridden edge is not blocked: $edge_ride_line"
[[ "$edge_ride_clips" = "walk_start,walk,run_n_start,run_n,run_n_stop,stand1" ]] ||
  fail "the ridden edge did not stop through run_n_stop (R11): $edge_ride_line"

dismount_line="$(one_line '^s8-dismount ' "s8-dismount")"
[[ "$dismount_line" = "s8-dismount side=R" ]] || fail "s8-dismount is '$dismount_line' (expected side=R)"

edge_line="$(one_line '^s8-edge ' "s8-edge")"
edge_blocked="$(field "$edge_line" blocked)"
edge_stopped="$(field "$edge_line" stopped)"
[[ "$edge_blocked" = "1" ]] || fail "the on-foot edge is not blocked: $edge_line"

rider_line="$(one_line '^s8-rider ' "s8-rider")"
rider_offset="$(field "$rider_line" offset)"
awk -v o="$rider_offset" 'BEGIN { exit !(o >= 0 && o <= 0.10) }' ||
  fail "the rider departs $rider_offset m from the seated rest (ceiling 0.10, ruling R13)"

audio_line="$(one_line '^s8-audio ' "s8-audio")"
audio_events="$(field "$audio_line" events)"
audio_hooves="$(field "$audio_line" hooves)"
[[ "$audio_hooves" -gt 0 && "$audio_events" -ge "$audio_hooves" ]] ||
  fail "s8-audio counts are implausible: $audio_line"

perf_line="$(one_line '^s8-perf ' "s8-perf")"
perf_frames="$(field "$perf_line" frames)"
perf_drawn="$(field "$perf_line" drawn)"
[[ "$perf_frames" = "240" ]] || fail "s8-perf frames=$perf_frames (expected 240)"
perf_frame="$(field "$perf_line" frame_ms)"
awk -v ms="$perf_frame" 'BEGIN { exit !(ms > 0.0 && ms <= 16.7) }' ||
  fail "the plain frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
perf_culled="$(field "$perf_line" culled)"
[[ "$perf_culled" -gt 0 ]] || fail "s8-perf culled=$perf_culled (culling is not active)"

reset_line="$(one_line '^s8-reset ' "s8-reset")"
[[ "$reset_line" = "s8-reset stage=arena ride=OnFoot door=Idle valus=100 player=100" ]] ||
  fail "s8-reset is '$reset_line'"

one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' "s1-status" > /dev/null

if [[ -z "${VERIFY_QUICK:-}" ]]; then   # --quick skips the e2e run
# --- Step 7: the end-to-end ride (the interactive loop, scripted) -------------

stamp "s8-e2e: the interactive loop from the shrine door to the ride and back"
if ! e2e_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --shrine-manifest "$shrine_manifest" \
  --plain-manifest "$plain_manifest" --model "$player" \
  --stage shrine --s8-e2e 2>&1)"; then
  printf '%s\n' "$e2e_out" >&2
  fail "--s8-e2e exited non-zero"
fi
printf '%s\n' "$e2e_out"
printf '%s\n' "$e2e_out" > "$e2e_txt"

e2e_lines() { printf '%s\n' "$e2e_out" | grep -E "$1" || true; }

# 2 doors + ride + reset.
e2e_total="$(e2e_lines '^s8e2e-' | grep -c . || true)"
[[ "$e2e_total" = "4" ]] || fail "expected 4 s8e2e-* lines, found $e2e_total"
e2e_doors="$(e2e_lines '^s8e2e-door ' | sed 's/^s8e2e-door //' | tr '\n' ';')"
[[ "$e2e_doors" = "from=shrine to=plain;from=plain to=shrine;" ]] ||
  fail "the e2e doors are '$e2e_doors' (expected shrine->plain, then plain->shrine)"
e2e_loads="$(e2e_lines '^s7-load ' | awk '{ print $2 }' | sed 's/stage=//' | tr '\n' ' ')"
[[ "$e2e_loads" = "shrine plain shrine arena " ]] ||
  fail "the e2e load order is '$e2e_loads' (expected 'shrine plain shrine arena ')"
while IFS= read -r l; do
  [[ "$l" =~ ^s7-load\ stage=[a-z]+\ instances=[0-9]+\ meshes=[0-9]+\ ms=[0-9]+\.[0-9]$ ]] ||
    fail "a malformed e2e load line: $l"
  l_ms="$(field "$l" ms)"
  awk -v ms="$l_ms" 'BEGIN { exit !(ms <= 15000) }' || fail "an e2e load took too long: $l"
done < <(e2e_lines '^s7-load ')
e2e_ride="$(e2e_lines '^s8e2e-ride ')"
[[ "$e2e_ride" =~ ^s8e2e-ride\ gallop_m=[0-9]+\.[0-9]$ ]] || fail "s8e2e-ride is malformed: '$e2e_ride'"
e2e_gallop="$(field "$e2e_ride" gallop_m)"
awk -v m="$e2e_gallop" 'BEGIN { exit !(m >= 60.0) }' || fail "the e2e gallop covered $e2e_gallop m (floor 60)"
e2e_reset="$(e2e_lines '^s8e2e-reset ')"
[[ "$e2e_reset" = "s8e2e-reset stage=arena ride=OnFoot door=Idle valus=100 player=100" ]] ||
  fail "s8e2e-reset is '$e2e_reset'"
[[ "$(e2e_lines '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' | grep -c . || true)" = "1" ]] ||
  fail "the e2e run is not windowed with the cursor free"
fi

# --- Step 8: the JSON cross-check ---------------------------------------------

stamp "cross-check: ride_points.h and the printed probes against placements.json"
python3 -I "$points_gen" "$placements" "$points_scratch" > /dev/null
cmp -s "$points_scratch" "$points_h" ||
  fail "app/src/ride_points.h is not the generator's output from $placements"
xcheck_py="$(heredoc_py s8 1)"
SEL_OUT="$selftest_out" checked json-cross-check python3 -I "$xcheck_py" "$placements"

# --- Step 9: the stills -------------------------------------------------------

span_for() {
  local want="$1"
  awk -v want="$want" '
    /^s1-pixels / { line = $0 }
    /^s1-screenshot / { if ($2 == "path=" want) { print line; exit } }
  ' <<<"$selftest_out"
}

size_for() {
  local want="$1"
  awk -v want="$want" '
    /^s1-screenshot / {
      if ($2 == "path=" want) {
        w = $3; h = $4;
        sub(/^width=/, "", w); sub(/^height=/, "", h);
        print w, h; exit
      }
    }
  ' <<<"$selftest_out"
}

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
  [[ "$w" = "2560" && "$h" = "1440" ]] || fail "$name: $path is ${w}x${h} (need 2560x1440)"
  pixels_line="$(span_for "$path")"
  [[ -n "$pixels_line" ]] || fail "$name: no s1-pixels line for $path"
  min_csv="${pixels_line#*min=}"; min_csv="${min_csv%% *}"
  max_csv="${pixels_line#*max=}"; max_csv="${max_csv%% *}"
  IFS=, read -r min_r min_g min_b <<<"$min_csv"
  IFS=, read -r max_r max_g max_b <<<"$max_csv"
  largest=$((max_r - min_r))
  d=$((max_g - min_g)); if ((d > largest)); then largest=$d; fi
  d=$((max_b - min_b)); if ((d > largest)); then largest=$d; fi
  [[ "$largest" -ge 30 ]] || fail "$name: uniform frame (largest channel span $largest < 30): $pixels_line"
  printf '%s|%s|%s\n' "$path" "$bytes" "$largest"
}

stills=""
for name in whistle mount gallop plain; do
  path="$stills_dir/$name.png"
  [[ "$(line_count "^s1-screenshot path=$path width=2560 height=1440\$")" = "1" ]] ||
    fail "still $name: expected one s1-screenshot line for $path"
  stills="${stills}${stills:+ }$(check_png "$name" "$path")"
done
}

run_section "$1"
