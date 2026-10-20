#!/usr/bin/env bash
#
# s4-acceptance.sh — the S4.0 "The encounter" acceptance pass, re-runnable end
# to end.
#
# Steps (the milestone: Valus stands in the arena, placed with recorded
# provenance, approachable — the collision proxy stops Wander at his legs — and
# framable, with a dedicated encounter manifest and acceptance):
#
#   1. build the converter solution and app/ (viewer, tests, mesh.metal copied);
#   2. both suites — `dotnet test tests/BpConvert.Tests` and
#      `ctest --test-dir app/build --output-on-failure`;
#   3. ensure S3's arena manifest (out/re/stage/valus.manifest; composed when
#      absent) and verify it with `check-manifest` — the encounter derives from
#      it, so its 30-instance pin is re-checked first;
#   4. compose the encounter manifest from that S3 manifest
#      (`compose-encounter`) and gate it with `check-encounter` (30 instances,
#      1 entity, 1 spawn, the provenance header, the bowl/ground/yaw checks);
#   5. the encounter self-test — `--s4-selftest` loads the encounter manifest +
#      the wander player, pins the entity at t=0, asserts the spawn is outside
#      every entity capsule, measures the entity's lowest posed vertex against
#      the ground (feet=ok), walks into the nearest leg capsule until the
#      collision stops the player (bounded penetration, still grounded), renders
#      the final frame (saved as s4-approach.png), and prints the `s4-status`
#      line the rest of this script parses;
#   6. stills under out/re/capture/s4/ (stale PNGs deleted first): the wide
#      spawn view (`--camera-target player`), the approach frame above, the
#      portrait (`--camera-target entity`, full body), and the idle pair
#      (`--entity-time 0` vs `0.8`, same spawn-follow camera);
#   7. pixel checks (stdlib Python, ~20 s): every still has the PNG magic and
#      > 5000 bytes; the entity region is non-background (proved against a
#      no-entity control render of the same camera: the region's pixels differ
#      where Valus is, and nowhere else); the idle pair differs > 0 and only
#      inside the `s4-status` entity_bbox (+16 px for the pose's travel);
#   8. measured load + marginal frame cost (`--frames 1` vs `--frames 240`,
#      the S3 pattern): the marginal cost must stay inside the 60 fps budget;
#   9. the S3 regression: `scripts/s3-acceptance.sh` runs unchanged and must
#      stay green (S3's manifest, script and walk remain valid);
#  10. a summary block (library, both manifests + sha256, the status line, the
#      stills + bytes, the region/idle numbers, the perf numbers).
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (the converter suite reads it;
#                   required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library)
#
# A capture file is removed before its run, so a stale PNG can never mask a
# failed write. The interactive guarantees (cursor free, drag-look, the T
# toggle, the title-bar fps readout) are the user's sign-off walk, whose
# checklist Task 7 records in docs/notes/s4-results.md.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
s3_manifest="$repo_root/out/re/stage/valus.manifest"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
player="models/shadow/characters/wander/_cmn/wander.glb"
out_dir="$repo_root/out/re/capture/s4"
control_dir="$out_dir/control"
run_log="$repo_root/out/s4-acceptance-run.log"
s3_regression_log="$repo_root/out/s3-acceptance-regression.log"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s4-acceptance: $*" >&2; exit 1; }

# Every line this script prints lands in the recorded run log as well.
mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set — the converter suite reads the game tree"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
[[ -f "$repo_root/tools/re/stage/probe.py" ]] || fail "composer not found: tools/re/stage/probe.py"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"

# --- Step 1: builds -----------------------------------------------------------

stamp "build: BpConvert.sln"
dotnet build "$repo_root/BpConvert.sln" -v q

stamp "build: app -> app/build"
cmake -S "$repo_root/app" -B "$build_dir"
cmake --build "$build_dir" -j
[[ -x "$viewer" ]] || fail "build did not produce $viewer"

# --- Step 2: both suites ------------------------------------------------------

stamp "dotnet test: tests/BpConvert.Tests"
dotnet test "$repo_root/tests/BpConvert.Tests" -v q

stamp "ctest: $build_dir"
ctest --test-dir "$build_dir" --output-on-failure

# --- Step 3: S3's arena manifest (the encounter's single instance source) -----

if [[ ! -f "$s3_manifest" ]]; then
  stamp "compose: S3 arena manifest (absent) -> $s3_manifest"
  python3 "$repo_root/tools/re/stage/probe.py" compose --library "$lib" --out "$s3_manifest"
fi
stamp "check-manifest: $s3_manifest"
python3 "$repo_root/tools/re/stage/probe.py" check-manifest "$s3_manifest" --library "$lib"
s3_instances="$(grep -c '^instance ' "$s3_manifest" || true)"
[[ "$s3_instances" -gt 0 ]] || fail "S3 manifest has no instance lines: $s3_manifest"

# --- Step 4: the encounter manifest ------------------------------------------

stamp "compose-encounter: $s3_manifest -> $encounter_manifest"
python3 "$repo_root/tools/re/stage/probe.py" compose-encounter --library "$lib" \
  --s3-manifest "$s3_manifest" --out "$encounter_manifest"
stamp "check-encounter: $encounter_manifest"
python3 "$repo_root/tools/re/stage/probe.py" check-encounter "$encounter_manifest" --library "$lib"
encounter_instances="$(grep -c '^instance ' "$encounter_manifest" || true)"
encounter_entities="$(grep -c '^entity ' "$encounter_manifest" || true)"
encounter_spawns="$(grep -c '^spawn ' "$encounter_manifest" || true)"
[[ "$encounter_instances" = "30" ]] ||
  fail "encounter manifest has $encounter_instances instances (need 30)"
[[ "$encounter_entities" = "1" ]] || fail "encounter manifest has $encounter_entities entities (need 1)"
[[ "$encounter_spawns" = "1" ]] || fail "encounter manifest has $encounter_spawns spawns (need 1)"

# --- Step 5: the encounter self-test + the approach still ---------------------

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
pixel_py="$control_dir/s4-pixels.py"
cat > "$pixel_py" <<'PY'
import struct
import sys
import zlib


def read_png(path):
    """Decode an 8-bit RGBA non-interlaced PNG (stb_image_write's output shape)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    pos = 8
    idat = b""
    width = height = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color != 6 or interlace != 0:
                raise SystemExit(f"{path}: expected 8-bit RGBA, non-interlaced")
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray(height * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filter_type == 1:  # Sub
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, out


def diff(first, second, box, margin):
    width, height, a = first
    b = second[2]
    if first[:2] != second[:2]:
        raise SystemExit(f"size mismatch: {first[:2]} vs {second[:2]}")
    x0, y0, x1, y1 = box
    bx0, by0 = max(x0 - margin, 0), max(y0 - margin, 0)
    bx1, by1 = min(x1 + margin, width - 1), min(y1 + margin, height - 1)
    count = outside = 0
    min_x = min_y = 1 << 30
    max_x = max_y = -1
    for y in range(height):
        row = y * width * 4
        for x in range(width):
            i = row + x * 4
            if a[i:i + 4] != b[i:i + 4]:
                count += 1
                if not (bx0 <= x <= bx1 and by0 <= y <= by1):
                    outside += 1
                if x < min_x: min_x = x
                if x > max_x: max_x = x
                if y < min_y: min_y = y
                if y > max_y: max_y = y
    if count == 0:
        min_x = min_y = max_x = max_y = -1
    return count, outside, (min_x, min_y, max_x, max_y)


wide = read_png(sys.argv[1])
noentity = read_png(sys.argv[2])
idle0 = read_png(sys.argv[3])
idle8 = read_png(sys.argv[4])
box = tuple(int(part) for part in sys.argv[5].split(","))
margin = int(sys.argv[6])

# The entity region's own content: span (is anything drawn there?) and the
# pixels the entity contributes against the no-entity control.
width, height, pixels = wide
x0, y0, x1, y1 = box
span = [0, 0, 0]
low = [255, 255, 255]
high = [0, 0, 0]
region_pixels = 0
for y in range(y0, y1 + 1):
    row = y * width * 4
    for x in range(x0, x1 + 1):
        i = row + x * 4
        region_pixels += 1
        for c in range(3):
            value = pixels[i + c]
            if value < low[c]: low[c] = value
            if value > high[c]: high[c] = value
for c in range(3):
    span[c] = high[c] - low[c]
largest_span = max(span)

presence, presence_outside, presence_bbox = diff(wide, noentity, box, margin)
idle, idle_outside, idle_bbox = diff(idle0, idle8, box, margin)
total = width * height

print(f"s4-region pixels={region_pixels} span={largest_span} "
      f"changed={presence} changed_pct={100.0 * presence / region_pixels:.3f} "
      f"outside={presence_outside} bbox={presence_bbox[0]},{presence_bbox[1]},"
      f"{presence_bbox[2]},{presence_bbox[3]}")
print(f"s4-idle-pixels differing={idle} total={total} "
      f"pct={100.0 * idle / total:.3f} outside={idle_outside} margin={margin} "
      f"bbox={idle_bbox[0]},{idle_bbox[1]},{idle_bbox[2]},{idle_bbox[3]}")
PY
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

# --- Step 9: the S3 regression (must stay green, unchanged) -------------------

stamp "s3 regression: scripts/s3-acceptance.sh"
if ! SOTC_LIB="$lib" "$repo_root/scripts/s3-acceptance.sh" > "$s3_regression_log" 2>&1; then
  tail -40 "$s3_regression_log" >&2
  fail "the S3 acceptance regression failed (log: $s3_regression_log)"
fi
grep -E '^s3-acceptance: (library|manifest|status|capture|wave|PASS) ' "$s3_regression_log" || true
s3_status="$(grep -m1 '^s3-status ' "$s3_regression_log")" ||
  fail "the S3 regression printed no s3-status line"
grep -q '^s3-acceptance: PASS' "$s3_regression_log" ||
  fail "the S3 regression did not print its PASS line"

# --- Step 10: summary ---------------------------------------------------------

stamp "summary"
printf 's4-acceptance: library      %s\n' "$lib"
printf 's4-acceptance: s3 manifest  %s\n' "$s3_manifest"
printf 's4-acceptance: s3 sha256    %s instances %s\n' \
  "$(shasum -a 256 "$s3_manifest" | cut -d' ' -f1)" "$s3_instances"
printf 's4-acceptance: encounter    %s\n' "$encounter_manifest"
printf 's4-acceptance: enc sha256   %s instances %s entity %s spawn %s\n' \
  "$(shasum -a 256 "$encounter_manifest" | cut -d' ' -f1)" "$encounter_instances" \
  "$encounter_entities" "$encounter_spawns"
printf 's4-acceptance: status       %s\n' "$status_line"
printf 's4-acceptance: spawn guard  %s\n' "$spawn_line"
printf 's4-acceptance: feet         lowest posed vertex vs ground: delta=%s m\n' "$entity_delta"
printf 's4-acceptance: collision    %s\n' "$walk_line"
printf 's4-acceptance: stills       %s (%s B), %s (%s B), %s (%s B), %s (%s B), %s (%s B)\n' \
  "$out_dir/s4-wide.png" "$(wc -c < "$out_dir/s4-wide.png" | tr -d '[:space:]')" \
  "$approach_png" "$approach_bytes" \
  "$out_dir/s4-portrait.png" "$(wc -c < "$out_dir/s4-portrait.png" | tr -d '[:space:]')" \
  "$out_dir/s4-idle-t0.png" "$(wc -c < "$out_dir/s4-idle-t0.png" | tr -d '[:space:]')" \
  "$out_dir/s4-idle-t08.png" "$(wc -c < "$out_dir/s4-idle-t08.png" | tr -d '[:space:]')"
printf 's4-acceptance: entity region %s\n' "$region_line"
printf 's4-acceptance: idle pair    %s\n' "$idle_line"
printf 's4-acceptance: perf         load %.3f s, 240 frames %.3f s, marginal %s ms (~%s fps)\n' \
  "$perf_ms" "$perf_slow_ms" "$marginal_ms" "$marginal_fps"
printf 's4-acceptance: s3 status    %s\n' "$s3_status"
printf 's4-acceptance: PASS — build, both suites, 30+1 encounter manifest (gate MET), s4-status (feet=ok, bbox %s), spawn outside all capsules (%.3f m), collision stop at %s m before the leg (penetration %s), captures %s B + idle pair %s (%s %% inside the entity region), marginal %s ms/frame, S3 regression green\n' \
  "$bbox_csv" "$spawn_clearance" "$(printf '%s\n' "$walk_line" | sed -n 's/.*stop_before=\([0-9.-]*\).*/\1/p')" \
  "$walk_penetration" "$approach_bytes" "$idle_differing" "$idle_pct" "$marginal_ms"
