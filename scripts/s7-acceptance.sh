#!/usr/bin/env bash
#
# s7-acceptance.sh — the S7 "Return to the Shrine" acceptance pass, re-runnable.
#
# Steps (the milestone: after Valus's death the screen fades out, the shrine
# loads, Wander wakes on the altar-hall floor and takes control; R returns to
# the arena):
#
#   1. build the converter solution and the published CLI, and app/ (Release);
#   2. both suites — `dotnet test tests/BpConvert.Tests` and `ctest`;
#   3. the **frozen S6 acceptance first** (`scripts/s6-acceptance.sh`, never
#      edited; it runs S5 -> S4.2 -> S4.1 -> S4 -> S3). Its own s6 outputs are
#      not re-asserted here beyond its PASS line;
#   4. the manifests: the arena compose re-run into a scratch path
#      (out/s7/arena-compose.manifest — out/re/stage/valus.manifest and
#      encounter.manifest are never rewritten) must hash to the pinned S3
#      value; the shrine compose re-run into a scratch path must be
#      byte-identical to out/re/stage/shrine.manifest; `check-cwks`,
#      `check-manifest` (arena), `check-encounter`, `check-manifest --stage
#      shrine`;
#   5. the deterministic return — `--s7-selftest --s7-stills out/s7/stills`:
#      the phase order Idle Hold FadeOut (R) Idle Hold FadeOut Load FadeIn Wake
#      Free, fade 1.000 at Load and 0.000 at Wake, four s7-load lines (arena,
#      shrine, arena, shrine; equal shrine mesh counts; each shrine load
#      inside 15 s), the ground probes (the floor under the feet, not the
#      roof), the wall probe stops the walk, the 16 idols (slot 1 broken), the
#      posed Mono, the perf line (<= 16.7 ms, culling active), the status and
#      the two resets, plus the `s1-status` window/cursor line;
#   5b. the end-to-end return — `--s7-e2e`: the *interactive* loop at a fixed
#      1/60 s with scripted input (one lethal hit through the production
#      combat path, then nothing; R 1 s into Free): the death Idle Motion
#      Collapse Done, the return Idle Hold FadeOut Load FadeIn Wake Free, the
#      three loads (arena, shrine, arena), the two wake clips in order, the
#      lock from the death through Wake and its release at Free, the feet on
#      the floor at Free (<= 0.05 m), the reset line and the window line;
#   6. the printed probe coordinates and the shrine_points.h transcription
#      against tools/re/s7/shrine_placements.json (ruling R1);
#   7. the five stills (fade, wake, altar, idols, hall): PNG magic, > 5000 B,
#      2560x1440, the viewer's non-uniformity span >= 30; fade.png (captured at
#      fade 0.6, authored) mean luma >= 180;
#   8. a summary block with every measured number.
#
# Every viewer and acceptance run is wrapped in `caffeinate -dimsu`. Never run
# two acceptances concurrently. The shrine loads take ~2 s each (four loads in
# the selftest, ceiling 15 s), the S6 chain ~10-15 min.
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library)
#
# Identity is not scriptable: these checks prove order, timing, geometry and
# presence, not that the wake looks like the game's. docs/notes/s7-results.md
# records every measured number; the user's walk checklist is PENDING there.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
probe="$repo_root/tools/re/stage/probe.py"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
shrine_manifest="$repo_root/out/re/stage/shrine.manifest"
placements="$repo_root/tools/re/s7/shrine_placements.json"
points_h="$repo_root/app/src/shrine_points.h"
player="models/shadow/characters/wander/_cmn/wander.glb"
cli="$repo_root/out/bin/bp-convert.dll"
out_dir="$repo_root/out/s7"
stills_dir="$out_dir/stills"
selftest_txt="$out_dir/s7-selftest.txt"
e2e_txt="$out_dir/s7-e2e.txt"
arena_scratch="$out_dir/arena-compose.manifest"
shrine_scratch="$out_dir/shrine-compose.manifest"
run_log="$repo_root/out/s7-acceptance-run.log"
s6_log="$repo_root/out/s7-s6-regression.log"
arena_sha_pin="088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s7-acceptance: $*" >&2; exit 1; }

mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

for c in dotnet cmake ctest caffeinate shasum awk grep sed sort tee tr wc python3 od cmp; do
  need "$c"
done

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
[[ -f "$encounter_manifest" ]] || fail "encounter manifest not found: $encounter_manifest"
[[ -f "$shrine_manifest" ]] || fail "shrine manifest not found: $shrine_manifest (probe.py compose --stage shrine)"
[[ -f "$placements" ]] || fail "shrine placements not found: $placements"

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

# --- Step 3: the frozen S6 acceptance -----------------------------------------

stamp "s6 regression: scripts/s6-acceptance.sh (it runs S5, S4.2, S4.1, S4 and S3)"
if ! caffeinate -dimsu "$repo_root/scripts/s6-acceptance.sh" > "$s6_log" 2>&1; then
  tail -40 "$s6_log" >&2
  fail "the frozen S6 acceptance regression failed (log: $s6_log)"
fi
s6_pass_line="$(grep -m1 -E '^s6-acceptance: PASS — ' "$s6_log" || true)"
[[ -n "$s6_pass_line" ]] || fail "the S6 regression did not print its final PASS — line"
s6_perf_line="$(grep -m1 -E '^s6-acceptance: s42 perf ' "$s6_log" || true)"
printf '%s\n' "$s6_pass_line" | cut -c1-200
printf '%s\n' "$s6_perf_line"

# --- Step 4: the manifests ----------------------------------------------------

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir"
mkdir -p "$stills_dir"
rm -f "$selftest_txt" "$e2e_txt" "$arena_scratch" "$shrine_scratch"

stamp "probe.py compose (arena, scratch): must hash to the pinned S3 value"
python3 "$probe" compose --library "$lib" --out "$arena_scratch"
arena_sha="$(shasum -a 256 "$arena_scratch" | cut -d' ' -f1)"
[[ "$arena_sha" = "$arena_sha_pin" ]] ||
  fail "the arena compose hashes to $arena_sha (expected $arena_sha_pin)"

stamp "probe.py compose --stage shrine (scratch): must equal $shrine_manifest"
python3 "$probe" compose --library "$lib" --stage shrine --out "$shrine_scratch"
cmp -s "$shrine_scratch" "$shrine_manifest" ||
  fail "the shrine compose differs from $shrine_manifest"
shrine_sha="$(shasum -a 256 "$shrine_manifest" | cut -d' ' -f1)"

stamp "probe.py check-cwks / check-manifest / check-encounter / check-manifest --stage shrine"
python3 "$probe" check-cwks
python3 "$probe" check-manifest "$arena_scratch" --library "$lib"
python3 "$probe" check-encounter "$encounter_manifest" --library "$lib"
python3 "$probe" check-manifest "$shrine_manifest" --library "$lib" --stage shrine

# --- Step 5: the deterministic return -----------------------------------------

stamp "s7-selftest + stills: $encounter_manifest -> $shrine_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --shrine-manifest "$shrine_manifest" \
  --model "$player" --s7-selftest --s7-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s7-selftest --s7-stills exited non-zero"
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

# 10 phases + 4 loads + 2 resets + 4 ground + wall + idols + mono + perf + status.
s7_total="$(line_count '^s7-')"
[[ "$s7_total" = "25" ]] ||
  fail "expected 25 s7-* lines (10 phase, 4 load, 2 reset, 4 ground, wall, idols, mono, perf, status), found $s7_total"

# --- the phase machine --------------------------------------------------------

phase_order="$(printf '%s\n' "$selftest_out" | grep -E '^s7-phase ' |
  awk '{ print $2 }' | sed 's/phase=//' | tr '\n' ' ')"
expected_order="Idle Hold FadeOut Idle Hold FadeOut Load FadeIn Wake Free "
[[ "$phase_order" = "$expected_order" ]] ||
  fail "the phase order is '$phase_order' (expected '$expected_order': R during the first FadeOut re-seeds, then the return completes)"

load_phase_line="$(one_line '^s7-phase phase=Load ' "s7-phase Load")"
wake_phase_line="$(one_line '^s7-phase phase=Wake ' "s7-phase Wake")"
free_phase_line="$(one_line '^s7-phase phase=Free ' "s7-phase Free")"
[[ "$(field "$load_phase_line" fade)" = "1.000" ]] ||
  fail "s7-phase Load fade=$(field "$load_phase_line" fade) (expected 1.000)"
[[ "$(field "$load_phase_line" lock)" = "1" ]] || fail "s7-phase Load is not input-locked"
[[ "$(field "$wake_phase_line" fade)" = "0.000" ]] ||
  fail "s7-phase Wake fade=$(field "$wake_phase_line" fade) (expected 0.000)"
[[ "$(field "$wake_phase_line" lock)" = "1" ]] || fail "s7-phase Wake is not input-locked"
[[ "$(field "$free_phase_line" lock)" = "0" ]] || fail "s7-phase Free is still input-locked"

# --- the four stage loads -----------------------------------------------------

loads_file="$out_dir/s7-loads.txt"
printf '%s\n' "$selftest_out" | grep -E '^s7-load ' > "$loads_file" || true
load_re='^s7-load stage=(arena|shrine) instances=[0-9]+ meshes=[0-9]+ ms=[0-9]+\.[0-9]$'
[[ "$(grep -c -E "$load_re" "$loads_file" || true)" = "4" ]] ||
  fail "expected 4 well-formed s7-load lines"
load_order="$(awk '{ print $2 }' "$loads_file" | sed 's/stage=//' | tr '\n' ' ')"
[[ "$load_order" = "arena shrine arena shrine " ]] ||
  fail "the load order is '$load_order' (expected 'arena shrine arena shrine ')"
load1="$(sed -n 1p "$loads_file")"; load2="$(sed -n 2p "$loads_file")"
load3="$(sed -n 3p "$loads_file")"; load4="$(sed -n 4p "$loads_file")"
shrine_meshes="$(field "$load2" meshes)"
[[ "$(field "$load4" meshes)" = "$shrine_meshes" ]] ||
  fail "the reloaded shrine has $(field "$load4" meshes) meshes, the first load had $shrine_meshes"
[[ "$(field "$load3" meshes)" = "$(field "$load1" meshes)" ]] ||
  fail "the reloaded arena has $(field "$load3" meshes) meshes, the first load had $(field "$load1" meshes)"
# Ruling R15: the ceiling is 15 s (R11 had 60 s for the pre-fix 24-27 s loads; the texture dedup cut them to ~2 s).
for l in "$load2" "$load4"; do
  awk -v ms="$(field "$l" ms)" 'BEGIN { exit !(ms <= 15000) }' ||
    fail "a shrine load took $(field "$l" ms) ms (ceiling 15000)"
done

# --- the ground, the wall, the idols, Mono ------------------------------------

ground_rows=""
for name in spawn altar_step hall stair; do
  gline="$(one_line "^s7-ground probe=$name " "s7-ground $name")"
  gfloor="$(field "$gline" floor)"
  gmax="$(field "$gline" column_max)"
  if [[ "$name" = "spawn" ]]; then
    awk -v f="$gfloor" 'BEGIN { exit !(f >= 8.5 && f <= 12.5) }' ||
      fail "the spawn floor is $gfloor (expected 8.5..12.5)"
  else
    awk -v f="$gfloor" -v m="$gmax" 'BEGIN { exit !(f < m - 1.0) }' ||
      fail "probe $name: floor $gfloor is not below the column maximum $gmax by 1 m (the roof was read)"
  fi
  ground_rows="${ground_rows}${ground_rows:+, }${name} floor ${gfloor} (column max ${gmax})"
done

wall_line="$(one_line '^s7-wall ' "s7-wall")"
[[ "$(field "$wall_line" blocked)" = "1" ]] || fail "s7-wall is not blocked: $wall_line"
wall_to="$(field "$wall_line" to)"
wall_stopped="$(field "$wall_line" stopped)"
awk -v to="${wall_to%%,*}" -v st="${wall_stopped%%,*}" 'BEGIN { exit !(st < to - 0.5) }' ||
  fail "the wall walk stopped at $wall_stopped, not short of $wall_to"

idols_line="$(one_line '^s7-idols ' "s7-idols")"
[[ "$idols_line" = "s7-idols count=16 broken_slot=1" ]] ||
  fail "s7-idols is '$idols_line' (expected 's7-idols count=16 broken_slot=1')"
mono_line="$(one_line '^s7-mono ' "s7-mono")"
[[ "$(field "$mono_line" present)" = "1" ]] || fail "s7-mono is not present: $mono_line"

# --- the perf, the status, the resets -----------------------------------------

perf_line="$(one_line '^s7-perf ' "s7-perf")"
[[ "$(field "$perf_line" frames)" = "240" ]] || fail "s7-perf frames=$(field "$perf_line" frames) (expected 240)"
perf_frame="$(field "$perf_line" frame_ms)"
awk -v ms="$perf_frame" 'BEGIN { exit !(ms > 0.0 && ms <= 16.7) }' ||
  fail "the shrine frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
perf_culled="$(field "$perf_line" culled)"
[[ "$perf_culled" -gt 0 ]] || fail "s7-perf culled=$perf_culled (culling is not active)"

status_line="$(one_line '^s7-status ' "s7-status")"
[[ "$status_line" = "s7-status stage=shrine phase=Free lock=0" ]] ||
  fail "s7-status is '$status_line' (expected 's7-status stage=shrine phase=Free lock=0')"

[[ "$(line_count '^s7-reset ')" = "2" ]] || fail "expected two s7-reset lines (R mid-fade, R from the shrine)"
reset_exact="s7-reset stage=arena phase=Idle death=Idle valus=100 player=100"
[[ "$(line_count "^$reset_exact\$")" = "2" ]] ||
  fail "both s7-reset lines must be exactly '$reset_exact'"

status_window="$(one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' "s1-status")"

# --- Step 5b: the end-to-end return (the interactive loop, scripted) ----------

stamp "s7-e2e: the interactive loop from one lethal hit to the shrine and back"
if ! e2e_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --shrine-manifest "$shrine_manifest" \
  --model "$player" --s7-e2e 2>&1)"; then
  printf '%s\n' "$e2e_out" >&2
  fail "--s7-e2e exited non-zero"
fi
printf '%s\n' "$e2e_out"
printf '%s\n' "$e2e_out" > "$e2e_txt"

e2e_lines() { printf '%s\n' "$e2e_out" | grep -E "$1" || true; }
e2e_one() {
  local regex="$1" label="$2"
  [[ "$(e2e_lines "$regex" | grep -c . || true)" = "1" ]] || fail "s7-e2e $label: expected exactly one line"
  e2e_lines "$regex"
}
e2e_order() { e2e_lines "^$1 " | awk -v k="$2" '{ for (i = 2; i <= NF; i++) if (index($i, k "=") == 1) print substr($i, length(k) + 2) }' | tr '\n' ' '; }

# 4 death + 7 return + 3 load + 2 input + 2 wake + status + reset.
e2e_total="$(e2e_lines '^s7e2e-' | grep -c . || true)"
[[ "$e2e_total" = "20" ]] || fail "expected 20 s7e2e-* lines, found $e2e_total"
e2e_death="$(e2e_order s7e2e-death phase)"
[[ "$e2e_death" = "Idle Motion Collapse Done " ]] ||
  fail "the e2e death order is '$e2e_death' (expected 'Idle Motion Collapse Done ')"
e2e_return="$(e2e_order s7e2e-return phase)"
[[ "$e2e_return" = "Idle Hold FadeOut Load FadeIn Wake Free " ]] ||
  fail "the e2e return order is '$e2e_return' (expected 'Idle Hold FadeOut Load FadeIn Wake Free ')"
e2e_loads="$(e2e_order s7e2e-load stage)"
[[ "$e2e_loads" = "arena shrine arena " ]] ||
  fail "the e2e load order is '$e2e_loads' (expected 'arena shrine arena ')"
[[ "$(e2e_lines '^s7e2e-load stage=(arena|shrine) meshes=[0-9]+ ms=[0-9]+\.[0-9]$' | grep -c . || true)" = "3" ]] ||
  fail "expected 3 well-formed s7e2e-load lines"
e2e_shrine_load="$(e2e_one '^s7e2e-load stage=shrine ' "shrine load")"
awk -v ms="$(field "$e2e_shrine_load" ms)" 'BEGIN { exit !(ms <= 15000) }' ||
  fail "the e2e shrine load took $(field "$e2e_shrine_load" ms) ms (ceiling 15000)"
e2e_wakes="$(e2e_order s7e2e-wake clip)"
[[ "$e2e_wakes" = "player_a_home_boss01_cut04 player_a_home_boss01_cut05 " ]] ||
  fail "the e2e wake clips are '$e2e_wakes' (expected cut04 then cut05)"
e2e_input="$(e2e_order s7e2e-input locked)"
[[ "$e2e_input" = "1 0 " ]] || fail "the e2e input lines are locked='$e2e_input' (expected '1 0 ': locked at the death, released at Free)"
e2e_load_ret="$(e2e_one '^s7e2e-return phase=Load ' "return Load")"
[[ "$(field "$e2e_load_ret" fade)" = "1.000" && "$(field "$e2e_load_ret" lock)" = "1" ]] ||
  fail "the e2e Load is not opaque and locked: $e2e_load_ret"
e2e_wake_ret="$(e2e_one '^s7e2e-return phase=Wake ' "return Wake")"
[[ "$(field "$e2e_wake_ret" lock)" = "1" ]] || fail "the e2e Wake is not locked: $e2e_wake_ret"
e2e_free_ret="$(e2e_one '^s7e2e-return phase=Free ' "return Free")"
[[ "$(field "$e2e_free_ret" lock)" = "0" ]] || fail "the e2e Free is still locked: $e2e_free_ret"
e2e_status="$(e2e_one '^s7e2e-status ' "status")"
[[ "$e2e_status" = "s7e2e-status stage=shrine phase=Free lock=0 "* ]] ||
  fail "s7e2e-status is '$e2e_status' (expected stage=shrine phase=Free lock=0)"
awk -v y="$(field "$e2e_status" player_y)" -v f="$(field "$e2e_status" floor)" \
  'BEGIN { d = y - f; if (d < 0) d = -d; exit !(d <= 0.05) }' ||
  fail "the e2e player is off the floor at Free: $e2e_status"
e2e_reset="$(e2e_one '^s7e2e-reset ' "reset")"
[[ "$e2e_reset" = "s7e2e-reset stage=arena valus=100 player=100 death=Idle return=Idle" ]] ||
  fail "s7e2e-reset is '$e2e_reset'"
[[ "$(e2e_lines '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' | grep -c . || true)" = "1" ]] ||
  fail "the e2e run is not windowed with the cursor free"

# --- Step 6: the JSON cross-check (ruling R1) ---------------------------------

stamp "cross-check: printed probes and shrine_points.h against shrine_placements.json"
SEL_WALL="$wall_line" python3 -I - "$placements" "$points_h" <<'PY'
import json, os, re, sys

d = json.load(open(sys.argv[1]))
h = open(sys.argv[2]).read()
p, flow = d["probes"], d["flow"]

# The printed wall segment (%.2f) equals the JSON's.
m = re.match(r"s7-wall from=(\S+),(\S+) to=(\S+),(\S+) ", os.environ["SEL_WALL"])
got = [float(v) for v in m.groups()]
want = p["wall"]["from"] + p["wall"]["to"]
assert got == [round(v, 2) for v in want], f"wall {got} != json {want}"

# The constexpr transcription equals the JSON (the app parses no JSON).
def pt(name):
    m = re.search(r"k%s\{(-?[\d.]+)f, (-?[\d.]+)f\}" % name, h)
    return [float(m.group(1)), float(m.group(2))]
for name, key in (("ProbeAltarStep", "altar_step"), ("ProbeHall", "hall"),
                  ("ProbeStair", "stair")):
    assert pt(name) == p[key], f"{name} {pt(name)} != json {p[key]}"
assert pt("ProbeWallFrom") == p["wall"]["from"] and pt("ProbeWallTo") == p["wall"]["to"]
def num(name):
    return float(re.search(r"k%s = (-?[\d.]+)f;" % name, h).group(1))
assert num("ShrineHoldSeconds") == flow["hold_s"]
assert num("ShrineFadeOutSeconds") == flow["fade_out_s"]
assert num("ShrineFadeInSeconds") == flow["fade_in_s"]
colour = re.search(r"kShrineFadeColour\{([^}]*)\}", h).group(1)
assert [float(v.strip().rstrip("f")) for v in colour.split(",")] == [float(v) for v in flow["colour"]]
print("s7-acceptance: json cross-check OK (wall, 4 probes, flow timings and colour)")
PY

# --- Step 7: the stills -------------------------------------------------------

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
for name in fade wake altar idols hall; do
  path="$stills_dir/$name.png"
  [[ "$(line_count "^s1-screenshot path=$path width=2560 height=1440\$")" = "1" ]] ||
    fail "still $name: expected one s1-screenshot line for $path"
  stills="${stills}${stills:+ }$(check_png "$name" "$path")"
done

# fade.png is the arena mid-fade to white (fade 0.6, authored): bright by design.
fade_mean="$(span_for "$stills_dir/fade.png" | sed -E 's/.*mean=([0-9.]+),([0-9.]+),([0-9.]+).*/\1 \2 \3/')"
fade_luma="$(awk -v m="$fade_mean" 'BEGIN { split(m, c, " "); printf "%.1f", 0.2126*c[1] + 0.7152*c[2] + 0.0722*c[3] }')"
awk -v l="$fade_luma" 'BEGIN { exit !(l >= 180) }' ||
  fail "fade.png mean luma $fade_luma < 180 (the white fade is not showing)"

# --- Step 8: summary ----------------------------------------------------------

stamp "summary"
printf 's7-acceptance: library      %s\n' "$lib"
printf 's7-acceptance: arena sha    %s\n' "$arena_sha"
printf 's7-acceptance: shrine sha   %s\n' "$shrine_sha"
printf 's7-acceptance: phases       %s\n' "$(printf '%s\n' "$selftest_out" | grep -E '^s7-phase ' | tr '\n' ' ')"
while IFS= read -r l; do printf 's7-acceptance: %s\n' "$l"; done < "$loads_file"
printf 's7-acceptance: ground       %s\n' "$ground_rows"
printf 's7-acceptance: wall         %s\n' "$wall_line"
printf 's7-acceptance: idols        %s\n' "$idols_line"
printf 's7-acceptance: mono         %s\n' "$mono_line"
printf 's7-acceptance: perf         %s\n' "$perf_line"
printf 's7-acceptance: status       %s\n' "$status_line"
printf 's7-acceptance: e2e          %s\n' "$(e2e_lines '^s7e2e-(death|return|input|wake)' | tr '\n' ' ')"
printf 's7-acceptance: e2e loads    %s\n' "$(e2e_lines '^s7e2e-load ' | tr '\n' ' ')"
printf 's7-acceptance: e2e status   %s\n' "$e2e_status"
printf 's7-acceptance: stills       %s\n' "$stills"
printf 's7-acceptance: fade luma    %s\n' "$fade_luma"
printf 's7-acceptance: s6 regress   %s\n' "$s6_perf_line"
printf 's7-acceptance: PASS — build, both suites, the frozen S6 acceptance green, the arena compose at sha %s…, the shrine compose byte-identical (sha %s…), check-cwks/check-manifest/check-encounter/check-manifest --stage shrine green; the return Idle Hold FadeOut (R) Idle Hold FadeOut Load FadeIn Wake Free, fade 1.000 at Load and 0.000 at Wake; four s7-loads (shrine %s meshes both times, %s and %s ms); ground floors %s; the wall walk stopped at %s (to %s); 16 idols (slot 1 broken), Mono present; shrine frame %s ms (drawn %s, culled %s); the two resets exact; the end-to-end run (the interactive loop from one lethal hit: death Idle Motion Collapse Done, return Idle Hold FadeOut Load FadeIn Wake Free, cut04 then cut05, %s) green; five stills 2560x1440 (fade luma %s); windowed, cursor free\n' \
  "${arena_sha:0:8}" "${shrine_sha:0:8}" "$shrine_meshes" "$(field "$load2" ms)" "$(field "$load4" ms)" \
  "$ground_rows" "$wall_stopped" "$wall_to" "$perf_frame" "$(field "$perf_line" drawn)" "$perf_culled" "$e2e_status" "$fade_luma"
