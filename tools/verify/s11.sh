#!/usr/bin/env bash
# s11.sh — the runner's adapter for S11 (Quadratus, Task 10). Unlike s3…s9 it is no second copy: there is
# no frozen scripts/s11-acceptance.sh yet (Task 11 writes it and blesses the goldens). Until both exist
# run.py runs this adapter REPORT-ONLY (run.report_only): its own gates below decide the section, the
# viewer runs are kept as golden candidates, nothing is diffed.
#   gpu: the preflight (arena_02.manifest selects Quadratus; his bake), the four flag refusals (exit 2),
#        the selftest + four 2560x1440 stills (the timeline's gates), the e2e (skipped by --quick): its
#        lines equal the selftest's, so the interactive loop and the script agree.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

arena02_manifest="$repo_root/out/re/stage/arena_02.manifest"
quad_clips_json="$lib/models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.clips.json"
stills_dir="$out_dir/stills"

preflight() {
  [[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
  [[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
  [[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
  [[ -x "$viewer_bin" ]] || fail "viewer not built: $viewer_bin"
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
}

section_gpu() {
preflight

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

stamp "s11-selftest + stills: $arena02_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s11-selftest \
  --s11-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s11-selftest --s11-stills exited non-zero"
fi
printf '%s\n' "$selftest_out"

one_line() {
  local regex="$1" label="$2" count
  count="$(grep -c -E "$regex" <<<"$selftest_out" || true)"
  [[ "$count" = "1" ]] || fail "$label: expected exactly one line, found $count"
  grep -m1 -E "$regex" <<<"$selftest_out"
}

# The timeline's events in order (the climb/strike lines in between vary with the route).
s11_events="$(grep -E '^s11-(load|attack|charge|hoof|grab|sigil|back|shake|death|corpse|reset|perf|status) ' \
  <<<"$selftest_out" | awk '{ print $1 }' | tr '\n' ' ')"
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
strikes="$(grep -E '^s11-strike ' <<<"$selftest_out" | sed 's/.*weakpoint=\([^ ]*\) .*/\1/' | uniq | tr '\n' ' ')"
[[ "$strikes" = "weakpoint_05 weakpoint_04 " ]] || fail "the sigils were not struck back then head: '$strikes'"
! grep -q -E '^s11-strike .* gate=(sealed|ignore|transition) ' <<<"$selftest_out" ||
  fail "a strike was sealed or ignored"
[[ "$(grep -E '^s11-strike ' <<<"$selftest_out" | tail -1)" =~ \ health=0\ killed=1\  ]] ||
  fail "the last strike did not kill him"
deaths="$(grep -E '^s11-death ' <<<"$selftest_out" | sed 's/^s11-death phase=\([A-Za-z]*\) .*/\1/' | tr '\n' ' ')"
[[ "$deaths" = "Motion Collapse Done " ]] || fail "the death phases are '$deaths'"
corpse_line="$(one_line '^s11-corpse ' s11-corpse)"
[[ "$corpse_line" =~ ^s11-corpse\ live_body=1\ clip=boss_a_death\ t=5\.333\ held=1\ moved=17\.[0-9]+\ .*\ return=Idle$ ]] ||
  fail "the corpse did not stay as his held death frame: $corpse_line"
[[ "$(one_line '^s11-reset ' s11-reset)" = "s11-reset health=220 defeated=0 state=Stand at_base=1 player_hp=100 mode=grounded death=Idle open=weakpoint_05" ]] ||
  fail "R did not bring Quadratus back to his opening state"
perf_line="$(one_line '^s11-perf ' s11-perf)"
[[ "$perf_line" =~ ^s11-perf\ frames=240\ frame_ms=[0-9]+\.[0-9]+\ drawn=[0-9]+\ culled=[0-9]+$ ]] ||
  fail "s11-perf is malformed: $perf_line"
one_line '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' s1-status > /dev/null

for name in kneel back head death; do
  path="$stills_dir/$name.png"
  [[ "$(grep -c -E "^s1-screenshot path=$path width=2560 height=1440\$" <<<"$selftest_out" || true)" = "1" ]] ||
    fail "still $name: expected one 2560x1440 s1-screenshot line for $path"
  [[ -s "$path" ]] || fail "still $name: $path is missing or empty"
done

if [[ -z "${VERIFY_QUICK:-}" ]]; then   # --quick skips the e2e run
  stamp "s11-e2e: the interactive loop from the arena_02 spawn to his death and R"
  if ! e2e_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s11-e2e 2>&1)"; then
    printf '%s\n' "$e2e_out" >&2
    fail "--s11-e2e exited non-zero"
  fi
  printf '%s\n' "$e2e_out"
  [[ "$(grep -c -E '^s11e2e-done frames=[0-9]+ ' <<<"$e2e_out" || true)" = "1" ]] || fail "no s11e2e-done line"
  grep -q -E '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' <<<"$e2e_out" ||
    fail "the e2e printed no s1-status"
  # The interactive loop played the selftest's fight: the same lines, the timings aside.
  self_lines="$(grep -E '^s11-' <<<"$selftest_out" | grep -v -E '^s11-(load|perf|status) ' | sed 's/^s11-//')"
  e2e_lines="$(grep -E '^s11e2e-' <<<"$e2e_out" | grep -v -E '^s11e2e-(load|done) ' | sed 's/^s11e2e-//')"
  [[ -n "$self_lines" && "$self_lines" = "$e2e_lines" ]] ||
    fail "the e2e's lines differ from the selftest's: $(diff <(printf '%s\n' "$self_lines") <(printf '%s\n' "$e2e_lines") | head -6 | tr '\n' ' ')"
fi
}

run_section "$1"
