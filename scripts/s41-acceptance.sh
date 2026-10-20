#!/usr/bin/env bash
#
# s41-acceptance.sh — the S4.1 "First colossus" acceptance pass, re-runnable
# end to end.
#
# Steps (the milestone: grab Valus's fur, climb him under stamina, reach the
# data-derived forehead weakpoint, strike it, survive the shake-off, and bring
# him down):
#
#   1. build the converter solution and app/ (viewer, tests, shaders copied);
#   2. both suites — `dotnet test tests/BpConvert.Tests` and
#      `ctest --test-dir app/build --output-on-failure`;
#   3. the deterministic fight timeline — `--s41-selftest` against the S4.0
#      encounter manifest: the S4 approach, a real grab, a 240-frame climb with
#      a proven rise, a full-charge head strike, a catch, a deliberately missed
#      catch, a forced stamina fall, the killing blow, and the post-kill settle
#      to a landed player. Every `s41-*` line is parsed field by field;
#   4. five stills under out/s41/ (stale PNGs deleted first): grab, mid-climb,
#      strike, reaction, defeat — each with PNG magic, > 5000 B and the
#      viewer's own non-uniformity span;
#   5. the pixel guards (stdlib Python): the defeat still against a no-entity
#      control (presence + `s41-defeat` bbox localization), the HUD against
#      no-HUD controls (both bars present; the zero-health bar is absent), and
#      the bar colours at the grab/strike moments;
#   6. measured load + marginal frame cost (`--frames 1` vs `--frames 240`,
#      the S3/S4 pattern): the marginal cost must stay inside the 60 fps budget;
#   7. the S4 regression: `scripts/s4-acceptance.sh` runs unchanged (it already
#      runs `scripts/s3-acceptance.sh`; S3 is not nested a second time);
#   8. a summary block (library, manifest + clip sha256s, every event/status
#      line, the stills + bytes, the pixel numbers, the perf numbers).
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (the converter suite reads it;
#                   required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library)
#
# A capture file is removed before its run, so a stale PNG can never mask a
# failed write. The interactive guarantees (cursor free, drag-look, the T
# toggle, the title-bar fps readout, HUD legibility, gamepad) are the user's
# sign-off walk, whose checklist Task 7 records in docs/notes/s41-results.md.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
encounter_manifest="$repo_root/out/re/stage/encounter.manifest"
player="models/shadow/characters/wander/_cmn/wander.glb"
wander_glb="$lib/$player"
valus_glb="$lib/models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
out_dir="$repo_root/out/s41"
control_dir="$out_dir/control"
run_log="$repo_root/out/s41-acceptance-run.log"
s4_regression_log="$repo_root/out/s4-acceptance-regression.log"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s41-acceptance: $*" >&2; exit 1; }

# Every line this script prints lands in the recorded run log as well.
mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

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
climb_rise="$(awk -v a="$climb_from_y" -v b="$climb_to_y" 'BEGIN { printf "%.2f", b - a }')"
awk -v r="$climb_rise" 'BEGIN { exit !(r >= 3.0) }' ||
  fail "s41-climb rise=${climb_rise} m (need >= 3.0)"
awk -v a="$climb_from_y" -v b="$climb_to_y" 'BEGIN { exit !(b > a) }' ||
  fail "s41-climb went backwards ($climb_from_y -> $climb_to_y)"

strike_re='^s41-strike swing=[0-9]+ damage=[0-9]+ health=[0-9]+->[0-9]+ weakpoint=-?[0-9]+$'
strike_count="$(line_count "$strike_re")"
[[ "$strike_count" = "2" ]] || fail "expected two s41-strike lines (full + weak), found $strike_count"
strike1="$(printf '%s\n' "$selftest_out" | grep -m1 -E "$strike_re")" ||
  fail "s41-strike: missing the full-charge hit"
strike2="$(printf '%s\n' "$selftest_out" | grep -E "$strike_re" | sed -n '2p')" ||
  fail "s41-strike: missing the weak hit"
strike1_swing="$(field "$strike1" swing)"
strike1_damage="$(field "$strike1" damage)"
strike1_health="$(field "$strike1" health)"
strike1_weakpoint="$(field "$strike1" weakpoint)"
strike2_swing="$(field "$strike2" swing)"
strike2_damage="$(field "$strike2" damage)"
strike2_health="$(field "$strike2" health)"
[[ "$strike1_swing" = "1" ]] || fail "s41-strike swing=$strike1_swing (need 1)"
[[ "$strike1_damage" = "34" ]] || fail "the full-charge strike dealt $strike1_damage (need 34)"
[[ "$strike1_health" = "100->66" ]] || fail "the full-charge strike health is $strike1_health (need 100->66)"
[[ "$strike1_weakpoint" = "0" ]] || fail "the full-charge strike weakpoint=$strike1_weakpoint (need the forehead)"
[[ "$strike2_swing" = "2" ]] || fail "s41-strike swing=$strike2_swing (need 2)"
[[ "$strike2_damage" = "20" ]] || fail "the weak strike dealt $strike2_damage (need 20)"
[[ "$strike2_health" = "66->46" ]] || fail "the weak strike health is $strike2_health (need 66->46)"
h1_before="${strike1_health%%->*}"
h1_after="${strike1_health##*->}"
h2_before="${strike2_health%%->*}"
h2_after="${strike2_health##*->}"
[[ $((h1_before - h1_after)) -eq "$strike1_damage" ]] ||
  fail "the full strike's health arithmetic does not match its damage: $strike1_health vs $strike1_damage"
[[ $((h2_before - h2_after)) -eq "$strike2_damage" ]] ||
  fail "the weak strike's health arithmetic does not match its damage: $strike2_health vs $strike2_damage"
[[ "$h2_before" -eq "$h1_after" ]] ||
  fail "the weak strike starts at $h2_before HP, not the full strike's $h1_after"

if ! catch_line="$(one_line "^s41-catch band=($band_re)$" "s41-catch")"; then
  exit 1
fi
catch_band="$(field "$catch_line" band)"
[[ "$catch_band" = "neck" ]] ||
  fail "s41-catch band=$catch_band (the recorded catch lands on the neck band)"

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

# check_png PHASE PATH — magic, byte floor, and the viewer's own span line.
check_png() {
  local phase="$1" path="$2" magic bytes pixels_line min_csv max_csv
  local min_r min_g min_b max_r max_g max_b d largest
  [[ -f "$path" ]] || fail "$phase: no PNG at $path"
  magic="$(od -An -tx1 -N4 "$path" | tr -d ' \n')"
  [[ "$magic" = "89504e47" ]] || fail "$phase: $path lacks the PNG magic (got $magic)"
  bytes="$(wc -c < "$path" | tr -d '[:space:]')"
  [[ "$bytes" -gt 5000 ]] || fail "$phase: $path is only $bytes bytes (need > 5000)"
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

pixel_py="$control_dir/s41-pixels.py"
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


def inside(x, y, box):
    return box[0] <= x <= box[2] and box[1] <= y <= box[3]


def diff(first, second, boxes, margin):
    """Count differing pixels, how many fall outside every box, per-box counts."""
    width, height, a = first
    b = second[2]
    if first[:2] != second[:2]:
        raise SystemExit(f"size mismatch: {first[:2]} vs {second[:2]}")
    count = outside = 0
    per_box = [0] * len(boxes)
    min_x = min_y = 1 << 30
    max_x = max_y = -1
    for y in range(height):
        row = y * width * 4
        for x in range(width):
            i = row + x * 4
            if a[i:i + 4] != b[i:i + 4]:
                count += 1
                hit = False
                for k, box in enumerate(boxes):
                    if inside(x, y, (box[0] - margin, box[1] - margin,
                                     box[2] + margin, box[3] + margin)):
                        hit = True
                        per_box[k] += 1
                if not hit:
                    outside += 1
                if x < min_x: min_x = x
                if x > max_x: max_x = x
                if y < min_y: min_y = y
                if y > max_y: max_y = y
    if count == 0:
        min_x = min_y = max_x = max_y = -1
    return count, outside, per_box, (min_x, min_y, max_x, max_y)


def span(img, box):
    width, _, pixels = img
    low = [255, 255, 255]
    high = [0, 0, 0]
    pixels_in = 0
    for y in range(box[1], box[3] + 1):
        row = y * width * 4
        for x in range(box[0], box[2] + 1):
            i = row + x * 4
            pixels_in += 1
            for c in range(3):
                v = pixels[i + c]
                if v < low[c]: low[c] = v
                if v > high[c]: high[c] = v
    return pixels_in, max(high[c] - low[c] for c in range(3))


def count_color(img, box, predicate):
    width, _, pixels = img
    count = 0
    for y in range(box[1], box[3] + 1):
        row = y * width * 4
        for x in range(box[0], box[2] + 1):
            i = row + x * 4
            if predicate(pixels[i], pixels[i + 1], pixels[i + 2]):
                count += 1
    return count


def design_box(img, x0, y0, x1, y1):
    """A 1280x720 design-space rectangle in this image's pixel coordinates."""
    width = img[0]
    scale = width / 1280.0
    return (int(x0 * scale), int(y0 * scale), int(x1 * scale), int(y1 * scale))


def box_csv(box):
    return f"{box[0]},{box[1]},{box[2]},{box[3]}"


def main():
    defeat = read_png(sys.argv[1])
    defeat_noentity = read_png(sys.argv[2])
    defeat_nohud = read_png(sys.argv[3])
    reaction = read_png(sys.argv[4])
    reaction_nohud = read_png(sys.argv[5])
    grab = read_png(sys.argv[6])
    strike = read_png(sys.argv[7])
    bbox = tuple(int(part) for part in sys.argv[8].split(","))
    margin = int(sys.argv[9])

    # The entity's screen region in the defeat still: the no-entity control
    # proves presence (differing pixels) and the bbox localizes every change.
    region_pixels, region_span = span(defeat, bbox)
    changed, outside, per_box, changed_bbox = diff(
        defeat, defeat_noentity, [bbox], margin)
    region_pct = 100.0 * changed / region_pixels
    print(f"s41-defeat-region pixels={region_pixels} span={region_span} "
          f"changed={changed} changed_pct={region_pct:.3f} outside={outside} "
          f"bbox={box_csv(changed_bbox)}")

    # The HUD bands, from the hud.cpp layout: the 600x8 health bar centred at
    # the top edge and the 240x12 stamina bar 24 px in from the bottom-left,
    # plus room for the STAMINA label above it.
    scale_health = design_box(defeat, 324, 16, 956, 40)
    scale_stamina = design_box(defeat, 8, 644, 280, 704)
    hud_boxes = [scale_health, scale_stamina]
    hud_changed, hud_outside, hud_per, _ = diff(defeat, defeat_nohud, hud_boxes, 0)
    print(f"s41-defeat-hud changed={hud_changed} outside={hud_outside} "
          f"stamina_changed={hud_per[1]} health_changed={hud_per[0]}")

    hud_changed, hud_outside, hud_per, _ = diff(reaction, reaction_nohud, hud_boxes, 0)
    print(f"s41-reaction-hud changed={hud_changed} outside={hud_outside} "
          f"stamina_changed={hud_per[1]} health_changed={hud_per[0]}")

    # The bars' own colours at the grab (stamina 100) and strike (health 100)
    # moments: green in the bottom-left bar, red in the top bar.
    stamina_bar = design_box(grab, 24, 684, 264, 696)
    health_bar = design_box(strike, 340, 24, 940, 32)
    stamina_green = count_color(grab, stamina_bar,
                                lambda r, g, b: g > r + 30 and g > b + 30 and g > 120)
    health_red = count_color(strike, health_bar,
                             lambda r, g, b: r > g + 50 and r > b + 50 and r > 120)
    print(f"s41-hud-bars stamina_green={stamina_green} health_red={health_red} "
          f"stamina_pixels={(stamina_bar[2] - stamina_bar[0] + 1) * (stamina_bar[3] - stamina_bar[1] + 1)} "
          f"health_pixels={(health_bar[2] - health_bar[0] + 1) * (health_bar[3] - health_bar[1] + 1)}")


if __name__ == "__main__":
    main()
PY

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

# --- Step 7: the S4 regression (which runs S3 unchanged) ----------------------

stamp "s4 regression: scripts/s4-acceptance.sh (runs S3 itself)"
if ! SOTC_LIB="$lib" SOTC_GAME_ROOT="$game_root" \
  "$repo_root/scripts/s4-acceptance.sh" > "$s4_regression_log" 2>&1; then
  tail -40 "$s4_regression_log" >&2
  fail "the S4 acceptance regression failed (log: $s4_regression_log)"
fi
grep -E '^s4-acceptance: (library|status|PASS) ' "$s4_regression_log" || true
s4_status="$(grep -m1 '^s4-status ' "$s4_regression_log")" ||
  fail "the S4 regression printed no s4-status line"
grep -q '^s4-acceptance: PASS' "$s4_regression_log" ||
  fail "the S4 regression did not print its PASS line"
# The S4 script captures S3's log and prints its lines prefixed; take the
# s3-status line from that captured summary (the S4 log has no raw s3-status).
s3_status="$(grep -m1 '^s3-acceptance: status ' "$s4_regression_log" |
  sed 's/^s3-acceptance: status *//')" ||
  fail "the S4 regression printed no s3-status line"
[[ -n "$s3_status" ]] || fail "the S4 regression printed an empty s3-status line"
grep -q '^s3-acceptance: PASS' "$s4_regression_log" ||
  fail "the S4 regression did not print the S3 PASS line"

# --- Step 8: summary ----------------------------------------------------------

stamp "summary"
printf 's41-acceptance: library      %s\n' "$lib"
printf 's41-acceptance: manifest     %s\n' "$encounter_manifest"
printf 's41-acceptance: manifest sha %s\n' "$(shasum -a 256 "$encounter_manifest" | cut -d' ' -f1)"
printf 's41-acceptance: wander       %s sha256 %s\n' "$wander_glb" \
  "$(shasum -a 256 "$wander_glb" | cut -d' ' -f1)"
printf 's41-acceptance: valus        %s sha256 %s\n' "$valus_glb" \
  "$(shasum -a 256 "$valus_glb" | cut -d' ' -f1)"
printf 's41-acceptance: grab         %s\n' "$grab_line"
printf 's41-acceptance: climb        %s\n' "$climb_line"
printf 's41-acceptance: strike       %s\n' "$strike1"
printf 's41-acceptance: strike       %s\n' "$strike2"
printf 's41-acceptance: catch        %s\n' "$catch_line"
printf 's41-acceptance: fall         %s\n' "$fall_line"
printf 's41-acceptance: stamina      %s\n' "$stamina_line"
printf 's41-acceptance: death        %s\n' "$death_line"
printf 's41-acceptance: settle       %s\n' "$settle_line"
printf 's41-acceptance: defeat       %s\n' "$defeat_line"
printf 's41-acceptance: status       %s\n' "$status_line"
printf 's41-acceptance: stills       %s\n' "$stills"
printf 's41-acceptance: defeat region %s\n' "$defeat_region_line"
printf 's41-acceptance: defeat hud   %s\n' "$defeat_hud_line"
printf 's41-acceptance: reaction hud %s\n' "$reaction_hud_line"
printf 's41-acceptance: hud bars     %s\n' "$hud_bars_line"
printf 's41-acceptance: perf         load %.3f s, 240 frames %.3f s, marginal %s ms (~%s fps)\n' \
  "$perf_ms" "$perf_slow_ms" "$marginal_ms" "$marginal_fps"
printf 's41-acceptance: s4 status    %s\n' "$s4_status"
printf 's41-acceptance: s3 status    %s\n' "$s3_status"
printf 's41-acceptance: PASS — build, both suites, s41 timeline (grab %s, climb %s m, strikes 34+20 damage health 100->66->46, catch %s, fall %s frames landed, stamina fall once, death %s, settled grounded in %s frames), five stills (span >= 30, > 5000 B), defeat presence %s %% inside bbox %s (outside %s), HUD bars present (stamina %s green px, health %s red px; 0-HP bar absent), marginal %s ms/frame, S4+S3 regressions green\n' \
  "$grab_band" "$climb_rise" "$catch_band" "$fall_frames" "$death_clip" \
  "$settle_frames" "$defeat_region_pct" "$defeat_region_bbox" "$defeat_region_outside" \
  "$stamina_green" "$health_red" "$marginal_ms"
