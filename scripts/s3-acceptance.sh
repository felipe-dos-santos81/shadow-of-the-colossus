#!/usr/bin/env bash
#
# s3-acceptance.sh — the S3 acceptance pass for the arena, re-runnable end to end.
#
# Steps (S3 milestone: "the Valus arena composed from real game data, explorable —
# you steer Wander, game-style third-person camera, ≥60 fps class, notes +
# screenshots"):
#
#   1. configure + build app/ (viewer, tests, and mesh.metal copied next to the binary);
#   2. run the suite — `ctest --test-dir app/build --output-on-failure`;
#   3. the W1.4 self-test against **W2's real manifest** (out/re/stage/valus.manifest,
#      the RE-3 deliverable): compose the arena from its instances, walk 240 fixed
#      1/60 s frames of forward+run from the manifest's spawn, and require
#      `s3-status instances=<n> ... ground=ok` — with <n> equal to the manifest's own
#      instance count (a parse that drops or invents an instance must not pass);
#   4. a spawn screenshot — the manifest's spawn, the camera snapped behind the
#      player, playback pinned with `--anim-time 0` (byte-reproducible) — carrying
#      the s1 PNG assertions: \x89PNG magic, > 5000 bytes, largest channel span
#      >= 30 in the viewer's own s1-pixels stats (a solid clear-colour frame also
#      passes the byte floor, so bytes alone cannot prove the draw loop ran);
#   5. the F3 wave guard (W1.4 review Q2): the shader's unskinned pass-through is
#      invisible to the unit tests, so the composed scene is rendered twice with the
#      wave pinned at t=0 and t=1 s and the two PNGs must differ only inside the
#      player's screen region. Measured (2026-10-18, real manifest): correct
#      0.36 % of the frame, player-localized; a pre-fix-simulated build (unskinned
#      vertices selecting palette[0]) 96.0 %, full frame — so the guard asserts
#      < 2 % differing pixels and no differing pixel in the outer 10 % border;
#   6. a summary block (library, manifest + sha256 + instance count, status line,
#      capture paths + byte sizes, the wave-delta measurement).
#
# Environment:
#   SOTC_LIB  the converted asset library (default: <repo>/out/library; the same
#             variable the app and its tests read — exported here so ctest and every
#             run agree)
#
# The manifest is W2's deliverable and is consumed as-is: when it is absent the pass
# fails loudly, naming the W2 plan — this plan ships no fallback (S3 design §5: a
# curated composition would be a separate, user-approved, clearly-labeled step).
#
# A screenshot file is removed before its run, so a stale PNG can never mask a failed
# write. The interactive guarantees (cursor free, drag-look, walk/run feel, edge
# clamp, the title bar's fps readout) are the user's sign-off walk, whose checklist is
# recorded in docs/notes/s3-results.md.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
manifest="$repo_root/out/re/stage/valus.manifest"
player="models/shadow/characters/wander/_cmn/wander.glb"
out_dir="$repo_root/out/s3"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s3-acceptance: $*" >&2; exit 1; }

[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$manifest" ]] ||
  fail "W2's arena manifest not found: $manifest — run the W2 plan (docs/superpowers/plans/2026-10-18-re3-stage-composition.md) to emit it; this pass ships no fallback"

manifest_instances="$(grep -c '^instance ' "$manifest" || true)"
[[ "$manifest_instances" -gt 0 ]] || fail "manifest has no instance lines: $manifest"

# --- Step 1: configure + build ------------------------------------------------

stamp "build: app -> app/build"
cmake -S "$repo_root/app" -B "$build_dir"
cmake --build "$build_dir" -j
[[ -x "$viewer" ]] || fail "build did not produce $viewer"

# --- Step 2: test suite -------------------------------------------------------

export SOTC_LIB="$lib"
stamp "ctest: $build_dir"
ctest --test-dir "$build_dir" --output-on-failure

# --- Step 3: the composed-scene self-test on W2's real manifest ---------------

stamp "s3-selftest: $manifest ($manifest_instances instances)"
if ! selftest_out="$("$viewer" --library "$lib" --manifest "$manifest" --model "$player" \
  --s3-selftest 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s3-selftest exited non-zero"
fi
printf '%s\n' "$selftest_out"
if ! status_line="$(printf '%s\n' "$selftest_out" |
  grep -m1 -E 's3-status instances=[0-9]+ .* ground=ok')"; then
  fail "s3-selftest did not print 's3-status instances=<n> ... ground=ok'"
fi
status_instances="${status_line#*instances=}"
status_instances="${status_instances%% *}"
[[ "$status_instances" = "$manifest_instances" ]] ||
  fail "s3-status reports $status_instances instances but the manifest has $manifest_instances"

# --- Step 4: the spawn screenshot ---------------------------------------------

mkdir -p "$out_dir"

# screenshot NAME [viewer flags...] — a manifest spawn capture with the s1 PNG
# assertions; the extra flags are passed through to the viewer.
screenshot() {
  local name="$1"
  shift
  local png magic bytes output status pixels_line
  local min_r min_g min_b max_r max_g max_b span d
  png="$out_dir/$name.png"
  rm -f "$png"
  stamp "screenshot: $name (manifest spawn)"
  # The viewer's output is captured (and printed below) so the run's own
  # s1-pixels stats can prove the frame was actually drawn, not just written.
  output="$("$viewer" --library "$lib" --manifest "$manifest" --model "$player" \
    --screenshot "$png" "$@" 2>&1)" || {
    status=$?
    printf '%s\n' "$output" >&2
    fail "screenshot $name: viewer exited $status"
  }
  printf '%s\n' "$output"
  [[ -f "$png" ]] || fail "screenshot $name: $png was not written"
  magic="$(od -An -tx1 -N4 "$png" | tr -d ' \n')"
  [[ "$magic" = "89504e47" ]] || fail "screenshot $name: $png lacks the PNG magic (got $magic)"
  bytes="$(wc -c < "$png" | tr -d '[:space:]')"
  [[ "$bytes" -gt 5000 ]] || fail "screenshot $name: $png is only $bytes bytes (need > 5000)"
  # A silently broken draw loop still writes a valid PNG of a solid clear colour,
  # so require a real span in some channel.
  pixels_line="$(printf '%s\n' "$output" | grep -m1 '^s1-pixels ')" ||
    fail "screenshot $name: viewer printed no s1-pixels line"
  min_csv="${pixels_line#*min=}"; min_csv="${min_csv%% *}"
  max_csv="${pixels_line#*max=}"; max_csv="${max_csv%% *}"
  IFS=, read -r min_r min_g min_b <<<"$min_csv"
  IFS=, read -r max_r max_g max_b <<<"$max_csv"
  span=$((max_r - min_r))
  d=$((max_g - min_g)); if ((d > span)); then span=$d; fi
  d=$((max_b - min_b)); if ((d > span)); then span=$d; fi
  [[ "$span" -ge 30 ]] ||
    fail "screenshot $name: uniform frame (largest channel span $span < 30): $pixels_line"
}

# The spawn capture pins the time: the skinned player plays its clip by default
# (S2), and a pinned time keeps the capture byte-reproducible.
screenshot spawn --anim-time 0

# --- Step 5: the F3 wave guard (W1.4 review Q2) --------------------------------

# wave_capture TIME PNG — one composed-scene render with the wave pinned at TIME.
wave_capture() {
  local time="$1" png="$2" output status
  rm -f "$png"
  output="$("$viewer" --library "$lib" --manifest "$manifest" --model "$player" \
    --s3-selftest --screenshot "$png" --anim-time "$time" 2>&1)" || {
    status=$?
    printf '%s\n' "$output" >&2
    fail "wave guard: viewer exited $status (t=$time)"
  }
  printf '%s\n' "$output"
  [[ -f "$png" ]] || fail "wave guard: $png was not written (t=$time)"
}

stamp "wave guard: captures at t=0 and t=1"
wave_capture 0 "$out_dir/wave-t0.png"
wave_capture 1.0 "$out_dir/wave-t1.png"

# The pixel diff is stdlib-only Python (PNG decode + differ, ~3 s): the guard
# needs the differing pixels' count and extent, which od/wc cannot give. It
# prints one machine line; the assertions below are the guard.
if ! diff_out="$(python3 - "$out_dir/wave-t0.png" "$out_dir/wave-t1.png" 2>&1 <<'PY'
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

first = read_png(sys.argv[1])
second = read_png(sys.argv[2])
if first[:2] != second[:2]:
    raise SystemExit(f"size mismatch: {first[:2]} vs {second[:2]}")
width, height, a = first
b = second[2]
border_x = width // 10
border_y = height // 10
count = 0
margin = 0
min_x = min_y = 1 << 30
max_x = max_y = -1
for y in range(height):
    row = y * width * 4
    for x in range(width):
        i = row + x * 4
        if a[i:i + 4] != b[i:i + 4]:
            count += 1
            if x < border_x or x >= width - border_x or y < border_y or y >= height - border_y:
                margin += 1
            if x < min_x: min_x = x
            if x > max_x: max_x = x
            if y < min_y: min_y = y
            if y > max_y: max_y = y
if count == 0:
    min_x = min_y = max_x = max_y = -1
print(f"s3-wave-pixels differing={count} total={width * height} margin={margin} "
      f"bbox={min_x},{min_y},{max_x},{max_y}")
PY
)"; then
  printf '%s\n' "$diff_out" >&2
  fail "wave guard: the pixel diff failed"
fi
printf '%s\n' "$diff_out"
if ! wave_line="$(printf '%s\n' "$diff_out" | grep -m1 '^s3-wave-pixels ')"; then
  fail "wave guard: no s3-wave-pixels line in the diff output"
fi
wave_differing="${wave_line#*differing=}"; wave_differing="${wave_differing%% *}"
wave_total="${wave_line#*total=}"; wave_total="${wave_total%% *}"
wave_margin="${wave_line#*margin=}"; wave_margin="${wave_margin%% *}"
# Non-vacuous: the player's pose must actually differ between the two times.
[[ "$wave_differing" -gt 0 ]] ||
  fail "wave guard: wave t=0 and t=1 rendered identical frames — the player's pose did not advance"
wave_limit=$((wave_total / 50))  # 2 %: measured 0.36 % correct vs 96.0 % pre-fix
[[ "$wave_differing" -lt "$wave_limit" ]] ||
  fail "wave guard: $wave_differing of $wave_total pixels differ between wave t=0 and t=1 (>= 2 %: the arena is warping with the wave — F3 regression?)"
[[ "$wave_margin" -eq 0 ]] ||
  fail "wave guard: $wave_margin differing pixels in the outer 10 % border (the world must not move with the wave — F3 regression?)"

# --- Step 6: summary -----------------------------------------------------------

stamp "summary"
printf 's3-acceptance: library  %s\n' "$lib"
printf 's3-acceptance: manifest %s\n' "$manifest"
printf 's3-acceptance: manifest sha256 %s instances %s\n' \
  "$(shasum -a 256 "$manifest" | cut -d' ' -f1)" "$manifest_instances"
printf 's3-acceptance: status   %s\n' "$status_line"
printf 's3-acceptance: capture  %s  %s bytes\n' "$out_dir/spawn.png" \
  "$(wc -c < "$out_dir/spawn.png" | tr -d '[:space:]')"
printf 's3-acceptance: wave     %s\n' "$wave_line"
printf 's3-acceptance: PASS — build, ctest, %s-instance composed scene (ground=ok), spawn PNG (\\x89PNG magic, >5000 B, non-uniform pixels), wave guard (<2 %% delta, border untouched)\n' \
  "$manifest_instances"
