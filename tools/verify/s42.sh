#!/usr/bin/env bash
# s42.sh — the runner's adapter for scripts/s42-acceptance.sh (see common.sh): the fight timeline
# with stills, the stills-off purity run (relational: the s42-* lines must be byte-identical), the
# six stills and the perf pair, every gate verbatim; the nested S4.1 regression is s41.sh.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
wander_glb="$lib/$player"
valus_glb="$lib/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
club_glb="$lib/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a_weapon.glb"

section_gpu() {

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

need dotnet
need cmake
need ctest
need caffeinate
need shasum
need awk
need grep
need od
need wc

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter suite reads the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$wander_glb" ]] || fail "player glb not found under the library: $wander_glb"
[[ -f "$valus_glb" ]] || fail "Valus glb not found under the library: $valus_glb"
[[ -f "$club_glb" ]] || fail "club glb not found under the library: $club_glb"
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; the S4.1 regression runs it unchanged)"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"

# --- Step 3: the deterministic fight timeline ---------------------------------

# The stills live under a clean out/s42/: deleting the directory first
# guarantees a stale PNG cannot pass as this run's evidence.
stamp "clean: $out_dir"
rm -rf "$out_dir"
mkdir -p "$out_dir"

stamp "s42-selftest + stills: $encounter_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" --s42-selftest \
  --s42-stills "$out_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s42-selftest --s42-stills exited non-zero"
fi
printf '%s\n' "$selftest_out"

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

# field LINE KEY — the value after KEY= up to the next space.
field() {
  local line="$1" key="$2" rest
  rest="${line#*"$key"=}"
  [[ "$rest" != "$line" ]] || fail "field $key missing from: $line"
  printf '%s' "${rest%% *}"
}

# --- the two shell proxies ----------------------------------------------------

proxy_re='^s42-proxy weakpoint=[01] radius=[0-9]+(\.[0-9]+)?$'
proxy_count="$(line_count "$proxy_re")"
[[ "$proxy_count" = "2" ]] ||
  fail "expected two s42-proxy lines (weakpoints 0 and 1), found $proxy_count"

if ! proxy0_line="$(one_line '^s42-proxy weakpoint=0 radius=[0-9]+(\.[0-9]+)?$' "s42-proxy 0")"; then
  exit 1
fi
if ! proxy1_line="$(one_line '^s42-proxy weakpoint=1 radius=[0-9]+(\.[0-9]+)?$' "s42-proxy 1")"; then
  exit 1
fi
proxy0_radius="$(field "$proxy0_line" radius)"
proxy1_radius="$(field "$proxy1_line" radius)"
for radius in "$proxy0_radius" "$proxy1_radius"; do
  awk -v r="$radius" 'BEGIN { exit !(r + 0 > 0.5 && r + 0 < 5.0) }' ||
    fail "proxy radius $radius is not finite inside (0.5, 5.0)"
done

# --- the gate, route and attacks (literals pinned here, not against tuning) ---

if ! gate_line="$(one_line '^s42-gate foot=0 head=34$' "s42-gate")"; then
  exit 1
fi
if ! arm_line="$(one_line '^s42-arm band=right_arm weakpoint=1 damage=34$' "s42-arm")"; then
  exit 1
fi
if ! stomp_line="$(one_line '^s42-stomp damage=35 health=100->65 mode=hurt$' "s42-stomp")"; then
  exit 1
fi
if ! club_line="$(one_line '^s42-club damage=45 health=65->20$' "s42-club")"; then
  exit 1
fi
if ! miss_line="$(one_line '^s42-miss club=0 stomp=15$' "s42-miss")"; then
  exit 1
fi
if ! death_line="$(one_line '^s42-death clip=player_a_fall_damage_to_death mode=dead$' "s42-death")"; then
  exit 1
fi
if ! reset_line="$(one_line '^s42-reset health=100 stamina=100 mode=grounded valus=100$' "s42-reset")"; then
  exit 1
fi
if ! stamina_line="$(one_line '^s42-stamina from_y=28\.425 to_y=43\.484 remaining=[0-9]+$' "s42-stamina")"; then
  exit 1
fi
stamina_remaining="$(field "$stamina_line" remaining)"
[[ "$stamina_remaining" -gt 0 && "$stamina_remaining" -lt 100 ]] ||
  fail "s42-stamina remaining=$stamina_remaining (need 0 < remaining < 100)"

# --- the status line agrees with every event line above ----------------------

if ! status_line="$(one_line '^s42-status proxy=2 gate_foot=0 gate_head=34 arm_band=right_arm arm_weakpoint=1 arm_damage=34 stomp_damage=35 stomp_health=65 club_damage=45 club_health=20 death_clip=player_a_fall_damage_to_death reset_health=100 reset_stamina=100 reset_mode=grounded reset_valus=100 stamina_remaining=[0-9]+ defeated=0$' "s42-status")"; then
  exit 1
fi
status_proxy="$(field "$status_line" proxy)"
status_stamina="$(field "$status_line" stamina_remaining)"
[[ "$status_proxy" = "$proxy_count" ]] ||
  fail "s42-status proxy=$status_proxy but the timeline printed $proxy_count proxy lines"
[[ "$status_stamina" = "$stamina_remaining" ]] ||
  fail "s42-status stamina_remaining=$status_stamina but s42-stamina reported $stamina_remaining"
[[ "$(field "$status_line" arm_band)" = "right_arm" ]] ||
  fail "s42-status arm_band disagrees with s42-arm"
[[ "$(field "$status_line" arm_weakpoint)" = "1" ]] ||
  fail "s42-status arm_weakpoint disagrees with s42-arm"
[[ "$(field "$status_line" arm_damage)" = "34" ]] ||
  fail "s42-status arm_damage disagrees with s42-arm"
[[ "$(field "$status_line" stomp_damage)" = "35" ]] ||
  fail "s42-status stomp_damage disagrees with s42-stomp"
[[ "$(field "$status_line" stomp_health)" = "65" ]] ||
  fail "s42-status stomp_health disagrees with s42-stomp"
[[ "$(field "$status_line" club_damage)" = "45" ]] ||
  fail "s42-status club_damage disagrees with s42-club"
[[ "$(field "$status_line" club_health)" = "20" ]] ||
  fail "s42-status club_health disagrees with s42-club"
[[ "$(field "$status_line" death_clip)" = "player_a_fall_damage_to_death" ]] ||
  fail "s42-status death_clip disagrees with s42-death"
[[ "$(field "$status_line" reset_health)" = "100" ]] ||
  fail "s42-status reset_health disagrees with s42-reset"
[[ "$(field "$status_line" reset_stamina)" = "100" ]] ||
  fail "s42-status reset_stamina disagrees with s42-reset"
[[ "$(field "$status_line" reset_mode)" = "grounded" ]] ||
  fail "s42-status reset_mode disagrees with s42-reset"
[[ "$(field "$status_line" reset_valus)" = "100" ]] ||
  fail "s42-status reset_valus disagrees with s42-reset"

# --- the stills are observations: without them the event lines are identical --

stamp "s42-selftest without stills (timeline purity check)"
if ! plain_out="$(caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" --s42-selftest 2>&1)"; then
  printf '%s\n' "$plain_out" >&2
  fail "--s42-selftest without stills exited non-zero"
fi
printf '%s\n' "$selftest_out" | grep -E '^s42-' | grep -v '^s42-still ' > "$out_dir/events-with-stills.txt" ||
  true
printf '%s\n' "$plain_out" | grep -E '^s42-' > "$out_dir/events-without-stills.txt" || true
if ! diff -u "$out_dir/events-without-stills.txt" "$out_dir/events-with-stills.txt" \
  > "$out_dir/events.diff"; then
  cat "$out_dir/events.diff" >&2
  fail "the stills changed the s42-* event lines (see $out_dir/events.diff)"
fi
stamp "timeline purity: s42-* event lines identical with and without stills"

# --- Step 4: the six stills ---------------------------------------------------

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

# check_png PHASE PATH — magic, byte floor, 2560x1440, and the viewer's own
# non-uniformity span line.
check_png() {
  local phase="$1" path="$2" magic bytes pixels_line min_csv max_csv size w h
  local min_r min_g min_b max_r max_g max_b d largest
  [[ -f "$path" ]] || fail "$phase: no PNG at $path"
  magic="$(od -An -tx1 -N4 "$path" | tr -d ' \n')"
  [[ "$magic" = "89504e47" ]] || fail "$phase: $path lacks the PNG magic (got $magic)"
  bytes="$(wc -c < "$path" | tr -d '[:space:]')"
  [[ "$bytes" -gt 5000 ]] || fail "$phase: $path is only $bytes bytes (need > 5000)"
  size="$(size_for "$path")"
  [[ -n "$size" ]] || fail "$phase: no s1-screenshot line for $path"
  read -r w h <<<"$size"
  [[ "$w" = "2560" && "$h" = "1440" ]] ||
    fail "$phase: $path is ${w}x${h} (need 2560x1440)"
  pixels_line="$(span_for "$path")"
  [[ -n "$pixels_line" ]] || fail "$phase: no s1-pixels line for $path"
  min_csv="${pixels_line#*min=}"; min_csv="${min_csv%% *}"
  max_csv="${pixels_line#*max=}"; max_csv="${max_csv%% *}"
  IFS=, read -r min_r min_g min_b <<<"$min_csv"
  IFS=, read -r max_r max_g max_b <<<"$max_csv"
  largest=$((max_r - min_r))
  d=$((max_g - min_g)); if ((d > largest)); then largest=$d; fi
  d=$((max_b - min_b)); if ((d > largest)); then largest=$d; fi
  [[ "$largest" -ge 30 ]] ||
    fail "$phase: uniform frame (largest channel span $largest < 30): $pixels_line"
  printf '%s|%s|%s\n' "$path" "$bytes" "$largest"
}

still_total="$(line_count '^s42-still ')"
[[ "$still_total" = "6" ]] ||
  fail "expected six s42-still lines, found $still_total"

stills=""
for phase in stomp-telegraph stomp-impact club-swing hurt death arm-weakpoint; do
  path="$out_dir/$phase.png"
  count="$(line_count "^s42-still phase=$phase path=$path$")"
  [[ "$count" = "1" ]] ||
    fail "still $phase: expected one s42-still line for $path, found $count"
  row="$(check_png "$phase" "$path")"
  stills="${stills}${stills:+ }$row"
done

# --- Step 5: performance (load + marginal frame cost) -------------------------

# timed LOG CMD... — wall seconds with millisecond resolution (bash's TIMEFORMAT
# %R), with CMD's stdout+stderr kept in LOG, and CMD's own exit status returned:
# a failed run is never timed as a success. The caller must check the status.
timed() {
  local log="$1"
  shift
  local seconds status
  TIMEFORMAT='%R'
  seconds=$({ time "$@" >"$log" 2>&1; } 2>&1)
  status=$?
  printf '%s' "$seconds"
  return "$status"
}

# Both runs must succeed AND write their frame: the 240-frame timing only means
# something when 240 frames were actually rendered, and a load-only failure
# cannot masquerade as a plausible marginal cost.
perf_png="$out_dir/perf.png"
perf_log="$out_dir/perf-1.log"
perf_slow_log="$out_dir/perf-240.log"
rm -f "$perf_png"
if ! perf_ms="$(timed "$perf_log" caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" --screenshot "$perf_png" \
  --frames 1 --anim-time 0 --entity-time 0)"; then
  tail -20 "$perf_log" >&2
  fail "perf: the 1-frame run failed (log: $perf_log)"
fi
[[ -s "$perf_png" ]] || fail "perf: the 1-frame run wrote no capture"
rm -f "$perf_png"
if ! perf_slow_ms="$(timed "$perf_slow_log" caffeinate -dimsu "$viewer" --library "$lib" \
  --manifest "$encounter_manifest" --model "$player" --screenshot "$perf_png" \
  --frames 240 --anim-time 0 --entity-time 0)"; then
  tail -20 "$perf_slow_log" >&2
  fail "perf: the 240-frame run failed (log: $perf_slow_log)"
fi
[[ -s "$perf_png" ]] || fail "perf: the 240-frame run wrote no capture"
perf_pixels_line="$(grep -m1 '^s1-pixels ' "$perf_slow_log")" ||
  fail "perf: the 240-frame run printed no s1-pixels line"
perf_screenshot_line="$(grep -m1 '^s1-screenshot ' "$perf_slow_log")" ||
  fail "perf: the 240-frame run printed no s1-screenshot line"
perf_bytes="$(wc -c < "$perf_png" | tr -d '[:space:]')"
[[ "$perf_bytes" -gt 5000 ]] || fail "perf: the 240-frame capture is only $perf_bytes bytes"
rm -f "$perf_png"
printf '%s\n' "$perf_screenshot_line"
stamp "perf frames240: $perf_pixels_line"

marginal_ms="$(awk -v one="$perf_ms" -v many="$perf_slow_ms" \
  'BEGIN { printf "%.2f", (many - one) * 1000.0 / 239.0 }')"
marginal_fps="$(awk -v ms="$marginal_ms" 'BEGIN { if (ms > 0) printf "%.1f", 1000.0 / ms; else print "n/a" }')"
awk -v many="$perf_slow_ms" -v one="$perf_ms" 'BEGIN { exit !(many > one) }' ||
  fail "perf: the 240-frame run ($perf_slow_ms s) was not slower than the 1-frame run ($perf_ms s)"
awk -v ms="$marginal_ms" 'BEGIN { exit !(ms <= 16.7) }' ||
  fail "perf: marginal frame cost ${marginal_ms} ms is outside the 60 fps budget (16.7 ms)"
stamp "perf: load=${perf_ms}s frames240=${perf_slow_ms}s marginal=${marginal_ms}ms (~${marginal_fps} fps)"
}

run_section "$1"
