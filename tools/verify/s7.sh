#!/usr/bin/env bash
# s7.sh — the runner's adapter for scripts/s7-acceptance.sh (see common.sh).
#   probes: the arena compose's sha pin, the shrine compose = the pinned shrine.manifest, check-cwks,
#           check-manifest (arena, shrine) and check-encounter.
#   gpu:    the s7 return timeline + stills, the e2e (skipped by --quick), the placements JSON
#           cross-check (heredoc extracted), every gate verbatim. The nested S6 chain is s6.sh … s3.sh.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

probe="$repo_root/tools/re/stage/probe.py"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
shrine_manifest="$repo_root/out/re/stage/shrine.manifest"
placements="$repo_root/tools/re/s7/shrine_placements.json"
points_h="$repo_root/app/src/shrine_points.h"
stills_dir="$out_dir/stills"
selftest_txt="$out_dir/s7-selftest.txt"
e2e_txt="$out_dir/s7-e2e.txt"
arena_scratch="$out_dir/arena-compose.manifest"
shrine_scratch="$out_dir/shrine-compose.manifest"
arena_sha_pin="088e621894ecd38312526bdac11916fb355a645ecef4d0bace91e53b2440c1d0"

section_probes() {
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

rm -f "$arena_scratch" "$shrine_scratch"

stamp "probe.py compose (arena, scratch): must hash to the pinned S3 value"
checked compose-arena python3 "$probe" compose --library "$lib" --out "$arena_scratch"
arena_sha="$(shasum -a 256 "$arena_scratch" | cut -d' ' -f1)"
[[ "$arena_sha" = "$arena_sha_pin" ]] ||
  fail "the arena compose hashes to $arena_sha (expected $arena_sha_pin)"

stamp "probe.py compose --stage shrine (scratch): must equal $shrine_manifest"
checked compose-shrine python3 "$probe" compose --library "$lib" --stage shrine --out "$shrine_scratch"
cmp -s "$shrine_scratch" "$shrine_manifest" ||
  fail "the shrine compose differs from $shrine_manifest"
shrine_sha="$(shasum -a 256 "$shrine_manifest" | cut -d' ' -f1)"

stamp "probe.py check-cwks / check-manifest / check-encounter / check-manifest --stage shrine"
checked check-cwks python3 "$probe" check-cwks
checked check-manifest-arena python3 "$probe" check-manifest "$arena_scratch" --library "$lib"
checked check-encounter python3 "$probe" check-encounter "$encounter_manifest" --library "$lib"
checked check-manifest-shrine python3 "$probe" check-manifest "$shrine_manifest" --library "$lib" --stage shrine
keep arena-sha "$arena_sha"
}

section_gpu() {
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

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir"
mkdir -p "$stills_dir"
rm -f "$selftest_txt" "$e2e_txt" "$arena_scratch" "$shrine_scratch"

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

if [[ -z "${VERIFY_QUICK:-}" ]]; then   # --quick skips the e2e run
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
fi

# --- Step 6: the JSON cross-check (ruling R1) ---------------------------------

stamp "cross-check: printed probes and shrine_points.h against shrine_placements.json"
xcheck_py="$(heredoc_py s7 1)"
SEL_WALL="$wall_line" checked json-cross-check python3 -I "$xcheck_py" "$placements" "$points_h"

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

keep fade-luma "$fade_luma"
}

run_section "$1"
