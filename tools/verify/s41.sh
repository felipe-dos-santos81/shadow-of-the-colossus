#!/usr/bin/env bash
# s41.sh — the runner's adapter for scripts/s41-acceptance.sh (see common.sh): the fight timeline,
# the five stills + three controls, the pixel guards (heredoc extracted) and the perf pair, every
# gate verbatim; the builds/suites are run.py's and the nested S4 regression is s4.sh.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
wander_glb="$lib/$player"
valus_glb="$lib/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
control_dir="$out_dir/control"

section_gpu() {

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

need dotnet
need cmake
need ctest
need python3
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
[[ -f "$encounter_manifest" ]] ||
  fail "encounter manifest not found: $encounter_manifest — run ./scripts/s4-acceptance.sh once (it composes it; this pass runs it unchanged as the regression)"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"

# --- Step 3: the deterministic fight timeline ---------------------------------

# The stills and controls live under a clean out/s41/: deleting the directory
# first guarantees a stale PNG cannot pass as this run's evidence.
stamp "clean: $out_dir"
rm -rf "$out_dir"
mkdir -p "$control_dir"

stamp "s41-selftest: $encounter_manifest"
if ! selftest_out="$("$viewer" --library "$lib" --manifest "$encounter_manifest" \
  --model "$player" --s41-selftest --s41-stills "$out_dir" \
  --screenshot "$out_dir/defeat.png" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s41-selftest exited non-zero"
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

band_re='left_foot|left_shin|left_thigh|waist|chest|neck|head|right_shoulder|right_arm'

if ! grab_line="$(one_line "^s41-grab frames=[0-9]+ band=($band_re) pos=-?[0-9.]+,-?[0-9.]+,-?[0-9.]+$" "s41-grab")"; then
  exit 1
fi
grab_frames="$(field "$grab_line" frames)"
grab_band="$(field "$grab_line" band)"
grab_pos="$(field "$grab_line" pos)"
[[ "$grab_frames" -gt 0 ]] || fail "s41-grab frames=$grab_frames (the approach made no progress)"
[[ "$grab_band" = "left_foot" ]] || fail "s41-grab band=$grab_band (need left_foot)"
awk -F, -v p="$grab_pos" 'BEGIN { n = split(p, a, ","); exit !(n == 3) }' ||
  fail "s41-grab pos=$grab_pos is not three components"

if ! climb_line="$(one_line "^s41-climb frames=240 from_y=-?[0-9.]+ to_y=-?[0-9.]+ band=($band_re)$" "s41-climb")"; then
  exit 1
fi
climb_frames="$(field "$climb_line" frames)"
climb_from_y="$(field "$climb_line" from_y)"
climb_to_y="$(field "$climb_line" to_y)"
climb_band="$(field "$climb_line" band)"
[[ "$climb_frames" = "240" ]] || fail "s41-climb frames=$climb_frames (need the 240-frame pinned climb)"
[[ "$climb_band" = "left_shin" ]] || fail "s41-climb band=$climb_band (need left_shin)"
climb_rise="$(awk -v a="$climb_from_y" -v b="$climb_to_y" 'BEGIN { printf "%.2f", b - a }')"
awk -v r="$climb_rise" 'BEGIN { exit !(r >= 3.0) }' ||
  fail "s41-climb rise=${climb_rise} m (need >= 3.0)"
awk -v a="$climb_from_y" -v b="$climb_to_y" 'BEGIN { exit !(b > a) }' ||
  fail "s41-climb went backwards ($climb_from_y -> $climb_to_y)"

strike_re='^s41-strike swing=[0-9]+ damage=[0-9]+ health=[0-9]+->[0-9]+ weakpoint=-?[0-9]+$'
strike_count="$(line_count "$strike_re")"
[[ "$strike_count" = "3" ]] ||
  fail "expected three s41-strike lines (full + weak + kill), found $strike_count"
strike1="$(printf '%s\n' "$selftest_out" | grep -m1 -E "$strike_re")" ||
  fail "s41-strike: missing the full-charge hit"
strike2="$(printf '%s\n' "$selftest_out" | grep -E "$strike_re" | sed -n '2p')" ||
  fail "s41-strike: missing the weak hit"
strike3="$(printf '%s\n' "$selftest_out" | grep -E "$strike_re" | sed -n '3p')" ||
  fail "s41-strike: missing the killing hit"
strike1_swing="$(field "$strike1" swing)"
strike1_damage="$(field "$strike1" damage)"
strike1_health="$(field "$strike1" health)"
strike1_weakpoint="$(field "$strike1" weakpoint)"
strike2_swing="$(field "$strike2" swing)"
strike2_damage="$(field "$strike2" damage)"
strike2_health="$(field "$strike2" health)"
strike2_weakpoint="$(field "$strike2" weakpoint)"
strike3_swing="$(field "$strike3" swing)"
strike3_damage="$(field "$strike3" damage)"
strike3_health="$(field "$strike3" health)"
strike3_weakpoint="$(field "$strike3" weakpoint)"
[[ "$strike1_swing" = "1" ]] || fail "s41-strike swing=$strike1_swing (need 1)"
[[ "$strike1_damage" = "34" ]] || fail "the full-charge strike dealt $strike1_damage (need 34)"
[[ "$strike1_health" = "100->66" ]] || fail "the full-charge strike health is $strike1_health (need 100->66)"
[[ "$strike1_weakpoint" = "0" ]] || fail "the full-charge strike weakpoint=$strike1_weakpoint (need the forehead)"
[[ "$strike2_swing" = "2" ]] || fail "s41-strike swing=$strike2_swing (need 2)"
[[ "$strike2_damage" = "20" ]] || fail "the weak strike dealt $strike2_damage (need 20)"
[[ "$strike2_health" = "66->46" ]] || fail "the weak strike health is $strike2_health (need 66->46)"
# R10's 1.75 m head band makes the head the nearest catch band, so the weak
# strike lands on the forehead weakpoint. It still exercises the weak damage
# path; the old neck catch's -1 is no longer the geometry.
[[ "$strike2_weakpoint" = "0" ]] ||
  fail "the weak strike weakpoint=$strike2_weakpoint (need the forehead after R10's head catch)"
[[ "$strike3_swing" = "3" ]] || fail "s41-strike swing=$strike3_swing (need 3)"
[[ "$strike3_damage" = "34" ]] || fail "the killing strike dealt $strike3_damage (need 34)"
[[ "$strike3_health" = "34->0" ]] ||
  fail "the killing strike health is $strike3_health (need 34->0)"
[[ "$strike3_weakpoint" = "0" ]] || fail "the killing strike weakpoint=$strike3_weakpoint (need the forehead)"
h1_before="${strike1_health%%->*}"
h1_after="${strike1_health##*->}"
h2_before="${strike2_health%%->*}"
h2_after="${strike2_health##*->}"
h3_before="${strike3_health%%->*}"
h3_after="${strike3_health##*->}"
[[ $((h1_before - h1_after)) -eq "$strike1_damage" ]] ||
  fail "the full strike's health arithmetic does not match its damage: $strike1_health vs $strike1_damage"
[[ $((h2_before - h2_after)) -eq "$strike2_damage" ]] ||
  fail "the weak strike's health arithmetic does not match its damage: $strike2_health vs $strike2_damage"
[[ "$h2_before" -eq "$h1_after" ]] ||
  fail "the weak strike starts at $h2_before HP, not the full strike's $h1_after"
[[ $((h3_before - h3_after)) -eq "$strike3_damage" ]] ||
  fail "the killing strike's health arithmetic does not match its damage: $strike3_health vs $strike3_damage"
[[ "$h3_before" = "34" ]] ||
  fail "the killing strike starts at $h3_before HP, not the selftest's one-plunge set-up 34"

if ! player_px_line="$(one_line '^s41-strike player_px=[0-9]+,[0-9]+,[0-9]+,[0-9]+$' "s41-strike player_px")"; then
  exit 1
fi
player_px="$(field "$player_px_line" player_px)"
IFS=, read -r player_x player_y player_w player_h <<<"$player_px"
if ((player_w <= 0 || player_h <= 0)); then
  fail "s41-strike player_px=$player_px is empty"
fi
if ((player_x < 0 || player_y < 0 || player_x + player_w > 2560 ||
     player_y + player_h > 1440)); then
  fail "s41-strike player_px=$player_px is not inside the 2560x1440 strike still"
fi

if ! catch_line="$(one_line "^s41-catch band=($band_re)$" "s41-catch")"; then
  exit 1
fi
catch_band="$(field "$catch_line" band)"
# R10's 1.75 m head band is the nearest band at the shake-off point, so the
# catch is the head (the neck was the pre-R10 geometry).
[[ "$catch_band" = "head" ]] ||
  fail "s41-catch band=$catch_band (R10's head band is the nearest catch band)"

if ! fall_line="$(one_line '^s41-fall frames=[0-9]+ landed=1$' "s41-fall")"; then
  exit 1
fi
fall_frames="$(field "$fall_line" frames)"
[[ "$fall_frames" -gt 0 ]] || fail "s41-fall frames=$fall_frames"

if ! stamina_line="$(one_line '^s41-stamina fall=1$' "s41-stamina")"; then
  exit 1
fi

if ! death_line="$(one_line '^s41-death clip=[a-z0-9_]+ defeated=1$' "s41-death")"; then
  exit 1
fi
death_clip="$(field "$death_line" clip)"
[[ "$death_clip" = "minotaur_a_death_front" ]] ||
  fail "s41-death clip=$death_clip (need minotaur_a_death_front)"

if ! settle_line="$(one_line '^s41-settle frames=[0-9]+ grounded=1$' "s41-settle")"; then
  exit 1
fi
settle_frames="$(field "$settle_line" frames)"
[[ "$settle_frames" -gt 0 ]] || fail "s41-settle frames=$settle_frames (the fall did not advance)"

if ! status_line="$(one_line "^s41-status grab=($band_re) climb=-?[0-9.]+ strikes=[0-9]+ entity_health=0 mode=grounded defeated=1$" "s41-status")"; then
  exit 1
fi
status_grab="$(field "$status_line" grab)"
status_climb="$(field "$status_line" climb)"
status_strikes="$(field "$status_line" strikes)"
status_health="$(field "$status_line" entity_health)"
status_mode="$(field "$status_line" mode)"
[[ "$status_grab" = "$grab_band" ]] ||
  fail "s41-status grab=$status_grab but s41-grab reported $grab_band"
awk -v a="$status_climb" -v b="$climb_rise" \
  'BEGIN { d = a - b; exit !(d <= 0.005 && d >= -0.005) }' ||
  fail "s41-status climb=$status_climb does not match the measured rise $climb_rise"
[[ "$status_strikes" = "3" ]] || fail "s41-status strikes=$status_strikes (need 3)"
[[ "$status_health" = "0" ]] || fail "s41-status entity_health=$status_health (need 0)"
[[ "$status_mode" = "grounded" ]] ||
  fail "s41-status mode=$status_mode (the defeat still must show the player landed)"

if ! defeat_line="$(one_line '^s41-defeat bbox=-?[0-9]+,-?[0-9]+,-?[0-9]+,-?[0-9]+ screen=[0-9]+x[0-9]+$' "s41-defeat")"; then
  exit 1
fi
defeat_bbox="$(field "$defeat_line" bbox)"
defeat_screen="$(field "$defeat_line" screen)"
[[ "$defeat_screen" = "2560x1440" ]] ||
  fail "s41-defeat screen=$defeat_screen (need 2560x1440)"
IFS=, read -r defeat_x0 defeat_y0 defeat_x1 defeat_y1 <<<"$defeat_bbox"
if ((defeat_x1 <= defeat_x0 || defeat_y1 <= defeat_y0)); then
  fail "s41-defeat bbox=$defeat_bbox is empty"
fi

# --- Step 4: the five stills --------------------------------------------------

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

stills=""
for phase in grab mid-climb strike reaction defeat; do
  path="$out_dir/$phase.png"
  count="$(line_count "^s41-still phase=$phase path=$path$")"
  [[ "$count" = "1" ]] || fail "still $phase: expected one s41-still line for $path, found $count"
  row="$(check_png "$phase" "$path")"
  stills="${stills}${stills:+ }$row"
done

for control in reaction-nohud defeat-noentity defeat-nohud; do
  path="$control_dir/$control.png"
  count="$(line_count "^s41-control phase=$control path=$path$")"
  [[ "$count" = "1" ]] || fail "control $control: expected one s41-control line for $path, found $count"
  check_png "control $control" "$path" >/dev/null
done

# --- Step 5: pixel guards (presence, defeat localization, HUD) ----------------

pixel_py="$(heredoc_py s41 1)"

if ! pixels_out="$(python3 "$pixel_py" "$out_dir/defeat.png" \
  "$control_dir/defeat-noentity.png" "$control_dir/defeat-nohud.png" \
  "$out_dir/reaction.png" "$control_dir/reaction-nohud.png" \
  "$out_dir/grab.png" "$out_dir/strike.png" "$defeat_bbox" 16 2>&1)"; then
  printf '%s\n' "$pixels_out" >&2
  fail "the pixel guards failed"
fi
printf '%s\n' "$pixels_out"

if ! defeat_region_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s41-defeat-region ')"; then
  fail "pixel guards: no s41-defeat-region line"
fi
if ! defeat_hud_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s41-defeat-hud ')"; then
  fail "pixel guards: no s41-defeat-hud line"
fi
if ! reaction_hud_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s41-reaction-hud ')"; then
  fail "pixel guards: no s41-reaction-hud line"
fi
if ! hud_bars_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s41-hud-bars ')"; then
  fail "pixel guards: no s41-hud-bars line"
fi

defeat_region_pixels="$(field "$defeat_region_line" pixels)"
defeat_region_span="$(field "$defeat_region_line" span)"
defeat_region_changed="$(field "$defeat_region_line" changed)"
defeat_region_pct="$(field "$defeat_region_line" changed_pct)"
defeat_region_outside="$(field "$defeat_region_line" outside)"
defeat_region_bbox="$(field "$defeat_region_line" bbox)"
[[ "$defeat_region_span" -ge 30 ]] ||
  fail "the defeat region is uniform (largest channel span $defeat_region_span < 30)"
region_min_changed=$((defeat_region_pixels / 20))  # 5 %
[[ "$defeat_region_changed" -ge "$region_min_changed" ]] ||
  fail "the defeat region is background: only $defeat_region_changed of $defeat_region_pixels pixels differ from the no-entity control (< 5 %)"
[[ "$defeat_region_outside" -eq 0 ]] ||
  fail "the defeat presence changed $defeat_region_outside pixels outside the s41-defeat bbox (+16 px)"

defeat_hud_changed="$(field "$defeat_hud_line" changed)"
defeat_hud_outside="$(field "$defeat_hud_line" outside)"
defeat_hud_stamina="$(field "$defeat_hud_line" stamina_changed)"
defeat_hud_health="$(field "$defeat_hud_line" health_changed)"
[[ "$defeat_hud_changed" -gt 0 ]] || fail "the defeat still has no HUD pixels"
[[ "$defeat_hud_outside" -eq 0 ]] ||
  fail "the HUD changed $defeat_hud_outside pixels outside the two HUD bands"
[[ "$defeat_hud_stamina" -gt 0 ]] ||
  fail "the defeat still's stamina bar is absent"
[[ "$defeat_hud_health" -eq 0 ]] ||
  fail "the defeat still draws a health bar at 0 HP ($defeat_hud_health changed pixels)"

reaction_hud_changed="$(field "$reaction_hud_line" changed)"
reaction_hud_outside="$(field "$reaction_hud_line" outside)"
reaction_hud_stamina="$(field "$reaction_hud_line" stamina_changed)"
reaction_hud_health="$(field "$reaction_hud_line" health_changed)"
[[ "$reaction_hud_changed" -gt 0 ]] || fail "the reaction still has no HUD pixels"
[[ "$reaction_hud_outside" -eq 0 ]] ||
  fail "the reaction HUD changed $reaction_hud_outside pixels outside the two HUD bands"
[[ "$reaction_hud_stamina" -gt 0 ]] ||
  fail "the reaction still's stamina bar is absent"
[[ "$reaction_hud_health" -gt 0 ]] ||
  fail "the reaction still's health bar is absent (66 HP must draw a bar)"

stamina_green="$(field "$hud_bars_line" stamina_green)"
health_red="$(field "$hud_bars_line" health_red)"
[[ "$stamina_green" -ge 500 ]] ||
  fail "the stamina bar has only $stamina_green green pixels (need >= 500)"
[[ "$health_red" -ge 500 ]] ||
  fail "the health bar has only $health_red red pixels (need >= 500)"

# --- Step 6: performance (load + marginal frame cost) -------------------------

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

# Both runs must succeed AND write their frame: the 240-frame timing only
# means something when 240 frames were actually rendered, and a load-only
# failure cannot masquerade as a plausible marginal cost.
perf_png="$control_dir/perf.png"
perf_log="$control_dir/perf-1.log"
perf_slow_log="$control_dir/perf-240.log"
rm -f "$perf_png"
if ! perf_ms="$(timed "$perf_log" "$viewer" --library "$lib" --manifest "$encounter_manifest" \
  --model "$player" --screenshot "$perf_png" --frames 1 --anim-time 0 --entity-time 0)"; then
  tail -20 "$perf_log" >&2
  fail "perf: the 1-frame run failed (log: $perf_log)"
fi
[[ -s "$perf_png" ]] || fail "perf: the 1-frame run wrote no capture"
rm -f "$perf_png"
if ! perf_slow_ms="$(timed "$perf_slow_log" "$viewer" --library "$lib" \
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
awk -v ms="$marginal_ms" 'BEGIN { exit !(ms < 16.667) }' ||
  fail "perf: marginal frame cost ${marginal_ms} ms is outside the 60 fps budget (16.667 ms)"
stamp "perf: load=${perf_ms}s frames240=${perf_slow_ms}s marginal=${marginal_ms}ms (~${marginal_fps} fps)"
keep pixels "$pixels_out"
}

run_section "$1"
