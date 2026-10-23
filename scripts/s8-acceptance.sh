#!/usr/bin/env bash
#
# s8-acceptance.sh — the S8 "Agro" acceptance pass, re-runnable.
#
# Steps (the milestone: walk out of the shrine's front door onto the plain,
# whistle for Agro, mount, ride at walk/trot/gallop, steer, rein in, back up,
# dismount; walk back into the shrine; R returns to the arena):
#
#   1. build the converter solution and the published CLI, and app/ (Release);
#   2. both suites — `dotnet test tests/BpConvert.Tests` and `ctest`;
#   3. the **frozen S7 acceptance first** (`scripts/s7-acceptance.sh`, never
#      edited; it runs S6 -> S5 -> S4.2 -> S4.1 -> S4 -> S3). Its outputs are
#      not re-asserted here beyond its PASS line;
#   4. the S8 audio: the S7 chain leaves the S6 ten in the manifest, so this
#      script decodes tools/re/audio/s8_map.json itself (`bp-convert audio`,
#      not `make audio`): 34 entries, the names equal to the map's cue events,
#      every file present; plus the audition gate (`s5_audition.py --check`
#      into a scratch dir: 34 cues, non-silent);
#   5. the manifests: the arena compose (scratch) hashes to the pinned S3
#      value; the shrine and plain composes (scratch) are byte-identical to
#      out/re/stage/{shrine,plain}.manifest; check-cwks, check-manifest
#      (arena), check-encounter, check-manifest --stage shrine and --stage plain;
#   6. the deterministic ride — `--s8-selftest --s8-stills out/s8/stills`: 16
#      s8-* lines — the load (<= 15 s), the whistle (arrival <= 3.5 m, neigh),
#      the left mount, the three gaits (+-5 % of their expected speed), the
#      steer (+-5 % of the yaw rate), the stop clips, the back-up (+-5 %), the
#      ridden edge (R11: stopped through run_n_stop), the right dismount, the
#      on-foot edge (blocked), the rider (<= 0.10 m from the seated rest,
#      ruling R13), the audio counters, the perf (<= 16.7 ms, culling active),
#      the exact reset; plus the `s1-status` window/cursor line;
#   7. the end-to-end ride — `--s8-e2e --stage shrine`: the interactive loop
#      at a fixed 1/60 s with scripted keys: the loads shrine plain shrine
#      arena, the doors shrine->plain then plain->shrine, the gallop >= 60 m,
#      the exact reset, the window line;
#   8. the JSON cross-check: ride_points.h regenerates byte-identical from
#      tools/re/s8/placements.json; the printed expected speeds equal the
#      JSON's; the whistle distance is the probe's; both edge stops lie on
#      the edge probe's segment, short of its end;
#   9. the four stills (whistle, mount, gallop, plain): PNG magic, > 5000 B,
#      2560x1440, the viewer's non-uniformity span >= 30;
#  10. a summary block with every measured number.
#
# Every viewer and acceptance run is wrapped in `caffeinate -dimsu`. Never run
# two acceptances concurrently. The S7 chain takes ~12 min.
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library)
#
# Identity is not scriptable: s8-gait/s8-steer prove the wiring (their expected
# values are the machine's own constants), not that the ride feels like the
# game's. docs/notes/s8-results.md records every measured number; the user's
# walk checklist is PENDING there.

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
placements="$repo_root/tools/re/s8/placements.json"
points_gen="$repo_root/tools/re/s8/ride_points.py"
points_h="$repo_root/app/src/ride_points.h"
audio_map="$repo_root/tools/re/audio/s8_map.json"
audition="$repo_root/tools/re/audio/s5_audition.py"
audio_manifest="$lib/audio/audio.manifest"
player="models/shadow/characters/wander/_cmn/wander.glb"
cli="$repo_root/out/bin/bp-convert.dll"
out_dir="$repo_root/out/s8"
stills_dir="$out_dir/stills"
audition_dir="$out_dir/audition-gate"
selftest_txt="$out_dir/s8-selftest.txt"
e2e_txt="$out_dir/s8-e2e.txt"
arena_scratch="$out_dir/arena-compose.manifest"
shrine_scratch="$out_dir/shrine-compose.manifest"
plain_scratch="$out_dir/plain-compose.manifest"
points_scratch="$out_dir/ride_points.h"
run_log="$repo_root/out/s8-acceptance-run.log"
s7_log="$repo_root/out/s8-s7-regression.log"
arena_sha_pin="088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s8-acceptance: $*" >&2; exit 1; }

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
[[ -f "$encounter_manifest" ]] || fail "encounter manifest not found: $encounter_manifest"
[[ -f "$shrine_manifest" ]] || fail "shrine manifest not found: $shrine_manifest (probe.py compose --stage shrine)"
[[ -f "$plain_manifest" ]] || fail "plain manifest not found: $plain_manifest (probe.py compose --stage plain)"
[[ -f "$placements" ]] || fail "S8 placements not found: $placements"
[[ -f "$audio_map" ]] || fail "S8 audio map not found: $audio_map"

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

# --- Step 3: the frozen S7 acceptance -----------------------------------------

stamp "s7 regression: scripts/s7-acceptance.sh (it runs S6, S5, S4.2, S4.1, S4 and S3)"
if ! caffeinate -dimsu "$repo_root/scripts/s7-acceptance.sh" > "$s7_log" 2>&1; then
  tail -40 "$s7_log" >&2
  fail "the frozen S7 acceptance regression failed (log: $s7_log)"
fi
s7_pass_line="$(grep -m1 -E '^s7-acceptance: PASS — ' "$s7_log" || true)"
[[ -n "$s7_pass_line" ]] || fail "the S7 regression did not print its final PASS — line"
s7_perf_line="$(grep -m1 -E '^s7-acceptance: perf ' "$s7_log" || true)"
printf '%s\n' "$s7_pass_line" | cut -c1-200
printf '%s\n' "$s7_perf_line"

# --- Step 4: the S8 audio (34 entries) ----------------------------------------

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir" "$audition_dir"
mkdir -p "$stills_dir"
rm -f "$selftest_txt" "$e2e_txt" "$arena_scratch" "$shrine_scratch" "$plain_scratch" "$points_scratch"

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
audition_line="$(printf '%s\n' "$audition_out" | grep -m1 -E '^AUDITION PASSED: 34 cue\(s\)' || true)"
[[ -n "$audition_line" ]] || fail "the audition gate did not report 34 passing cues"
printf '%s\n' "$audition_line"

# --- Step 5: the manifests ----------------------------------------------------

stamp "probe.py compose (arena, scratch): must hash to the pinned S3 value"
python3 "$probe" compose --library "$lib" --out "$arena_scratch"
arena_sha="$(shasum -a 256 "$arena_scratch" | cut -d' ' -f1)"
[[ "$arena_sha" = "$arena_sha_pin" ]] ||
  fail "the arena compose hashes to $arena_sha (expected $arena_sha_pin)"

for stage in shrine plain; do
  if [[ "$stage" = "shrine" ]]; then scratch="$shrine_scratch"; pinned="$shrine_manifest"
  else scratch="$plain_scratch"; pinned="$plain_manifest"; fi
  stamp "probe.py compose --stage $stage (scratch): must equal $pinned"
  python3 "$probe" compose --library "$lib" --stage "$stage" --out "$scratch"
  cmp -s "$scratch" "$pinned" || fail "the $stage compose differs from $pinned"
done
shrine_sha="$(shasum -a 256 "$shrine_manifest" | cut -d' ' -f1)"
plain_sha="$(shasum -a 256 "$plain_manifest" | cut -d' ' -f1)"

stamp "probe.py check-cwks / check-manifest / check-encounter / check-manifest --stage shrine|plain"
python3 "$probe" check-cwks
python3 "$probe" check-manifest "$arena_scratch" --library "$lib"
python3 "$probe" check-encounter "$encounter_manifest" --library "$lib"
python3 "$probe" check-manifest "$shrine_manifest" --library "$lib" --stage shrine
python3 "$probe" check-manifest "$plain_manifest" --library "$lib" --stage plain

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
  line="$(printf '%s\n' "$selftest_out" | grep -m1 -E "$regex")" || fail "$label: missing"
  printf '%s' "$line"
}

# within VALUE EXPECTED FRACTION — |value - expected| <= fraction x |expected|.
within() {
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
[[ "$(field "$whistle_line" neigh)" = "1" ]] || fail "Agro did not neigh: $whistle_line"
awk -v d="$whistle_arrived" 'BEGIN { exit !(d <= 3.5) }' || fail "Agro arrived $whistle_arrived m away (ceiling 3.5)"

mount_line="$(one_line '^s8-mount ' "s8-mount")"
[[ "$mount_line" = "s8-mount side=L clip=player_a_uma_stand_ride_l" ]] ||
  fail "s8-mount is '$mount_line' (expected the left mount, stand_ride_l)"

gait_rows=""
for gait in Walk Trot Gallop; do
  gline="$(one_line "^s8-gait gait=$gait " "s8-gait $gait")"
  within "$(field "$gline" speed)" "$(field "$gline" expected)" 0.05 ||
    fail "the $gait loop speed is off by more than 5 %: $gline"
  gait_rows="${gait_rows}${gait_rows:+, }${gait} $(field "$gline" speed) m/s (expected $(field "$gline" expected))"
done
gait_order="$(printf '%s\n' "$selftest_out" | grep -E '^s8-gait ' | awk '{ print $2 }' | tr '\n' ' ')"
[[ "$gait_order" = "gait=Walk gait=Trot gait=Gallop " ]] || fail "the gait order is '$gait_order'"

steer_line="$(one_line '^s8-steer ' "s8-steer")"
steer_rate="$(field "$steer_line" yaw_rate)"
[[ "$(field "$steer_line" expected)" = "-45.00" ]] ||
  fail "s8-steer expected=$(field "$steer_line" expected) (D10's gallop rate, right = -45.00)"
within "$steer_rate" -45.00 0.05 || fail "the gallop yaw rate is off by more than 5 %: $steer_line"

stop_line="$(one_line '^s8-stop ' "s8-stop")"
[[ "$stop_line" = "s8-stop from=Gallop clips=run_stop1,run_stop2,run_stop3" ]] ||
  fail "s8-stop is '$stop_line' (expected run_stop1,run_stop2,run_stop3 from Gallop)"

back_line="$(one_line '^s8-back ' "s8-back")"
back_speed="$(field "$back_line" speed)"

edge_ride_line="$(one_line '^s8-edge-ride ' "s8-edge-ride")"
[[ "$(field "$edge_ride_line" blocked)" = "1" ]] || fail "the ridden edge is not blocked: $edge_ride_line"
[[ "$(field "$edge_ride_line" clips)" = "walk_start,walk,run_n_start,run_n,run_n_stop,stand1" ]] ||
  fail "the ridden edge did not stop through run_n_stop (R11): $edge_ride_line"

dismount_line="$(one_line '^s8-dismount ' "s8-dismount")"
[[ "$dismount_line" = "s8-dismount side=R" ]] || fail "s8-dismount is '$dismount_line' (expected side=R)"

edge_line="$(one_line '^s8-edge ' "s8-edge")"
[[ "$(field "$edge_line" blocked)" = "1" ]] || fail "the on-foot edge is not blocked: $edge_line"

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
[[ "$(field "$perf_line" frames)" = "240" ]] || fail "s8-perf frames=$(field "$perf_line" frames) (expected 240)"
perf_frame="$(field "$perf_line" frame_ms)"
awk -v ms="$perf_frame" 'BEGIN { exit !(ms > 0.0 && ms <= 16.7) }' ||
  fail "the plain frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
perf_culled="$(field "$perf_line" culled)"
[[ "$perf_culled" -gt 0 ]] || fail "s8-perf culled=$perf_culled (culling is not active)"

reset_line="$(one_line '^s8-reset ' "s8-reset")"
[[ "$reset_line" = "s8-reset stage=arena ride=OnFoot door=Idle valus=100 player=100" ]] ||
  fail "s8-reset is '$reset_line'"

one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' "s1-status" > /dev/null

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
  awk -v ms="$(field "$l" ms)" 'BEGIN { exit !(ms <= 15000) }' || fail "an e2e load took too long: $l"
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

# --- Step 8: the JSON cross-check ---------------------------------------------

stamp "cross-check: ride_points.h and the printed probes against placements.json"
python3 -I "$points_gen" "$placements" "$points_scratch" > /dev/null
cmp -s "$points_scratch" "$points_h" ||
  fail "app/src/ride_points.h is not the generator's output from $placements"
SEL_OUT="$selftest_out" python3 -I - "$placements" <<'PY'
import json, math, os, re, sys

d = json.load(open(sys.argv[1]))
out = os.environ["SEL_OUT"]
def line(prefix):
    return next(l for l in out.splitlines() if l.startswith(prefix))
def kv(l):
    return dict(t.split("=", 1) for t in l.split()[1:])

sp = d["speeds"]
for gait, key in (("Walk", "walk"), ("Trot", "trot"), ("Gallop", "gallop")):
    exp = float(kv(line("s8-gait gait=%s " % gait))["expected"])
    assert exp == round(sp[key], 3), f"{gait} expected {exp} != json {sp[key]}"
back = float(kv(line("s8-back "))["speed"])
assert abs(back - sp["back"]) <= 0.05 * abs(sp["back"]), f"back {back} vs json {sp['back']}"

# The whistle starts at the probe, Agro at her start.
w = d["probes"]["whistle_from"]; a = d["agro"]["p"]
dist = float(kv(line("s8-whistle "))["dist"])
want = math.hypot(a[0] - w[0], a[2] - w[1])
assert abs(dist - want) <= 0.01, f"whistle dist {dist} != probe {want:.2f}"

# Both edge stops lie on the edge segment, short of its end (the cliff).
e = d["probes"]["edge"]; f0, t0 = e["from"], e["to"]
L = math.hypot(t0[0] - f0[0], t0[1] - f0[1])
ux, uz = (t0[0] - f0[0]) / L, (t0[1] - f0[1]) / L
for p in ("s8-edge-ride ", "s8-edge "):
    x, z = (float(v) for v in kv(line(p))["stopped"].split(","))
    along = (x - f0[0]) * ux + (z - f0[1]) * uz
    off = abs(-(x - f0[0]) * uz + (z - f0[1]) * ux)
    assert 0.0 < along < L - 0.5 and off <= 2.0, f"{p.strip()} stopped at {x},{z}: along {along:.2f}/{L:.2f}, off {off:.2f}"
print("s8-acceptance: json cross-check OK (ride_points.h regenerated, 3 speeds + back, whistle %.2f m, 2 edge stops)" % want)
PY

# --- Step 9: the stills -------------------------------------------------------

span_for() {
  local want="$1"
  printf '%s\n' "$selftest_out" | awk -v want="$want" '
    /^s1-pixels / { line = $0 }
    /^s1-screenshot / { if ($2 == "path=" want) { print line; exit } }
  '
}

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

# --- Step 10: summary ---------------------------------------------------------

stamp "summary"
printf 's8-acceptance: library      %s\n' "$lib"
printf 's8-acceptance: arena sha    %s\n' "$arena_sha"
printf 's8-acceptance: shrine sha   %s\n' "$shrine_sha"
printf 's8-acceptance: plain sha    %s\n' "$plain_sha"
printf 's8-acceptance: audio sha    %s (34 entries)\n' "$audio_sha"
printf 's8-acceptance: audition     %s\n' "$audition_line"
printf 's8-acceptance: %s\n' "$load_line" "$whistle_line" "$mount_line"
printf 's8-acceptance: gaits        %s\n' "$gait_rows"
printf 's8-acceptance: %s\n' "$steer_line" "$stop_line" "$back_line" "$edge_ride_line" \
  "$dismount_line" "$edge_line" "$rider_line" "$audio_line" "$perf_line" "$reset_line"
printf 's8-acceptance: e2e          %s\n' "$(e2e_lines '^(s8e2e-|s7-load )' | tr '\n' ' ')"
printf 's8-acceptance: stills       %s\n' "$stills"
printf 's8-acceptance: s7 regress   %s\n' "$(printf '%s\n' "$s7_pass_line" | cut -c1-160)"
printf 's8-acceptance: PASS — build, both suites, the frozen S7 acceptance green (S6 → S5 → S4.2 → S4.1 → S4 → S3 inside it), the S8 decode (34 manifest entries = the map'"'"'s cue events; the audition gate 34 cues non-silent), the arena compose at sha %s…, the shrine and plain composes byte-identical (sha %s…, %s…), the five probe checks green; the plain load %s ms; the whistle from %s m, Agro at %s m with the neigh; the left mount (stand_ride_l); %s; the gallop yaw rate %s °/s (expected -45.00); the stop run_stop1,2,3; the back-up %s m/s; the ridden edge stopped at %s through run_n_stop (R11); the right dismount; the on-foot edge stopped at %s; the rider within %s m of the seated rest (R13); %s ride audio events (%s hooves); plain frame %s ms (drawn %s, culled %s); the reset exact; the end-to-end run (shrine → door → plain → whistle, mount, gallop %s m, stop, dismount → door → shrine → R → arena) green; ride_points.h regenerated byte-identical; four stills 2560x1440; windowed, cursor free\n' \
  "${arena_sha:0:8}" "${shrine_sha:0:8}" "${plain_sha:0:8}" "$load_ms" "$(field "$whistle_line" dist)" "$whistle_arrived" \
  "$gait_rows" "$steer_rate" "$back_speed" "$(field "$edge_ride_line" stopped)" "$(field "$edge_line" stopped)" \
  "$rider_offset" "$audio_events" "$audio_hooves" "$perf_frame" "$(field "$perf_line" drawn)" "$perf_culled" "$e2e_gallop"
