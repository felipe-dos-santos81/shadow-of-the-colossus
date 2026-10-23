#!/usr/bin/env bash
#
# s9-acceptance.sh — the S9 "The road to Valus" acceptance pass, re-runnable.
#
# Steps (the milestone: the arena's entrance stairs and corridor are part of
# the plain — the road; the corridor's dead end is a door into Valus's arena
# and the arena has one back; Agro waits where she was left; the raised
# sword's light — the eight-line fan, the lock, the ray — points to Valus):
#
#   1. build the converter solution and the published CLI, and app/ (Release);
#   2. both suites — `dotnet test tests/BpConvert.Tests` and `ctest`;
#   3. the **frozen S8 acceptance first** (`scripts/s8-acceptance.sh`, never
#      edited; it runs S7 -> S6 -> S5 -> S4.2 -> S4.1 -> S4 -> S3 and decodes
#      S8's own map, 34 entries, into SOTC_LIB). Its outputs are not
#      re-asserted here beyond its PASS line;
#   4. the S9 audio: this script decodes tools/re/audio/s9_map.json itself
#      (`bp-convert audio`, never `make audio`): 36 entries, the names equal to
#      the map's cue events, every file present; plus the audition gate
#      (`s5_audition.py --check` into a scratch dir: 36 cues, non-silent);
#   5. the manifests: the arena compose (scratch) hashes to the pinned S3
#      value; the shrine, plain and road composes (scratch) are byte-identical
#      to out/re/stage/{shrine,plain,road}.manifest; check-manifest --stage
#      shrine|plain|road;
#   6. road_points.h regenerates byte-identical from tools/re/s9/placements.json;
#   7. the flag refusals (exit 2): --s9-e2e with --s9-selftest, with
#      --screenshot, with --no-return, without --stage shrine; --s9-selftest
#      without --manifest; --s9-stills without --s9-selftest;
#   8. the S9 bake in the library: Wander's clips 43…48 are the six raise
#      clips (49 in all) and the four sword PNGs exist (PNG magic, Task 0's
#      dimensions);
#   9. the deterministic proof — `--s9-selftest --s9-stills out/s9/stills`: 15
#      s9-* lines in order, each with its values/tolerances (the load <= 15 s,
#      the sun, the raise, the fan, the partial, the lock — locked=1, hit=1,
#      hit_m <= 150, ruling B6 —, the sparks, the shade, the mounted raise, the
#      entrance, both doors, the audio counters, the perf <= 16.7 ms, the exact
#      reset); plus the `s1-status` window/cursor line;
#  10. the end-to-end loop — `--stage shrine --s9-e2e`: the interactive loop
#      at a fixed 1/60 s with scripted input: the joins in the implementation's
#      order (the loads and the s9e2e-* lines; the ride before the lock), the
#      gallop minimum, the lock, the stairs, the death, the return;
#  11. the JSON cross-check: the printed door spawns, the sun and the entrance
#      against placements.json;
#  12. the four stills (fan, locked, entrance, arena_door): PNG magic,
#      > 5000 B, 2560x1440, the viewer's non-uniformity span >= 30;
#  13. a summary block with every measured number.
#
# Every viewer and acceptance run is wrapped in `caffeinate -dimsu`. Never run
# two acceptances concurrently. The S8 chain takes ~10 min.
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library).
#                   Until S9's merge promotes the bake, the S9 library view:
#                   `make libview BRANCH=s9-road` prints it
#                   (<repo>/out/libviews/s9-road); the base library without the
#                   S9 bake is refused.
#
# Identity is not scriptable: the lines prove the wiring and the curves, not
# that the light looks like the game's. docs/notes/s9-results.md records every
# measured number; the user's walk checklist is PENDING there.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
probe="$repo_root/tools/re/stage/probe.py"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
shrine_manifest="$repo_root/out/re/stage/shrine.manifest"
plain_manifest="$repo_root/out/re/stage/plain.manifest"
road_manifest="$repo_root/out/re/stage/road.manifest"
placements="$repo_root/tools/re/s9/placements.json"
points_gen="$repo_root/tools/re/s9/road_points.py"
points_h="$repo_root/app/src/road_points.h"
audio_map="$repo_root/tools/re/audio/s9_map.json"
audition="$repo_root/tools/re/audio/s5_audition.py"
audio_manifest="$lib/audio/audio.manifest"
player="models/shadow/characters/wander/_cmn/wander.glb"
clips_json="$lib/models/shadow/characters/wander/_cmn/wander.clips.json"
cli="$repo_root/out/bin/bp-convert.dll"
out_dir="$repo_root/out/s9"
stills_dir="$out_dir/stills"
audition_dir="$out_dir/audition-gate"
selftest_txt="$out_dir/s9-selftest.txt"
e2e_txt="$out_dir/s9-e2e.txt"
arena_scratch="$out_dir/arena-compose.manifest"
shrine_scratch="$out_dir/shrine-compose.manifest"
plain_scratch="$out_dir/plain-compose.manifest"
road_scratch="$out_dir/road-compose.manifest"
points_scratch="$out_dir/road_points.h"
run_log="$repo_root/out/s9-acceptance-run.log"
s8_log="$repo_root/out/s9-s8-regression.log"
arena_sha_pin="088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0"
# The player glb's 12 primitives + wander_sword.glb's 1: the road's s9-load
# meshes are the road compose's totals plus these (measured, like S8's 3541+12).
player_meshes=13
# The e2e gallops kE2eRoute's legs (487 m, Task 0) to the rein 20 m short of
# kAgroStairs; the floor leaves room for the kick ramp and the stop's slide.
e2e_gallop_floor=400.0
# The four sword PNGs in road_points::kSwordTextures order (lines, overlay,
# flare, hit — hit is the flare's atlas) and Task 0's decoded dimensions.
sword_dims="512x512 512x512 256x256 256x256"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s9-acceptance: $*" >&2; exit 1; }

mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

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
[[ -f "$clips_json" ]] || fail "Wander's clip list not found: $clips_json"
wander_clips="$(python3 -I -c 'import json, sys; print(len(json.load(open(sys.argv[1]))["clips"]))' "$clips_json")"
if [[ "$wander_clips" != "49" ]]; then
  if [[ "$(cd "$lib" && pwd -P)" = "$(cd "$repo_root/out/library" 2>/dev/null && pwd -P)" ]]; then
    fail "the base library $lib lacks the S9 bake (Wander has $wander_clips clips, not 49): export SOTC_LIB=$repo_root/out/libviews/s9-road (make libview BRANCH=s9-road)"
  fi
  fail "Wander has $wander_clips clips in $lib (expected 49: the S9 bake, make bake)"
fi
[[ -f "$encounter_manifest" ]] || fail "encounter manifest not found: $encounter_manifest"
[[ -f "$shrine_manifest" ]] || fail "shrine manifest not found: $shrine_manifest (make shrine-manifest)"
[[ -f "$plain_manifest" ]] || fail "plain manifest not found: $plain_manifest (make plain-manifest)"
[[ -f "$road_manifest" ]] || fail "road manifest not found: $road_manifest (make road-manifest)"
[[ -f "$placements" ]] || fail "S9 placements not found: $placements"
[[ -f "$audio_map" ]] || fail "S9 audio map not found: $audio_map"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"

# --- Step 1: builds -----------------------------------------------------------

stamp "build: BpConvert.sln"
dotnet build "$repo_root/BpConvert.sln" -v q

stamp "publish: src/BpConvert.Cli -> out/bin"
dotnet publish "$repo_root/src/BpConvert.Cli" -c Release -o "$repo_root/out/bin"
[[ -f "$cli" ]] || fail "publish did not produce $cli"

stamp "build: app -> app/build (Release)"
cmake -S "$repo_root/app" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j
[[ -x "$viewer" ]] || fail "build did not produce $viewer"

# --- Step 2: both suites ------------------------------------------------------

stamp "dotnet test: tests/BpConvert.Tests"
dotnet test "$repo_root/tests/BpConvert.Tests" -v q

stamp "ctest: $build_dir"
ctest --test-dir "$build_dir" --output-on-failure

# --- Step 3: the frozen S8 acceptance -----------------------------------------

stamp "s8 regression: scripts/s8-acceptance.sh (it runs S7, S6, S5, S4.2, S4.1, S4 and S3)"
if ! caffeinate -dimsu "$repo_root/scripts/s8-acceptance.sh" > "$s8_log" 2>&1; then
  tail -40 "$s8_log" >&2
  fail "the frozen S8 acceptance regression failed (log: $s8_log)"
fi
s8_pass_line="$(grep -m1 -E '^s8-acceptance: PASS — ' "$s8_log" || true)"
[[ -n "$s8_pass_line" ]] || fail "the S8 regression did not print its final PASS — line"
printf '%s\n' "$s8_pass_line" | cut -c1-200

# --- Step 4: the S9 audio (36 entries) ----------------------------------------

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir" "$audition_dir"
mkdir -p "$stills_dir"
rm -f "$selftest_txt" "$e2e_txt" "$arena_scratch" "$shrine_scratch" "$plain_scratch" \
  "$road_scratch" "$points_scratch"

stamp "bp-convert audio: $audio_map (the S8 acceptance left S8's 34)"
if ! convert_out="$(caffeinate -dimsu dotnet exec "$cli" audio \
  --root "$game_root" --out "$lib" --map "$audio_map" --force 2>&1)"; then
  printf '%s\n' "$convert_out" >&2
  fail "bp-convert audio exited non-zero"
fi
printf '%s\n' "$convert_out"
grep -q ' 36 manifest entries' <<<"$convert_out" ||
  fail "bp-convert audio did not report 36 manifest entries"

[[ -f "$audio_manifest" ]] || fail "no audio manifest at $audio_manifest after the decode"
manifest_entries="$(grep -c '^name=' "$audio_manifest" || true)"
[[ "$manifest_entries" = "36" ]] ||
  fail "the audio manifest lists $manifest_entries entries (expected 36: S8's 34 + sword.shimmer + sword.search)"
manifest_names="$out_dir/manifest-names.txt"
grep '^name=' "$audio_manifest" | sed 's/^name=//; s/ .*$//' | sort > "$manifest_names"
map_names="$out_dir/map-names.txt"
sed -n '/"cues": \[/,/"deferred": \[/p' "$audio_map" |
  grep -o '"event": "[^"]*"' | sed 's/.*: "//; s/"$//' | sort > "$map_names"
if ! diff -u "$map_names" "$manifest_names" > "$out_dir/manifest-names.diff"; then
  cat "$out_dir/manifest-names.diff" >&2
  fail "the manifest's names are not the S9 map's cue events"
fi
for cue in sword.shimmer sword.search; do
  grep -q "^name=$cue " "$audio_manifest" || fail "the audio manifest lacks $cue"
done
grep -q '^name=sword.shimmer .* loop=1 ' "$audio_manifest" || fail "sword.shimmer is not a loop=1 row"
while IFS= read -r entry; do
  file="${entry#*file=}"; file="${file%% *}"
  [[ -f "$lib/$file" ]] || fail "manifest entry without its file: $entry"
done < <(grep '^name=' "$audio_manifest")
audio_sha="$(shasum -a 256 "$audio_manifest" | cut -d' ' -f1)"

stamp "audition gate: s5_audition.py --check on $audio_map"
if ! audition_out="$(python3 "$audition" --root "$game_root" --map "$audio_map" \
  --out "$audition_dir" --check 2>&1)"; then
  printf '%s\n' "$audition_out" >&2
  fail "the S9 audition gate failed"
fi
audition_line="$(grep -m1 -E '^AUDITION PASSED: 36 cue\(s\)' <<<"$audition_out" || true)"
[[ -n "$audition_line" ]] || fail "the audition gate did not report 36 passing cues"
printf '%s\n' "$audition_line"

# --- Step 5: the manifests ----------------------------------------------------

stamp "probe.py compose (arena, scratch): must hash to the pinned S3 value"
python3 "$probe" compose --library "$lib" --out "$arena_scratch"
arena_sha="$(shasum -a 256 "$arena_scratch" | cut -d' ' -f1)"
[[ "$arena_sha" = "$arena_sha_pin" ]] ||
  fail "the arena compose hashes to $arena_sha (expected $arena_sha_pin)"

road_compose_out=""
for stage in shrine plain road; do
  case "$stage" in
    shrine) scratch="$shrine_scratch"; pinned="$shrine_manifest" ;;
    plain) scratch="$plain_scratch"; pinned="$plain_manifest" ;;
    road) scratch="$road_scratch"; pinned="$road_manifest" ;;
  esac
  stamp "probe.py compose --stage $stage (scratch): must equal $pinned"
  compose_out="$(python3 "$probe" compose --library "$lib" --stage "$stage" --out "$scratch")"
  printf '%s\n' "$compose_out"
  [[ "$stage" = "road" ]] && road_compose_out="$compose_out"
  cmp -s "$scratch" "$pinned" || fail "the $stage compose differs from $pinned"
done
shrine_sha="$(shasum -a 256 "$shrine_manifest" | cut -d' ' -f1)"
plain_sha="$(shasum -a 256 "$plain_manifest" | cut -d' ' -f1)"
road_sha="$(shasum -a 256 "$road_manifest" | cut -d' ' -f1)"
road_meshes="$(sed -n 's/^compose: totals \([0-9][0-9]*\) meshes,.*$/\1/p' <<<"$road_compose_out")"
[[ -n "$road_meshes" ]] || fail "the road compose did not print its mesh totals"
road_instances="$(grep -c '^instance ' "$road_manifest" || true)"
road_backdrops="$(grep -c '^backdrop ' "$road_manifest" || true)"

stamp "probe.py check-manifest --stage shrine|plain|road"
python3 "$probe" check-manifest "$shrine_manifest" --library "$lib" --stage shrine
python3 "$probe" check-manifest "$plain_manifest" --library "$lib" --stage plain
road_check_out="$(python3 "$probe" check-manifest "$road_manifest" --library "$lib" --stage road)"
printf '%s\n' "$road_check_out"
grep -q '^gate: MET$' <<<"$road_check_out" || fail "check-manifest --stage road did not print gate: MET"
road_check_summary="$(grep -m1 '^summary: ' <<<"$road_check_out" || true)"

# --- Step 6: road_points.h ----------------------------------------------------

stamp "road_points.py: app/src/road_points.h regenerated from placements.json"
python3 -I "$points_gen" "$placements" "$points_scratch" > /dev/null
cmp -s "$points_scratch" "$points_h" ||
  fail "app/src/road_points.h is not the generator's output from $placements"

# --- Step 7: the flag refusals ------------------------------------------------

# refused LABEL ARGS… — the viewer must exit 2 (a usage refusal) before any load.
refused() {
  local label="$1" rc=0 err
  shift
  err="$(caffeinate -dimsu "$viewer" "$@" 2>&1 >/dev/null)" || rc=$?
  [[ "$rc" = "2" ]] || fail "$label: exit $rc (expected the usage refusal, exit 2): $err"
  grep -q -- '--s9-' <<<"$err" || fail "$label: exit 2 without an --s9-* message: $err"
  printf 's9-acceptance: refused (exit 2) %s\n' "$label"
}

stamp "flag refusals"
base_args=(--library "$lib" --manifest "$encounter_manifest" --model "$player")
refused "--s9-e2e with --s9-selftest" "${base_args[@]}" --stage shrine --s9-e2e --s9-selftest
refused "--s9-e2e with --screenshot" "${base_args[@]}" --stage shrine --s9-e2e \
  --screenshot "$out_dir/refused.png"
refused "--s9-e2e with --no-return" "${base_args[@]}" --stage shrine --s9-e2e --no-return
refused "--s9-e2e without --stage shrine" "${base_args[@]}" --s9-e2e
refused "--s9-selftest without --manifest" --library "$lib" --model "$player" --s9-selftest
refused "--s9-stills without --s9-selftest" "${base_args[@]}" --s9-stills "$stills_dir"
[[ ! -e "$out_dir/refused.png" ]] || fail "a refused run wrote $out_dir/refused.png"

# --- Step 8: the S9 bake in the library ----------------------------------------

stamp "the S9 bake: Wander's raise clips 43…48 and the four sword PNGs"
raise_clips="$(python3 -I -c '
import json, sys
c = json.load(open(sys.argv[1]))["clips"]
print(",".join((x if isinstance(x, str) else x["name"]) for x in c[43:49]))' "$clips_json")"
[[ "$raise_clips" = "player_a_hoist_sword_start,player_a_hoist_sword_loop,player_a_hoist_sword_end,player_a_uma_hoist_sword_start,player_a_uma_hoist_sword_loop,player_a_uma_hoist_sword_end" ]] ||
  fail "Wander's clips 43…48 are '$raise_clips' (expected the six raise clips in D9 order)"

sword_pngs="$(python3 -I -c '
import json, sys
t = json.load(open(sys.argv[1]))["textures"]
print(" ".join(t[k] for k in ("lines", "overlay", "flare", "hit")))' "$placements")"
sword_rows=""
set -- $sword_dims
for png in $sword_pngs; do
  path="$lib/$png"
  [[ -f "$path" ]] || fail "sword texture missing: $path"
  magic="$(od -An -tx1 -N8 "$path" | tr -d ' \n')"
  [[ "$magic" = "89504e470d0a1a0a" ]] || fail "$path lacks the PNG magic (got $magic)"
  # IHDR: width and height, big-endian, at bytes 16..23.
  w="$(od -An -tu1 -j16 -N4 "$path" | tr -s ' \n' ' ' | awk '{ print $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 }')"
  h="$(od -An -tu1 -j20 -N4 "$path" | tr -s ' \n' ' ' | awk '{ print $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 }')"
  [[ "${w}x${h}" = "$1" ]] || fail "$path is ${w}x${h} (expected $1, Task 0's decode)"
  sword_rows="${sword_rows}${sword_rows:+, }${png##*/} ${w}x${h}"
  shift
done

# --- Step 9: the deterministic proof (--s9-selftest) ----------------------------

stamp "s9-selftest + stills: $road_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --shrine-manifest "$shrine_manifest" \
  --plain-manifest "$plain_manifest" --road-manifest "$road_manifest" --model "$player" \
  --s9-selftest --s9-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s9-selftest --s9-stills exited non-zero"
fi
printf '%s\n' "$selftest_out"
printf '%s\n' "$selftest_out" > "$selftest_txt"

# field LINE KEY — the value of KEY=… in LINE. Assign it to a variable first
# (x="$(field …)"): set -e then stops the script when it is missing; used as an
# argument inside "$(…)" its failure would be lost.
field() {
  local line="$1" key="$2" rest
  rest="${line#*" $key"=}"
  [[ "$rest" != "$line" ]] || fail "field $key missing from: $line"
  printf '%s' "${rest%% *}"
}

line_count() {
  grep -c -E "$1" <<<"$selftest_out" || true
}

one_line() {
  local regex="$1" label="$2" count line
  count="$(line_count "$regex")"
  [[ "$count" = "1" ]] || fail "$label: expected exactly one line, found $count"
  line="$(grep -m1 -E "$regex" <<<"$selftest_out")" || fail "$label: missing"
  printf '%s' "$line"
}

# near VALUE EXPECTED TOL — |value - expected| <= tol. An empty argument is a
# failure, never a vacuous pass.
near() {
  [[ -n "$1" && -n "$2" && -n "$3" ]] || fail "near: an empty argument ('$1' '$2' '$3')"
  awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t) }'
}

# cmp_ok VALUE OP LIMIT — an awk comparison (OP one of < <= > >=); empty fails.
cmp_ok() {
  [[ -n "$1" && -n "$3" ]] || fail "cmp_ok: an empty argument ('$1' '$2' '$3')"
  awk -v v="$1" -v l="$3" -v op="$2" 'BEGIN {
    if (op == "<") exit !(v < l); if (op == "<=") exit !(v <= l);
    if (op == ">") exit !(v > l); if (op == ">=") exit !(v >= l); exit 1 }'
}

# xyz CSV — "x y z" from "x,y,z".
xyz() { printf '%s' "$1" | tr ',' ' '; }

# The order is the timeline's: load, sun, raise, fan, partial, lock, spark,
# shade, mounted, entrance, the two doors, audio, perf, reset.
s9_order="$(grep -E '^s9-' <<<"$selftest_out" | awk '{ print $1 }' | tr '\n' ' ')"
[[ "$s9_order" = "s9-load s9-sun s9-raise s9-fan s9-partial s9-lock s9-spark s9-shade s9-mounted s9-entrance s9-door s9-door s9-audio s9-perf s9-reset " ]] ||
  fail "the s9-* lines are '$s9_order' (expected the 15 in the timeline's order)"

load_line="$(one_line '^s9-load ' "s9-load")"
[[ "$load_line" =~ ^s9-load\ stage=plain\ manifest=road\ instances=[0-9]+\ backdrops=[0-9]+\ meshes=[0-9]+\ raygrid=[0-9]+x[0-9]+\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s9-load is malformed: $load_line"
load_instances="$(field "$load_line" instances)"
load_backdrops="$(field "$load_line" backdrops)"
load_meshes="$(field "$load_line" meshes)"
load_raygrid="$(field "$load_line" raygrid)"
load_ms="$(field "$load_line" ms)"
[[ "$load_instances" = "$road_instances" ]] ||
  fail "s9-load instances=$load_instances (road.manifest has $road_instances instance lines)"
[[ "$load_backdrops" = "$road_backdrops" ]] ||
  fail "s9-load backdrops=$load_backdrops (road.manifest has $road_backdrops backdrop lines)"
[[ "$load_meshes" = "$((road_meshes + player_meshes))" ]] ||
  fail "s9-load meshes=$load_meshes (expected the road's $road_meshes + $player_meshes: the player and the sword)"
[[ "${load_raygrid%x*}" -gt 0 && "${load_raygrid#*x}" -gt 0 ]] || fail "s9-load raygrid=$load_raygrid is empty"
cmp_ok "$load_ms" "<=" 15000 || fail "the road load took $load_ms ms (ceiling 15000)"

sun_line="$(one_line '^s9-sun ' "s9-sun")"
[[ "$sun_line" = "s9-sun dir=0.371391,0.742781,0.557086" ]] || fail "s9-sun is '$sun_line'"

raise_line="$(one_line '^s9-raise ' "s9-raise")"
[[ "$raise_line" = "s9-raise clip=player_a_hoist_sword_start then=player_a_hoist_sword_loop raise=1.000" ]] ||
  fail "s9-raise is '$raise_line'"

fan_line="$(one_line '^s9-fan ' "s9-fan")"
fan_dot="$(field "$fan_line" dot)"
fan_k="$(field "$fan_line" k)"
fan_lean="$(field "$fan_line" lean_deg)"
fan_width="$(field "$fan_line" width)"
cmp_ok "$fan_dot" "<" 0 || fail "s9-fan is not facing away from Valus: $fan_line"
cmp_ok "$fan_k" ">=" 0.95 || fail "s9-fan k=$fan_k (floor 0.95: the fan open)"
near "$fan_lean" "$(awk -v k="$fan_k" 'BEGIN { printf "%.4f", 75 * k }')" 0.40 ||
  fail "s9-fan lean_deg=$fan_lean is not 75 x k (k=$fan_k)"
[[ "$fan_width" = "0.000" ]] || fail "s9-fan width=$fan_width (expected 0.000)"

partial_line="$(one_line '^s9-partial ' "s9-partial")"
partial_dot="$(field "$partial_line" dot)"
partial_k="$(field "$partial_line" k)"
partial_lean="$(field "$partial_line" lean_deg)"
partial_width="$(field "$partial_line" width)"
near "$partial_dot" 0.80 0.01 || fail "s9-partial dot=$partial_dot (expected 0.80 +- 0.01)"
near "$partial_k" 0.100 0.005 || fail "s9-partial k=$partial_k (expected 0.100 +- 0.005)"
near "$partial_lean" 7.50 0.40 || fail "s9-partial lean_deg=$partial_lean (expected 7.50 +- 0.40)"
near "$partial_width" 0.675 0.01 || fail "s9-partial width=$partial_width (expected 0.675 +- 0.01)"

lock_line="$(one_line '^s9-lock ' "s9-lock")"
lock_k="$(field "$lock_line" k)"
lock_locked="$(field "$lock_line" locked)"
lock_ray="$(field "$lock_line" ray_m)"
lock_hit="$(field "$lock_line" hit)"
lock_hit_m="$(field "$lock_line" hit_m)"
cmp_ok "$lock_k" "<" 0.01 || fail "s9-lock k=$lock_k (the lock is k < 0.01)"
[[ "$lock_locked" = "1" ]] || fail "s9-lock locked=$lock_locked"
[[ "$lock_ray" = "150.0" ]] || fail "s9-lock ray_m=$lock_ray (expected 150.0 at raise 1)"
[[ "$lock_hit" = "1" ]] || fail "s9-lock hit=$lock_hit (B6: the ray meets the entrance's east bank)"
cmp_ok "$lock_hit_m" ">" 0 || fail "s9-lock hit_m=$lock_hit_m"
cmp_ok "$lock_hit_m" "<=" 150 || fail "s9-lock hit_m=$lock_hit_m (B6: within the 150 m ray)"

spark_line="$(one_line '^s9-spark ' "s9-spark")"
[[ "$spark_line" = "s9-spark count=4 seconds=2.0" ]] || fail "s9-spark is '$spark_line'"

shade_line="$(one_line '^s9-shade ' "s9-shade")"
shade_from="$(field "$shade_line" light_from)"
shade_light="$(field "$shade_line" light)"
shade_seconds="$(field "$shade_line" seconds)"
cmp_ok "$shade_from" ">" 0.5 || fail "s9-shade light_from=$shade_from (the light was not on in the sun)"
[[ "$shade_light" = "0.000" ]] || fail "s9-shade light=$shade_light (expected 0.000)"
cmp_ok "$shade_seconds" "<=" 0.120 || fail "s9-shade took $shade_seconds s (ceiling 0.120: 9.0/s)"

mounted_line="$(one_line '^s9-mounted ' "s9-mounted")"
[[ "$mounted_line" = "s9-mounted clip=player_a_uma_hoist_sword_loop gait=Stand kick_ignored=1" ]] ||
  fail "s9-mounted is '$mounted_line'"

# within_box CSV — x 300–308, y 2.7–5.6, z >= 280: the corridor floor.
on_corridor() {
  local x y z
  read -r x y z <<<"$(xyz "$1")"
  cmp_ok "$x" ">=" 300 && cmp_ok "$x" "<=" 308 && cmp_ok "$y" ">=" 2.7 && cmp_ok "$y" "<=" 5.6 &&
    cmp_ok "$z" ">=" 280
}

entrance_line="$(one_line '^s9-entrance ' "s9-entrance")"
entrance_top="$(field "$entrance_line" top)"
on_corridor "$entrance_top" || fail "s9-entrance top=$entrance_top is not on the corridor floor (x 300–308, y 2.7–5.6, z >= 280)"

door_arena_line="$(one_line '^s9-door from=plain to=arena ' "s9-door to=arena")"
door_arena_spawn="$(field "$door_arena_line" spawn)"
door_arena_yaw="$(field "$door_arena_line" yaw)"
door_arena_ret="$(field "$door_arena_line" ret)"
read -r dx dy dz <<<"$(xyz "$door_arena_spawn")"
near "$dx" 353 0.05 && near "$dy" 27.951 0.05 && near "$dz" 345 0.05 ||
  fail "s9-door to=arena spawn=$door_arena_spawn (expected (353, 27.951, 345) +- 0.05)"
[[ "$door_arena_yaw" = "49.24" ]] || fail "s9-door to=arena yaw=$door_arena_yaw (expected 49.24)"
[[ "$door_arena_ret" = "Idle" ]] || fail "s9-door to=arena ret=$door_arena_ret (D3: a fresh return machine)"

door_plain_line="$(one_line '^s9-door from=arena to=plain ' "s9-door to=plain")"
door_plain_spawn="$(field "$door_plain_line" spawn)"
door_plain_agro="$(field "$door_plain_line" agro_moved)"
read -r dx dy dz <<<"$(xyz "$door_plain_spawn")"
near "$dx" 304 0.05 && near "$dy" 2.788 0.05 && near "$dz" 282 0.05 ||
  fail "s9-door to=plain spawn=$door_plain_spawn (expected (304, 2.788, 282) +- 0.05)"
cmp_ok "$door_plain_agro" "<=" 0.010 || fail "s9-door to=plain agro_moved=$door_plain_agro (ceiling 0.010 m)"

audio_line="$(one_line '^s9-audio ' "s9-audio")"
[[ "$audio_line" = "s9-audio shimmer_started=1 shimmer_stopped=1 search=1" ]] ||
  fail "s9-audio is '$audio_line' (one shimmer started and stopped; one search cue for the one lock)"

perf_line="$(one_line '^s9-perf ' "s9-perf")"
perf_frames="$(field "$perf_line" frames)"
perf_frame="$(field "$perf_line" frame_ms)"
perf_drawn="$(field "$perf_line" drawn)"
perf_culled="$(field "$perf_line" culled)"
[[ "$perf_frames" = "240" ]] || fail "s9-perf frames=$perf_frames (expected 240)"
cmp_ok "$perf_frame" ">" 0 && cmp_ok "$perf_frame" "<=" 16.7 ||
  fail "the road frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
[[ "$perf_drawn" -gt 0 ]] || fail "s9-perf drawn=$perf_drawn"
[[ "$perf_culled" -gt 0 ]] || fail "s9-perf culled=$perf_culled (culling is not active)"

reset_line="$(one_line '^s9-reset ' "s9-reset")"
[[ "$reset_line" = "s9-reset stage=arena ride=OnFoot door=Idle sword=Lowered horse=none valus=100 player=100" ]] ||
  fail "s9-reset is '$reset_line'"

one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' "s1-status" > /dev/null

# --- Step 10: the end-to-end loop (the interactive loop, scripted) -------------

stamp "s9-e2e: the interactive loop from the shrine door to Valus's death and back"
if ! e2e_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --shrine-manifest "$shrine_manifest" \
  --plain-manifest "$plain_manifest" --road-manifest "$road_manifest" --model "$player" \
  --stage shrine --s9-e2e 2>&1)"; then
  printf '%s\n' "$e2e_out" >&2
  fail "--s9-e2e exited non-zero"
fi
printf '%s\n' "$e2e_out"
printf '%s\n' "$e2e_out" > "$e2e_txt"

e2e_lines() { grep -E "$1" <<<"$e2e_out" || true; }
e2e_one() {
  local regex="$1" label="$2" count
  count="$(grep -c -E "$regex" <<<"$e2e_out" || true)"
  [[ "$count" = "1" ]] || fail "$label: expected exactly one line, found $count"
  grep -m1 -E "$regex" <<<"$e2e_out"
}

# The joins in the implementation's order (Task 9: the ride before the lock):
# each stage load and each s9e2e-* line, as one sequence.
e2e_joins="$(e2e_lines '^(s7-load |s9e2e-)' | awk '
  /^s7-load / { s = $2; sub(/^stage=/, "", s); printf "load:%s ", s; next }
  { j = $1; sub(/^s9e2e-/, "", j); printf "%s ", j }')"
[[ "$e2e_joins" = "load:shrine load:plain door mount ride lock stairs load:arena door death load:shrine return done " ]] ||
  fail "the e2e joins are '$e2e_joins' (expected shrine, the road, door, mount, ride, lock, stairs, the arena, door, death, the shrine, return, done)"
while IFS= read -r l; do
  [[ "$l" =~ ^s7-load\ stage=[a-z]+\ instances=[0-9]+\ meshes=[0-9]+\ ms=[0-9]+\.[0-9]$ ]] ||
    fail "a malformed e2e load line: $l"
  l_ms="$(field "$l" ms)"
  cmp_ok "$l_ms" "<=" 15000 || fail "an e2e load took too long: $l"
done < <(e2e_lines '^s7-load ')
e2e_road_load="$(e2e_one '^s7-load stage=plain ' "the e2e road load")"
e2e_road_instances="$(field "$e2e_road_load" instances)"
[[ "$e2e_road_instances" = "$road_instances" ]] ||
  fail "the e2e plain load has $e2e_road_instances instances (the road manifest has $road_instances: the e2e did not load the road)"

e2e_door1="$(e2e_one '^s9e2e-door from=shrine ' "s9e2e-door shrine")"
[[ "$e2e_door1" = "s9e2e-door from=shrine to=plain" ]] || fail "s9e2e-door is '$e2e_door1'"
e2e_door2="$(e2e_one '^s9e2e-door from=plain ' "s9e2e-door plain")"
[[ "$e2e_door2" = "s9e2e-door from=plain to=arena" ]] || fail "s9e2e-door is '$e2e_door2'"
e2e_mount="$(e2e_one '^s9e2e-mount ' "s9e2e-mount")"
[[ "$e2e_mount" =~ ^s9e2e-mount\ side=[LR]$ ]] || fail "s9e2e-mount is '$e2e_mount'"
e2e_side="$(field "$e2e_mount" side)"

e2e_ride="$(e2e_one '^s9e2e-ride ' "s9e2e-ride")"
[[ "$e2e_ride" =~ ^s9e2e-ride\ gallop_m=[0-9]+\.[0-9]$ ]] || fail "s9e2e-ride is malformed: '$e2e_ride'"
e2e_gallop="$(field "$e2e_ride" gallop_m)"
cmp_ok "$e2e_gallop" ">=" "$e2e_gallop_floor" ||
  fail "the e2e gallop covered $e2e_gallop m (floor $e2e_gallop_floor: kE2eRoute is 487 m)"

e2e_lock="$(e2e_one '^s9e2e-lock ' "s9e2e-lock")"
[[ "$e2e_lock" =~ ^s9e2e-lock\ k=[0-9]+\.[0-9]{4}\ locked=1\ hit=[01]\ hit_m=[0-9]+\.[0-9]$ ]] ||
  fail "s9e2e-lock is malformed: '$e2e_lock'"
e2e_lock_k="$(field "$e2e_lock" k)"
e2e_lock_hit="$(field "$e2e_lock" hit)"
e2e_lock_hit_m="$(field "$e2e_lock" hit_m)"
cmp_ok "$e2e_lock_k" "<" 0.01 || fail "s9e2e-lock k=$e2e_lock_k (the lock is k < 0.01)"
[[ "$e2e_lock_hit" = "1" ]] || fail "s9e2e-lock hit=$e2e_lock_hit (B6: 1 expected at kAgroStairs)"
cmp_ok "$e2e_lock_hit_m" ">" 0 && cmp_ok "$e2e_lock_hit_m" "<=" 150 ||
  fail "s9e2e-lock hit_m=$e2e_lock_hit_m (B6: within the 150 m ray)"

e2e_stairs="$(e2e_one '^s9e2e-stairs ' "s9e2e-stairs")"
[[ "$(field "$e2e_stairs" mounted)" = "1" ]] || fail "s9e2e-stairs is not mounted: $e2e_stairs"
e2e_stairs_top="$(field "$e2e_stairs" top)"
on_corridor "$e2e_stairs_top" || fail "s9e2e-stairs top=$e2e_stairs_top is not on the corridor floor"

e2e_death="$(e2e_one '^s9e2e-death ' "s9e2e-death")"
[[ "$e2e_death" = "s9e2e-death phase=Done" ]] || fail "s9e2e-death is '$e2e_death'"
e2e_return="$(e2e_one '^s9e2e-return ' "s9e2e-return")"
[[ "$e2e_return" = "s9e2e-return to=shrine" ]] || fail "s9e2e-return is '$e2e_return'"
e2e_done="$(e2e_one '^s9e2e-done ' "s9e2e-done")"
[[ "$e2e_done" =~ ^s9e2e-done\ frames=[0-9]+$ ]] || fail "s9e2e-done is malformed: '$e2e_done'"
e2e_frames="$(field "$e2e_done" frames)"
[[ "$e2e_frames" -gt 0 ]] || fail "s9e2e-done frames=$e2e_frames"
[[ "$(e2e_lines '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' | grep -c . || true)" = "1" ]] ||
  fail "the e2e run is not windowed with the cursor free"

# --- Step 11: the JSON cross-check ----------------------------------------------

stamp "cross-check: the printed doors, the sun and the entrance against placements.json"
SEL_OUT="$selftest_out" E2E_OUT="$e2e_out" python3 -I - "$placements" <<'PY'
import json, math, os, sys

d = json.load(open(sys.argv[1]))
out = os.environ["SEL_OUT"]
e2e = os.environ["E2E_OUT"]
def line(text, prefix):
    return next(l for l in text.splitlines() if l.startswith(prefix))
def kv(l):
    return dict(t.split("=", 1) for t in l.split()[1:])
def vec(s):
    return [float(v) for v in s.split(",")]

# The doors' printed arrivals are placements.json's (recon-pinned), +-0.05 per axis.
for prefix, door, key in (("s9-door from=plain to=arena ", "door_corridor", "arrive_arena"),
                          ("s9-door from=arena to=plain ", "door_arena", "arrive_road")):
    want = d[door][key]["p"]
    got = vec(kv(line(out, prefix))["spawn"])
    assert all(abs(a - b) <= 0.05 for a, b in zip(got, want)), f"{prefix.strip()} {got} != {want}"
yaw = float(kv(line(out, "s9-door from=plain to=arena "))["yaw"])
assert abs(yaw - d["door_corridor"]["arrive_arena"]["yaw_deg"]) <= 0.005, f"yaw {yaw}"

# The sun is the placement's direction, normalized.
s = d["sun"]
sv = s["dir"]
n = math.sqrt(sum(c * c for c in sv))
got = vec(kv(line(out, "s9-sun "))["dir"])
assert all(abs(a - b / n) <= 5e-6 for a, b in zip(got, sv)), f"sun {got} vs {sv}"

# The entrance (selftest, on foot) and the stairs (e2e, mounted) lie in the corridor
# door's x range, short of its box (z < its min z: the door has not fired yet).
dc = d["door_corridor"]
for text, prefix in ((out, "s9-entrance "), (e2e, "s9e2e-stairs ")):
    x, y, z = vec(kv(line(text, prefix))["top"])
    assert dc["min"][0] <= x <= dc["max"][0] and z < dc["min"][2], f"{prefix.strip()} {x},{y},{z} vs {dc['min']}..{dc['max']}"
print("s9-acceptance: json cross-check OK (two door arrivals, the arena yaw, the sun, the entrance and the stairs)")
PY

# --- Step 12: the stills ----------------------------------------------------------

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
for name in fan locked entrance arena_door; do
  path="$stills_dir/$name.png"
  [[ "$(line_count "^s1-screenshot path=$path width=2560 height=1440\$")" = "1" ]] ||
    fail "still $name: expected one s1-screenshot line for $path"
  row="$(check_png "$name" "$path")"
  stills="${stills}${stills:+ }$row"
done

# --- Step 13: summary ---------------------------------------------------------------

stamp "summary"
printf 's9-acceptance: library      %s\n' "$lib"
printf 's9-acceptance: arena sha    %s\n' "$arena_sha"
printf 's9-acceptance: shrine sha   %s\n' "$shrine_sha"
printf 's9-acceptance: plain sha    %s\n' "$plain_sha"
printf 's9-acceptance: road sha     %s (%s instances, %s backdrops, %s meshes; %s)\n' \
  "$road_sha" "$road_instances" "$road_backdrops" "$road_meshes" "$road_check_summary"
printf 's9-acceptance: audio sha    %s (36 entries)\n' "$audio_sha"
printf 's9-acceptance: audition     %s\n' "$audition_line"
printf 's9-acceptance: raise clips  43…48 = %s\n' "$raise_clips"
printf 's9-acceptance: sword pngs   %s\n' "$sword_rows"
printf 's9-acceptance: %s\n' "$load_line" "$sun_line" "$raise_line" "$fan_line" "$partial_line" \
  "$lock_line" "$spark_line" "$shade_line" "$mounted_line" "$entrance_line" "$door_arena_line" \
  "$door_plain_line" "$audio_line" "$perf_line" "$reset_line"
printf 's9-acceptance: e2e          %s\n' "$(e2e_lines '^(s9e2e-|s7-load )' | tr '\n' ' ')"
printf 's9-acceptance: stills       %s\n' "$stills"
printf 's9-acceptance: s8 regress   %s\n' "$(printf '%s\n' "$s8_pass_line" | cut -c1-160)"
printf 's9-acceptance: PASS — build, both suites, the frozen S8 acceptance green (S7 → S6 → S5 → S4.2 → S4.1 → S4 → S3 inside it), the S9 decode (36 manifest entries = the map'"'"'s cue events; the audition gate 36 cues non-silent), the arena compose at sha %s…, the shrine, plain and road composes byte-identical (sha %s…, %s…, %s…), check-manifest --stage shrine|plain|road green; road_points.h regenerated byte-identical; six flag refusals exit 2; Wander'"'"'s raise clips at 43…48 and the four sword PNGs; the road load %s ms (%s instances, %s backdrops, %s meshes, ray grid %s); the sun 0.371391,0.742781,0.557086; the raise 1.000; the fan k %s (width %s); the partial dot %s k %s lean %s° width %s; the lock k %s, hit at %s m (B6); 4 sparks in 2.0 s; the shade to 0 in %s s; the mounted raise at Stand, the kick ignored; the entrance at %s; the corridor door to the arena spawn %s yaw %s (ret Idle); the arena door to the road at %s, Agro moved %s m; the shimmer started and stopped, one search cue; road frame %s ms (drawn %s, culled %s); the reset exact; the end-to-end loop (shrine → door → road → mount %s, gallop %s m, lock k %s hit %s m → stairs %s → door → arena → death → shrine, %s frames) green in the implementation'"'"'s join order; four stills 2560x1440; windowed, cursor free\n' \
  "${arena_sha:0:8}" "${shrine_sha:0:8}" "${plain_sha:0:8}" "${road_sha:0:8}" \
  "$load_ms" "$load_instances" "$load_backdrops" "$load_meshes" "$load_raygrid" \
  "$fan_k" "$fan_width" "$partial_dot" "$partial_k" "$partial_lean" "$partial_width" \
  "$lock_k" "$lock_hit_m" "$shade_seconds" "$entrance_top" "$door_arena_spawn" "$door_arena_yaw" \
  "$door_plain_spawn" "$door_plain_agro" "$perf_frame" "$perf_drawn" "$perf_culled" \
  "$e2e_side" "$e2e_gallop" "$e2e_lock_k" "$e2e_lock_hit_m" "$e2e_stairs_top" "$e2e_frames"
