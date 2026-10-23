#!/usr/bin/env bash
#
# s11-acceptance.sh — the S11 "Quadratus" acceptance pass, re-runnable.
#
# Steps (the milestone: the second colossus in arena_02 — stab a lifted rear
# hoof, he kneels, climb the fur, strike the back sigil, walk his back, hold
# through the shake-off, strike the head sigil; his fight is silent, D10):
#
#   1. (preflight) the inputs: SOTC_GAME_ROOT, the library with Quadratus's
#      bake (his clips.json: boss_a_stand/death/down_b_l_start), Wander;
#      out/re/stage/arena_02.manifest is composed if absent (probe.py compose
#      --stage arena_02) and must select Quadratus (`encounter quadratus`);
#   2. the **frozen S9 acceptance first** (`scripts/s9-acceptance.sh`, never
#      edited — exactly `make verify-long`'s chain before S11: it builds both
#      the converter and app/, runs both suites and S8 → S7 → S6 → S5 → S4.2 →
#      S4.1 → S4 → S3). Its outputs are not re-asserted here beyond its PASS line;
#   3. the flag refusals (exit 2): --s11-stills without --s11-selftest,
#      --s11-selftest with --s11-e2e, --s11-e2e with --stage shrine,
#      --s11-selftest without --manifest, --stage plain with --s11-selftest;
#   4. the deterministic proof — `--s11-selftest --s11-stills out/s11/stills`:
#      the events in order (load, attack, charge, hoof, grab, back sigil, back
#      walk, shake start + survived, the three death phases, corpse, reset,
#      perf, status); the layered ground and return=0; the charge dodged; a
#      rear hoof's stab kneels him without damage; the fur grabbed in the
#      kneel; the back sigil takes its half (health 110) before the head's
#      strikes, none sealed; the last strike kills; the corpse is his held
#      death frame (return=Idle: no shrine); R restores the opening state
#      exactly; the perf line <= 16.7 ms; phases_done=11; windowed, cursor free;
#   5. the four stills (kneel, back, head, death): one 2560x1440 s1-screenshot
#      line each, PNG magic, > 5000 B, the viewer's channel span >= 30;
#   6. the end-to-end loop — `--s11-e2e`: the interactive loop at a fixed
#      1/60 s with scripted input; one arena load and no shrine load; its
#      s11e2e-* lines equal the selftest's s11-* lines (load/perf/status aside);
#   7. a summary block with every measured number.
#
# tools/verify/s11.sh is the runner's second copy of steps 1 and 3–6
# (test_coverage.py proves every gate below is in it). Every viewer and
# acceptance run is wrapped in `caffeinate -dimsu`. Never run two acceptances
# concurrently. The S9 chain takes ~14 min.
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library,
#                   which holds the S9 bake and Quadratus's since S11, ruling A16)
#
# Identity is not scriptable: the lines prove the wiring, not that the fight
# feels like the game's. docs/notes/s11-results.md records every measured
# number; the user's walk checklist is PENDING there.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
probe="$repo_root/tools/re/stage/probe.py"
arena02_manifest="$repo_root/out/re/stage/arena_02.manifest"
quad_clips_json="$lib/models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.clips.json"
player="models/shadow/characters/wander/_cmn/wander.glb"
out_dir="$repo_root/out/s11"
stills_dir="$out_dir/stills"
selftest_txt="$out_dir/s11-selftest.txt"
e2e_txt="$out_dir/s11-e2e.txt"
run_log="$repo_root/out/s11-acceptance-run.log"
s9_log="$repo_root/out/s11-s9-regression.log"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s11-acceptance: FAIL — $*" >&2; exit 1; }

mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

for c in dotnet cmake ctest caffeinate shasum awk grep sed sort tee tr wc python3 od cmp diff cut uniq; do
  need "$c"
done

# --- Step 1: the inputs (before the ~14 min S9 chain) -------------------------

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
if [[ ! -f "$arena02_manifest" ]]; then
  stamp "compose: arena_02 -> $arena02_manifest"
  python3 "$probe" compose --stage arena_02 --library "$lib" --out "$arena02_manifest"
fi
[[ -f "$arena02_manifest" ]] ||
  fail "arena_02 manifest not found: $arena02_manifest (python3 tools/re/stage/probe.py compose --stage arena_02)"
grep -q '^encounter quadratus$' "$arena02_manifest" ||
  fail "$arena02_manifest does not select Quadratus (encounter quadratus)"
[[ -f "$quad_clips_json" ]] || fail "Quadratus's clip list not found: $quad_clips_json (make bake-quadratus)"
python3 -I -c 'import json, sys
c = json.load(open(sys.argv[1]))["clips"]
names = {x if isinstance(x, str) else x["name"] for x in c}
sys.exit(0 if {"boss_a_stand", "boss_a_death", "boss_a_down_b_l_start"} <= names else 1)' "$quad_clips_json" ||
  fail "$quad_clips_json lacks his bake (boss_a_stand/death/down_b_l_start: make bake-quadratus)"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"
started=$SECONDS

# --- Step 2: the frozen S9 acceptance (builds, suites, S8 → … → S3) -------------

stamp "s9 regression: scripts/s9-acceptance.sh (builds, both suites, S8, S7, S6, S5, S4.2, S4.1, S4 and S3)"
if ! caffeinate -dimsu "$repo_root/scripts/s9-acceptance.sh" > "$s9_log" 2>&1; then
  tail -40 "$s9_log" >&2
  fail "the frozen S9 acceptance regression failed (log: $s9_log)"
fi
s9_pass_line="$(grep -m1 -E '^s9-acceptance: PASS — ' "$s9_log" || true)"
[[ -n "$s9_pass_line" ]] || fail "the S9 regression did not print its final PASS — line"
printf '%s\n' "$s9_pass_line" | cut -c1-200
s9_seconds=$((SECONDS - started))
[[ -x "$viewer" ]] || fail "the S9 chain did not build $viewer"

# --- Steps 3–5: the refusals, the selftest, the stills ------------------------

stamp "clean: the acceptance's own artifacts under $out_dir"
mkdir -p "$out_dir"
rm -rf "$stills_dir"
mkdir -p "$stills_dir"

# refused LABEL ARGS… — the viewer must exit 2 (a usage refusal) before any load.
refused() {
  local label="$1" rc=0 err
  shift
  err="$(caffeinate -dimsu "$viewer" "$@" 2>&1)" || rc=$?
  [[ "$rc" = "2" ]] || fail "$label: exit $rc (expected the usage refusal, exit 2): $err"
  grep -q -- '--s11-' <<<"$err" || fail "$label: exit 2 without an --s11-* message: $err"
  printf 's11: refused (exit 2) %s\n' "$label"
}
base_args=(--library "$lib" --manifest "$arena02_manifest" --model "$player")
refused "--s11-stills without --s11-selftest" "${base_args[@]}" --s11-stills "$stills_dir"
refused "--s11-selftest with --s11-e2e" "${base_args[@]}" --s11-selftest --s11-e2e
refused "--s11-e2e with --stage shrine" "${base_args[@]}" --stage shrine --s11-e2e
refused "--s11-selftest without --manifest" --library "$lib" --model "$player" --s11-selftest
# --stage with the selftest meets main.cpp's general rule first (--s11-selftest is a frozen-style
# selftest: --stage is an interactive start), so its message names --stage, not --s11-*.
stage_rc=0
stage_err="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --stage plain --s11-selftest 2>&1)" || stage_rc=$?
[[ "$stage_rc" = "2" ]] && grep -q -- '--stage plain is an interactive start' <<<"$stage_err" ||
  fail "--s11-selftest with --stage plain: exit $stage_rc (expected the usage refusal, exit 2): $stage_err"

stamp "s11-selftest + stills: $arena02_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s11-selftest \
  --s11-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s11-selftest --s11-stills exited non-zero"
fi
printf '%s\n' "$selftest_out"

# field LINE KEY — the value of KEY=… in LINE (assign it to a variable: set -e then stops on a miss).
field() {
  local line="$1" key="$2" rest
  rest="${line#*" $key"=}"
  [[ "$rest" != "$line" ]] || fail "field $key missing from: $line"
  printf '%s' "${rest%% *}"
}

# cmp_ok VALUE OP LIMIT — an awk comparison (OP one of < <= > >=); empty fails.
cmp_ok() {
  [[ -n "$1" && -n "$3" ]] || fail "cmp_ok: an empty argument ('$1' '$2' '$3')"
  [[ "$1" =~ ^-?[0-9]+(\.[0-9]+)?$ && "$3" =~ ^-?[0-9]+(\.[0-9]+)?$ ]] ||
    fail "cmp_ok: a non-numeric argument ('$1' '$2' '$3')"
  awk -v v="$1" -v l="$3" -v op="$2" 'BEGIN {
    if (op == "<") exit !(v < l); if (op == "<=") exit !(v <= l);
    if (op == ">") exit !(v > l); if (op == ">=") exit !(v >= l); exit 1 }'
}

one_line() {
  local regex="$1" label="$2" count
  count="$(grep -c -E "$regex" <<<"$selftest_out" || true)"
  [[ "$count" = "1" ]] || fail "$label: expected exactly one line, found $count"
  grep -m1 -E "$regex" <<<"$selftest_out"
}

# The timeline's events in order (the climb/strike lines in between vary with the route).
s11_events="$(grep -E '^s11-(load|attack|charge|hoof|grab|sigil|back|shake|death|corpse|reset|perf|status) ' <<<"$selftest_out" | awk '{ print $1 }' | tr '\n' ' ' || true)"
[[ "$s11_events" = "s11-load s11-attack s11-charge s11-hoof s11-grab s11-sigil s11-back s11-shake s11-shake s11-death s11-death s11-death s11-corpse s11-reset s11-perf s11-status " ]] ||
  fail "the s11-* events are '$s11_events' (expected the timeline's order)"

load_line="$(one_line '^s11-load ' s11-load)"
[[ "$load_line" =~ ^s11-load\ stage=arena\ encounter=quadratus\ meshes=[0-9]+\ ground=[0-9]+x[0-9]+\ layers=64\ .*\ return=0\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s11-load is malformed (a layered ground, no return): $load_line"
charge_line="$(one_line '^s11-charge ' s11-charge)"
[[ "$charge_line" =~ \ dodged=1\ .*\ player_hp=100\  ]] || fail "the charge was not dodged: $charge_line"
hoof_line="$(one_line '^s11-hoof ' s11-hoof)"
[[ "$hoof_line" =~ ^s11-hoof\ weakpoint=colossus_02_hoof_weakpoint_r[lr]\ .*\ state=DownB[LR]Start\ damage=0\ health=220\  ]] ||
  fail "a rear hoof's stab did not kneel him without damage: $hoof_line"
grab_line="$(one_line '^s11-grab ' s11-grab)"
[[ "$grab_line" =~ \ state=DownB[LR](Start|Loop)\  ]] || fail "the fur was not grabbed during the kneel: $grab_line"
[[ "$(one_line '^s11-sigil ' s11-sigil)" =~ ^s11-sigil\ done=back\ .*\ health=110\  ]] ||
  fail "the back sigil did not take its half"
back_line="$(one_line '^s11-back ' s11-back)"
[[ "$back_line" =~ ^s11-back\ band=back\ .*\ standing=1\  ]] || fail "no surface walk on his back: $back_line"
[[ "$(one_line '^s11-shake survived=' s11-shake)" =~ ^s11-shake\ survived=1\ band=back\  ]] ||
  fail "the shake was not survived on his back"
# The sigil order: every strike on the back sigil comes before the first on the head's; none is sealed.
strikes="$(grep -E '^s11-strike ' <<<"$selftest_out" | sed 's/.*weakpoint=\([^ ]*\) .*/\1/' | uniq | tr '\n' ' ' || true)"
[[ "$strikes" = "weakpoint_05 weakpoint_04 " ]] || fail "the sigils were not struck back then head: '$strikes'"
! grep -q -E '^s11-strike .* gate=(sealed|ignore|transition) ' <<<"$selftest_out" ||
  fail "a strike was sealed or ignored"
[[ "$(grep -E '^s11-strike ' <<<"$selftest_out" | tail -1)" =~ \ health=0\ killed=1\  ]] ||
  fail "the last strike did not kill him"
deaths="$(grep -E '^s11-death ' <<<"$selftest_out" | sed 's/^s11-death phase=\([A-Za-z]*\) .*/\1/' | tr '\n' ' ' || true)"
[[ "$deaths" = "Motion Collapse Done " ]] || fail "the death phases are '$deaths'"
corpse_line="$(one_line '^s11-corpse ' s11-corpse)"
[[ "$corpse_line" =~ ^s11-corpse\ live_body=1\ clip=boss_a_death\ t=5\.333\ held=1\ moved=17\.[0-9]+\ .*\ return=Idle$ ]] ||
  fail "the corpse did not stay as his held death frame: $corpse_line"
[[ "$(one_line '^s11-reset ' s11-reset)" = "s11-reset health=220 defeated=0 state=Stand at_base=1 player_hp=100 mode=grounded death=Idle open=weakpoint_05" ]] ||
  fail "R did not bring Quadratus back to his opening state"
perf_line="$(one_line '^s11-perf ' s11-perf)"
[[ "$perf_line" =~ ^s11-perf\ frames=240\ frame_ms=[0-9]+\.[0-9]+\ drawn=[0-9]+\ culled=[0-9]+$ ]] ||
  fail "s11-perf is malformed: $perf_line"
perf_frame="$(field "$perf_line" frame_ms)"
cmp_ok "$perf_frame" ">" 0 && cmp_ok "$perf_frame" "<=" 16.7 ||
  fail "the arena_02 frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
[[ "$(one_line '^s11-status ' s11-status)" =~ ^s11-status\ phases_done=11\  ]] ||
  fail "the selftest did not reach its last phase (reset)"
one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' s1-status > /dev/null

# The four stills: one 2560x1440 s1-screenshot line each, PNG magic, > 5000 B, a channel span >= 30.
span_for() {
  local want="$1"
  awk -v want="$want" '
    /^s1-pixels / { line = $0 }
    /^s1-screenshot / { if ($2 == "path=" want) { print line; exit } }
  ' <<<"$selftest_out"
}

size_for() {
  local want="$1"
  awk -v want="$want" '
    /^s1-screenshot / {
      if ($2 == "path=" want) {
        w = $3; h = $4;
        sub(/^width=/, "", w); sub(/^height=/, "", h);
        print w, h; exit
      }
    }
  ' <<<"$selftest_out"
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
for name in kneel back head death; do
  path="$stills_dir/$name.png"
  [[ "$(grep -c -E "^s1-screenshot path=$path width=2560 height=1440\$" <<<"$selftest_out" || true)" = "1" ]] ||
    fail "still $name: expected one 2560x1440 s1-screenshot line for $path"
  row="$(check_png "$name" "$path")"
  stills="${stills}${stills:+ }$row"
done
printf '%s
' "$selftest_out" > "$selftest_txt"

# --- Step 6: the end-to-end loop ---------------------------------------------

stamp "s11-e2e: the interactive loop from the arena_02 spawn to his death and R"
if ! e2e_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s11-e2e 2>&1)"; then
  printf '%s\n' "$e2e_out" >&2
  fail "--s11-e2e exited non-zero"
fi
printf '%s\n' "$e2e_out"
[[ "$(grep -c -E '^s11e2e-done frames=[0-9]+ ' <<<"$e2e_out" || true)" = "1" ]] || fail "no s11e2e-done line"
grep -q -E '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' <<<"$e2e_out" ||
  fail "the e2e printed no s1-status"
# returnWired is off for a non-Valus encounter: the loop loads the arena once and never the shrine.
e2e_loads="$(grep -E '^s7-load ' <<<"$e2e_out" | awk '{ print $2 }' | tr '\n' ' ' || true)"
[[ "$e2e_loads" = "stage=arena " ]] ||
  fail "the e2e loaded '$e2e_loads' (expected the arena once: no shrine after his death)"
e2e_load_line="$(grep -m1 -E '^s11e2e-load ' <<<"$e2e_out" || true)"
[[ "$e2e_load_line" =~ ^s11e2e-load\ stage=arena\ encounter=quadratus\ meshes=[0-9]+\ ground=[0-9]+x[0-9]+\ layers=64\ .*\ return=0\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s11e2e-load is malformed (Quadratus's arena, a layered ground, no return): $e2e_load_line"
# The interactive loop played the selftest's fight: the same lines, the timings aside.
self_lines="$(grep -E '^s11-' <<<"$selftest_out" | grep -v -E '^s11-(load|perf|status) ' | sed 's/^s11-//' || true)"
e2e_lines="$(grep -E '^s11e2e-' <<<"$e2e_out" | grep -v -E '^s11e2e-(load|done) ' | sed 's/^s11e2e-//' || true)"
[[ -n "$self_lines" && "$self_lines" = "$e2e_lines" ]] ||
  fail "the e2e's lines differ from the selftest's: $(diff <(printf '%s\n' "$self_lines") <(printf '%s\n' "$e2e_lines") | head -6 | tr '\n' ' ')"
printf '%s
' "$e2e_out" > "$e2e_txt"

# --- Step 7: summary ----------------------------------------------------------

stamp "summary"
load_ms="$(field "$load_line" ms)"
load_ground="$(field "$load_line" ground)"
load_meshes="$(field "$load_line" meshes)"
hoof_wp="$(field "$hoof_line" weakpoint)"
hoof_lift="$(field "$hoof_line" lift)"
sigil_line="$(one_line '^s11-sigil ' s11-sigil)"
sigil_strikes="$(field "$sigil_line" strikes)"
back_feet="$(field "$back_line" feet_y)"
shake_line="$(one_line '^s11-shake survived=' s11-shake)"
shake_low="$(field "$shake_line" low)"
corpse_moved="$(field "$corpse_line" moved)"
perf_drawn="$(field "$perf_line" drawn)"
perf_culled="$(field "$perf_line" culled)"
status_line="$(one_line '^s11-status ' s11-status)"
status_frames="$(field "$status_line" frames)"
status_seconds="$(field "$status_line" seconds)"
e2e_done="$(grep -m1 -E '^s11e2e-done ' <<<"$e2e_out")"
e2e_load_ms="$(field "$e2e_load_line" ms)"
total_seconds=$((SECONDS - started))
printf 's11-acceptance: library      %s\n' "$lib"
printf 's11-acceptance: manifest     %s\n' "$arena02_manifest"
printf 's11-acceptance: s9 chain     %s s\n' "$s9_seconds"
printf 's11-acceptance: %s\n' "$load_line" "$charge_line" "$hoof_line" "$grab_line" "$sigil_line" \
  "$back_line" "$shake_line" "$corpse_line" "$perf_line" "$status_line" "$e2e_done"
printf 's11-acceptance: stills       %s\n' "$stills"
printf 's11-acceptance: PASS — the frozen S9 acceptance green (%s s: builds, both suites, S8 → S7 → S6 → S5 → S4.2 → S4.1 → S4 → S3 inside it); five flag refusals exit 2; arena_02 load %s ms (%s meshes, layered ground %s, no return); the charge dodged; the %s stab (lift %s) kneels him without damage; the fur grabbed in the kneel; the back sigil in %s strikes (health 110), the back walk at feet_y %s, the shake survived (stamina low %s), the head sigil, the kill; the death Motion → Collapse → Done with his held death frame (moved %s m) and no shrine; R exact; frame %s ms (drawn %s, culled %s); %s frames / %s s; the e2e loop (%s; load %s ms) equal to the selftest, one arena load; four stills 2560x1440; windowed, cursor free; %s s in all\n' \
  "$s9_seconds" "$load_ms" "$load_meshes" "$load_ground" "$hoof_wp" "$hoof_lift" "$sigil_strikes" "$back_feet" \
  "$shake_low" "$corpse_moved" "$perf_frame" "$perf_drawn" "$perf_culled" "$status_frames" "$status_seconds" \
  "${e2e_done#s11e2e-done }" "$e2e_load_ms" "$total_seconds"
