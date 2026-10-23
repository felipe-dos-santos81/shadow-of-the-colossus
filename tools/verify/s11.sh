#!/usr/bin/env bash
# s11.sh — the runner's adapter for S11 (Quadratus): a second copy of scripts/s11-acceptance.sh's
# assertions, like s3…s9 (the frozen S9 chain and the build are run.py's; the gates are verbatim).
#   gpu: the preflight (arena_02.manifest selects Quadratus; his bake), the four flag refusals + the
#        --stage refusal (exit 2), the selftest + four 2560x1440 stills (the timeline's gates, the perf
#        budget, the pixel spans), the e2e (skipped by --quick): one arena load and no shrine, and its
#        lines equal the selftest's, so the interactive loop and the script agree.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

arena02_manifest="$repo_root/out/re/stage/arena_02.manifest"
quad_clips_json="$lib/models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.clips.json"
stills_dir="$out_dir/stills"

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

preflight() {
  for c in caffeinate awk grep sed tr wc python3 od diff uniq; do
    need "$c"
  done
  [[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
  [[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
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
fi
}

run_section "$1"
