#!/usr/bin/env bash
# s4.sh — the runner's adapter for scripts/s4-acceptance.sh (see common.sh).
#   probes: the S3 manifest's check-manifest, compose-encounter into the runner's dir +
#           check-encounter + the 30/1/1 counts; the shared encounter.manifest must equal it.
#   gpu:    the selftest, the four stills, the no-entity control, the pixel guards (heredoc
#           extracted), the perf pair — every gate verbatim.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

s3_manifest="$repo_root/out/re/stage/valus.manifest"
shared_encounter="$repo_root/out/re/stage/encounter.manifest"
control_dir="$out_dir/control"

section_probes() {
[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter suite reads the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
[[ -f "$repo_root/tools/re/stage/probe.py" ]] || fail "composer not found: tools/re/stage/probe.py"
# The frozen script composes the S3 manifest when it is absent; the runner never writes the shared
# out/re/stage, so an absent one is a failure here.
[[ -f "$s3_manifest" ]] || fail "S3's arena manifest is absent: $s3_manifest (make accept-s4 composes it)"
encounter_manifest="$out_dir/encounter.manifest"
stamp "check-manifest: $s3_manifest"
checked check-manifest-s3 python3 "$repo_root/tools/re/stage/probe.py" check-manifest "$s3_manifest" --library "$lib"
s3_instances="$(grep -c '^instance ' "$s3_manifest" || true)"
[[ "$s3_instances" -gt 0 ]] || fail "S3 manifest has no instance lines: $s3_manifest"


stamp "compose-encounter: $s3_manifest -> $encounter_manifest"
checked compose-encounter python3 "$repo_root/tools/re/stage/probe.py" compose-encounter --library "$lib" \
  --s3-manifest "$s3_manifest" --out "$encounter_manifest"
stamp "check-encounter: $encounter_manifest"
checked check-encounter python3 "$repo_root/tools/re/stage/probe.py" check-encounter "$encounter_manifest" --library "$lib"
encounter_instances="$(grep -c '^instance ' "$encounter_manifest" || true)"
encounter_entities="$(grep -c '^entity ' "$encounter_manifest" || true)"
encounter_spawns="$(grep -c '^spawn ' "$encounter_manifest" || true)"
[[ "$encounter_instances" = "30" ]] ||
  fail "encounter manifest has $encounter_instances instances (need 30)"
[[ "$encounter_entities" = "1" ]] || fail "encounter manifest has $encounter_entities entities (need 1)"
[[ "$encounter_spawns" = "1" ]] || fail "encounter manifest has $encounter_spawns spawns (need 1)"

# Runner-only: the frozen s4 rewrites the shared encounter.manifest in place and every later script
# reads it; the runner composed into its own dir, so the shared one must equal that composition
# (the `# source:` worktree path normalised) for the GPU runs below to read what s4 would write.
encounter_norm="$(norm_source "$encounter_manifest")"
keep encounter-manifest "$encounter_norm"
[[ "$encounter_norm" = "$(norm_source "$shared_encounter")" ]] ||
  fail "the shared $shared_encounter differs from compose-encounter's output (re-run make accept-s4)"
}

section_gpu() {
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
encounter_manifest="$shared_encounter"
encounter_instances="$(grep -c '^instance ' "$encounter_manifest" || true)"

mkdir -p "$out_dir" "$control_dir"
find "$out_dir" -maxdepth 1 -name 's4-*.png' -delete 2>/dev/null || true
find "$control_dir" -name '*.png' -delete 2>/dev/null || true
approach_png="$out_dir/s4-approach.png"
rm -f "$approach_png"

stamp "s4-selftest: $encounter_manifest"
if ! selftest_out="$("$viewer" --library "$lib" --manifest "$encounter_manifest" \
  --model "$player" --s4-selftest --screenshot "$approach_png" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s4-selftest exited non-zero"
fi
printf '%s\n' "$selftest_out"

if ! status_line="$(printf '%s\n' "$selftest_out" |
  grep -m1 -E '^s4-status instances=[0-9]+ rigs=[0-9]+ entity=[^ ]+ placement=[^ ]+ spawn=[^ ]+ feet=(ok|none) entity_bbox=-?[0-9]+,-?[0-9]+,-?[0-9]+,-?[0-9]+ pos=-?[0-9.]+,-?[0-9.]+,-?[0-9.]+ frames=[0-9]+$')"; then
  fail "s4-selftest did not print the s4-status line"
fi

status_instances="${status_line#*instances=}"; status_instances="${status_instances%% *}"
status_rigs="${status_line#*rigs=}"; status_rigs="${status_rigs%% *}"
status_entity="${status_line#*entity=}"; status_entity="${status_entity%% *}"
status_placement="${status_line#*placement=}"; status_placement="${status_placement%% *}"
status_spawn="${status_line#*spawn=}"; status_spawn="${status_spawn%% *}"
status_feet="${status_line#*feet=}"; status_feet="${status_feet%% *}"
status_frames="${status_line#*frames=}"; status_frames="${status_frames%% *}"
bbox_csv="${status_line#*entity_bbox=}"; bbox_csv="${bbox_csv%% *}"
IFS=, read -r bbox_x0 bbox_y0 bbox_x1 bbox_y1 <<<"$bbox_csv"

[[ "$status_instances" = "$encounter_instances" ]] ||
  fail "s4-status reports $status_instances instances but the manifest has $encounter_instances"
[[ "$status_rigs" = "2" ]] || fail "s4-status reports rigs=$status_rigs (need the player + one entity)"
[[ "$status_entity" = "minotaur_a_stand" ]] || fail "s4-status entity=$status_entity"
[[ "$status_placement" = "builder-pick" ]] || fail "s4-status placement=$status_placement"
[[ "$status_spawn" = "data-position+builder-yaw" ]] || fail "s4-status spawn=$status_spawn"
[[ "$status_feet" = "ok" ]] || fail "s4-status feet=$status_feet (the entity is not on the ground)"
[[ "$status_frames" -gt 0 ]] || fail "s4-status frames=$status_frames"
if ((bbox_x1 <= bbox_x0 || bbox_y1 <= bbox_y0)); then
  fail "s4-status entity_bbox=$bbox_csv is empty"
fi

if ! spawn_line="$(printf '%s\n' "$selftest_out" | grep -m1 '^s4-spawn ')" ; then
  fail "s4-selftest printed no s4-spawn line"
fi
spawn_clearance="${spawn_line#*clearance=}"; spawn_clearance="${spawn_clearance%% *}"
if ! walk_line="$(printf '%s\n' "$selftest_out" | grep -m1 '^s4-walk ')" ; then
  fail "s4-selftest printed no s4-walk line"
fi
walk_stop="${walk_line#*stop=}"; walk_stop="${walk_stop%% *}"
walk_penetration="${walk_line#*penetration=}"; walk_penetration="${walk_penetration%% *}"
[[ "$walk_stop" = "yes" ]] || fail "s4-walk stop=$walk_stop (the collision did not stop the player)"
awk -v c="$spawn_clearance" 'BEGIN { exit !(c >= 0) }' ||
  fail "s4-spawn clearance=$spawn_clearance (the spawn is inside an entity capsule)"
if ! entity_line="$(printf '%s\n' "$selftest_out" | grep -m1 '^s4-entity ')" ; then
  fail "s4-selftest printed no s4-entity line"
fi
entity_delta="${entity_line#*delta=}"; entity_delta="${entity_delta%% *}"
awk -v d="$entity_delta" 'BEGIN { exit !(d <= 0.10 && d >= -0.10) }' ||
  fail "s4-entity delta=$entity_delta m (feet are not on the ground)"
awk -v p="$walk_penetration" 'BEGIN { exit !(p <= 0.02) }' ||
  fail "s4-walk penetration=$walk_penetration m (beyond the 2 cm bound)"

# The approach frame is the self-test's final frame; give it the still checks.
[[ -f "$approach_png" ]] || fail "the self-test wrote no approach still: $approach_png"
approach_magic="$(od -An -tx1 -N4 "$approach_png" | tr -d ' \n')"
[[ "$approach_magic" = "89504e47" ]] || fail "$approach_png lacks the PNG magic (got $approach_magic)"
approach_bytes="$(wc -c < "$approach_png" | tr -d '[:space:]')"
[[ "$approach_bytes" -gt 5000 ]] || fail "$approach_png is only $approach_bytes bytes"

# --- Step 6: the stills -------------------------------------------------------

# capture NAME [viewer flags...] — one deterministic frame; PNG magic, > 5000 B
# and the viewer's own non-uniformity span (a solid clear frame also passes the
# byte floor, so bytes alone cannot prove the draw loop ran).
capture() {
  local name="$1"
  shift
  local png magic bytes output pixels_line
  png="$out_dir/$name.png"
  rm -f "$png"
  stamp "capture: $name ($*)"
  output="$("$viewer" --library "$lib" --manifest "$encounter_manifest" \
    --model "$player" --screenshot "$png" --frames 1 --anim-time 0 "$@" 2>&1)" || {
    printf '%s\n' "$output" >&2
    fail "capture $name: viewer failed"
  }
  printf '%s\n' "$output" | grep -E '^s1-(scene|pixels|screenshot) ' || true
  [[ -f "$png" ]] || fail "capture $name: $png was not written"
  magic="$(od -An -tx1 -N4 "$png" | tr -d ' \n')"
  [[ "$magic" = "89504e47" ]] || fail "capture $name: $png lacks the PNG magic (got $magic)"
  bytes="$(wc -c < "$png" | tr -d '[:space:]')"
  [[ "$bytes" -gt 5000 ]] || fail "capture $name: $png is only $bytes bytes (need > 5000)"
  pixels_line="$(printf '%s\n' "$output" | grep -m1 '^s1-pixels ')" ||
    fail "capture $name: viewer printed no s1-pixels line"
  min_csv="${pixels_line#*min=}"; min_csv="${min_csv%% *}"
  max_csv="${pixels_line#*max=}"; max_csv="${max_csv%% *}"
  local min_r min_g min_b max_r max_g max_b d largest
  IFS=, read -r min_r min_g min_b <<<"$min_csv"
  IFS=, read -r max_r max_g max_b <<<"$max_csv"
  largest=$((max_r - min_r))
  d=$((max_g - min_g)); if ((d > largest)); then largest=$d; fi
  d=$((max_b - min_b)); if ((d > largest)); then largest=$d; fi
  [[ "$largest" -ge 30 ]] ||
    fail "capture $name: uniform frame (largest channel span $largest < 30): $pixels_line"
}

# The wide spawn view (the same camera the idle pair uses) and the portrait.
capture s4-wide --camera-target player --entity-time 0
capture s4-portrait --camera-target entity --entity-time 0

# The idle pair: the spawn-follow camera does not depend on the entity's pose,
# so the two frames differ only where Valus animates.
capture s4-idle-t0 --camera-target player --entity-time 0
capture s4-idle-t08 --camera-target player --entity-time 0.8

# The presence control: the same manifest without its entity line, the same
# camera. Its diff against s4-wide is the proof the entity region is not
# background (and that the bbox is the entity's screen region).
noentity_manifest="$control_dir/s4-noentity.manifest"
grep -v '^entity ' "$encounter_manifest" > "$noentity_manifest"
[[ "$(grep -c '^instance ' "$noentity_manifest" || true)" = "30" ]] ||
  fail "the no-entity control manifest lost instances"
noentity_png="$control_dir/s4-noentity.png"
rm -f "$noentity_png"
stamp "capture: s4-noentity (presence control, same camera)"
if ! noentity_out="$("$viewer" --library "$lib" --manifest "$noentity_manifest" --model "$player" \
  --screenshot "$noentity_png" --frames 1 --anim-time 0 2>&1)"; then
  printf '%s\n' "$noentity_out" >&2
  fail "the no-entity control render failed"
fi
printf '%s\n' "$noentity_out" | grep -E '^s1-(scene|pixels|screenshot) ' || true
[[ -f "$noentity_png" ]] || fail "the no-entity control wrote no PNG"

# --- Step 7: pixel checks -----------------------------------------------------

# Stdlib-only Python (PNG decode + two diffs, ~30 s): the checks need the
# differing pixels' count and extent, which od/wc cannot give. It prints machine
# lines; the assertions below are the guard.
pixel_py="$(heredoc_py s4 1)"
if ! pixels_out="$(python3 "$pixel_py" "$out_dir/s4-wide.png" "$noentity_png" \
  "$out_dir/s4-idle-t0.png" "$out_dir/s4-idle-t08.png" "$bbox_csv" 16 2>&1)"; then
  printf '%s\n' "$pixels_out" >&2
  fail "the pixel checks failed"
fi
printf '%s\n' "$pixels_out"

if ! region_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s4-region ')"; then
  fail "pixel checks: no s4-region line"
fi
if ! idle_line="$(printf '%s\n' "$pixels_out" | grep -m1 '^s4-idle-pixels ')"; then
  fail "pixel checks: no s4-idle-pixels line"
fi
region_pixels="${region_line#*pixels=}"; region_pixels="${region_pixels%% *}"
region_span="${region_line#*span=}"; region_span="${region_span%% *}"
region_changed="${region_line#*changed=}"; region_changed="${region_changed%% *}"
region_changed_pct="${region_line#*changed_pct=}"; region_changed_pct="${region_changed_pct%% *}"
region_outside="${region_line#*outside=}"; region_outside="${region_outside%% *}"
idle_differing="${idle_line#*differing=}"; idle_differing="${idle_differing%% *}"
idle_total="${idle_line#*total=}"; idle_total="${idle_total%% *}"
idle_pct="${idle_line#*pct=}"; idle_pct="${idle_pct%% *}"
idle_outside="${idle_line#*outside=}"; idle_outside="${idle_outside%% *}"

[[ "$region_span" -ge 30 ]] ||
  fail "entity region is uniform (largest channel span $region_span < 30)"
region_min_changed=$((region_pixels / 20))  # 5 %: measured 66.2 %
[[ "$region_changed" -ge "$region_min_changed" ]] ||
  fail "entity region is background: only $region_changed of $region_pixels pixels differ from the no-entity control (< 5 %)"
[[ "$region_outside" -eq 0 ]] ||
  fail "the entity presence changed $region_outside pixels outside its screen region (+16 px)"
[[ "$idle_differing" -gt 0 ]] ||
  fail "the idle pair rendered identical frames — Valus's pose did not advance"
idle_max=$((idle_total / 10))  # 10 %: measured 3.595 %
[[ "$idle_differing" -lt "$idle_max" ]] ||
  fail "the idle pair differs in $idle_differing of $idle_total pixels (>= 10 %: the world or camera moved with the entity)"
[[ "$idle_outside" -eq 0 ]] ||
  fail "the idle pair changed $idle_outside pixels outside the entity region (+16 px): the player/world must not move"

# --- Step 8: performance (load + marginal frame cost) -------------------------

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
perf_png="$control_dir/s4-perf.png"
perf_log="$control_dir/s4-perf-1.log"
perf_slow_log="$control_dir/s4-perf-240.log"
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
