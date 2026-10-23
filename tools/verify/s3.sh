#!/usr/bin/env bash
# s3.sh — the runner's adapter for scripts/s3-acceptance.sh (see common.sh): its spawn/selftest/wave
# viewer runs and gates, copied verbatim; the build + ctest are run.py's; the wave diff heredoc is
# extracted from the frozen script.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

manifest="$repo_root/out/re/stage/valus.manifest"

section_gpu() {
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$manifest" ]] ||
  fail "W2's arena manifest not found: $manifest — run the W2 plan (docs/superpowers/plans/2026-10-18-re3-stage-composition.md) to emit it; this pass ships no fallback"

manifest_instances="$(grep -c '^instance ' "$manifest" || true)"
[[ "$manifest_instances" -gt 0 ]] || fail "manifest has no instance lines: $manifest"

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

wave_py="$(heredoc_py s3 1)"
if ! diff_out="$(python3 "$wave_py" "$out_dir/wave-t0.png" "$out_dir/wave-t1.png" 2>&1)"; then
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
keep wave-pixels "$diff_out"
}

run_section "$1"
