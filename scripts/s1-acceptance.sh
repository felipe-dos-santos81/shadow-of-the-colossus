#!/usr/bin/env bash
#
# s1-acceptance.sh — the S1 acceptance pass for the windowed SDL viewer, re-runnable end to end.
#
# Steps (S1 milestone: "app/ builds an arm64 binary; window opens windowed; cursor visible and
# free; Wander + a prop textured; fps HUD; screenshot + notes"):
#
#   1. configure + build app/ (viewer, tests, and mesh.metal copied next to the binary);
#   2. run the suite — `ctest --test-dir app/build --output-on-failure` (unit tests plus the
#      windowed selftest, which opens a real window and pins the s1-status line);
#   3. run --selftest explicitly and require the exact
#      `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0` line — the state queried back
#      from SDL (windowed flag, relative mouse mode, the auto-capture hint), not assumed flags;
#   4. two screenshot runs — wander (a character) and obj_plank_01 (a prop) — writing
#      out/s1/<name>.png; each PNG must carry the \x89PNG magic, be larger than 5000 bytes,
#      and show a non-uniform frame (largest channel span >= 30 in the viewer's own
#      s1-pixels stats) — a solid clear-colour frame passes the size floor (it compresses
#      to ~146 KB), so bytes alone cannot prove the draw loop ran;
#   5. a summary block (library, status line, screenshot paths + byte sizes).
#
# Environment:
#   SOTC_LIB  the converted asset library (default: <repo>/out/library; the same variable the
#             app and its tests read — exported here so ctest and every run agree)
#
# A screenshot file is removed before its run, so a stale PNG can never mask a failed write. The
# interactive guarantees (a click does not capture the cursor, ⌘-Tab works, the window opens
# windowed) cannot be automated from a script; step 3's queried state plus the manual pass
# recorded in docs/notes/s1-results.md cover them.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
out_dir="$repo_root/out/s1"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s1-acceptance: $*" >&2; exit 1; }

[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"

# --- Step 1: configure + build ------------------------------------------------

stamp "build: app -> app/build"
cmake -S "$repo_root/app" -B "$build_dir"
cmake --build "$build_dir" -j
[[ -x "$viewer" ]] || fail "build did not produce $viewer"

# --- Step 2: test suite -------------------------------------------------------

export SOTC_LIB="$lib"
stamp "ctest: $build_dir"
ctest --test-dir "$build_dir" --output-on-failure

# --- Step 3: selftest — the window/cursor guarantees, queried from SDL --------

stamp "selftest: $viewer --library $lib --selftest"
if ! selftest_out="$("$viewer" --library "$lib" --selftest 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--selftest exited non-zero"
fi
printf '%s\n' "$selftest_out"
if ! status_line="$(printf '%s\n' "$selftest_out" |
  grep -m1 's1-status windowed=1 relative_mouse=0 mouse_auto_capture=0')"; then
  fail "selftest did not print 's1-status windowed=1 relative_mouse=0 mouse_auto_capture=0'"
fi

# --- Step 4: screenshot runs --------------------------------------------------

mkdir -p "$out_dir"

screenshot() {
  local name="$1" rel="$2" png magic bytes output status pixels_line
  local min_r min_g min_b max_r max_g max_b span d
  png="$out_dir/$name.png"
  rm -f "$png"
  stamp "screenshot: $name <- $rel"
  # The viewer's output is captured (and printed below) so the run's own
  # s1-pixels stats can prove the frame was actually drawn, not just written.
  output="$("$viewer" --library "$lib" --model "$rel" --screenshot "$png" --frames 3 2>&1)" || {
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
  # A silently broken draw loop still writes a valid PNG of ~146 KB (a solid
  # clear-colour 2560x1440 frame), so require a real span in some channel.
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

screenshot wander models/shadow/characters/wander/_cmn/wander.glb
screenshot plank models/objects/props/_cmn/obj_plank_01.glb

# --- Step 5: summary ----------------------------------------------------------

stamp "summary"
printf 's1-acceptance: library  %s\n' "$lib"
printf 's1-acceptance: status   %s\n' "$status_line"
printf 's1-acceptance: capture  %s  %s bytes\n' "$out_dir/wander.png" \
  "$(wc -c < "$out_dir/wander.png" | tr -d '[:space:]')"
printf 's1-acceptance: capture  %s  %s bytes\n' "$out_dir/plank.png" \
  "$(wc -c < "$out_dir/plank.png" | tr -d '[:space:]')"
printf 's1-acceptance: PASS — build, ctest, selftest status, 2 PNG screenshots (\\x89PNG magic, >5000 B, non-uniform pixels)\n'
