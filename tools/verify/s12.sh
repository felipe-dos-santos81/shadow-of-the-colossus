#!/usr/bin/env bash
# s12.sh — the runner's adapter for S12 (Gaius), report-only until S12 Task 10 commits
# scripts/s12-acceptance.sh and blesses the goldens (run.report_only).
#   gpu: the preflight (arena_03.manifest selects Gaius; his bake, sidecar and exported graph), the
#        four flag refusals + the --stage refusal (exit 2), the selftest + four 2560x1440 stills (the
#        timeline's gates, Review Focus 5's bound, the perf budget, the pixel spans), the e2e (skipped
#        by --quick): one arena load and no shrine, and its lines equal the selftest's.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

arena03_manifest="$repo_root/out/re/stage/arena_03.manifest"
knight_dir="$lib/models/shadow/characters/colossus_03_knight/_cmn"
knight_clips_json="$knight_dir/colossus_03_knight.clips.json"
knight_events_json="$knight_dir/colossus_03_knight.events.json"
knight_graph_json="$lib/behaviour/KNIGHT.graph.json"
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
  [[ -f "$arena03_manifest" ]] ||
    fail "arena_03 manifest not found: $arena03_manifest (make arena03-manifest)"
  grep -q '^encounter knight$' "$arena03_manifest" ||
    fail "$arena03_manifest does not select Gaius (encounter knight)"
  [[ -f "$knight_clips_json" ]] || fail "Gaius's clip list not found: $knight_clips_json (make bake-knight)"
  python3 -I -c 'import json, sys
c = json.load(open(sys.argv[1]))["clips"]
names = {x if isinstance(x, str) else x["name"] for x in c}
sys.exit(0 if {"knight_a_stand", "knight_a_atk1_3", "knight_a_knight_death"} <= names else 1)' "$knight_clips_json" ||
    fail "$knight_clips_json lacks his bake (knight_a_stand/atk1_3/knight_death: make bake-knight)"
  [[ -f "$knight_events_json" ]] || fail "Gaius's events sidecar not found: $knight_events_json (make bake-knight)"
  [[ -f "$knight_graph_json" ]] || fail "Gaius's exported graph not found: $knight_graph_json (make behaviour)"
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
  grep -q -- '--s12-' <<<"$err" || fail "$label: exit 2 without an --s12-* message: $err"
  printf 's12: refused (exit 2) %s\n' "$label"
}
base_args=(--library "$lib" --manifest "$arena03_manifest" --model "$player")
refused "--s12-stills without --s12-selftest" "${base_args[@]}" --s12-stills "$stills_dir"
refused "--s12-selftest with --s12-e2e" "${base_args[@]}" --s12-selftest --s12-e2e
refused "--s12-e2e with --stage shrine" "${base_args[@]}" --stage shrine --s12-e2e
refused "--s12-selftest without --manifest" --library "$lib" --model "$player" --s12-selftest
# --stage with the selftest meets main.cpp's general rule first (a frozen-style selftest).
stage_rc=0
stage_err="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --stage plain --s12-selftest 2>&1)" || stage_rc=$?
[[ "$stage_rc" = "2" ]] && grep -q -- '--stage plain is an interactive start' <<<"$stage_err" ||
  fail "--s12-selftest with --stage plain: exit $stage_rc (expected the usage refusal, exit 2): $stage_err"

stamp "s12-selftest + stills: $arena03_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s12-selftest \
  --s12-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s12-selftest --s12-stills exited non-zero"
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

# The timeline's events in order (the mode/climb/strike lines in between vary with the route).
s12_events="$(grep -E '^s12-(load|judge|slam|hit|dodge|blade|route|sigil|shake|death|corpse|bound|reset|perf|status) ' <<<"$selftest_out" | awk '{ print $1 }' | tr '\n' ' ' || true)"
[[ "$s12_events" = "s12-load s12-judge s12-slam s12-hit s12-dodge s12-blade s12-route s12-sigil s12-shake s12-sigil s12-death s12-death s12-death s12-corpse s12-bound s12-reset s12-perf s12-status " ]] ||
  fail "the s12-* events are '$s12_events' (expected the timeline's order)"

load_line="$(one_line '^s12-load ' s12-load)"
[[ "$load_line" =~ ^s12-load\ stage=arena\ encounter=knight\ meshes=[0-9]+\ ground=[0-9]+x[0-9]+\ layers=64\ .*\ return=0\ graph=KNIGHT\ modes=13\ edges=171\ start=Knight_route_patrol\ clip=knight_a_stand\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s12-load is malformed (his graph from its retry start, a layered ground, no return): $load_line"
# The graph's own mode transitions, named: the slam from route_patrol, the parent mode once attached.
grep -q -E '^s12-mode from=Knight_route_patrol to=Knight_sword_attack_mode ' <<<"$selftest_out" ||
  fail "no route_patrol -> sword_attack_mode transition"
grep -q -E '^s12-mode from=Knight_sword_attack_mode to=Knight_parent_mode ' <<<"$selftest_out" ||
  fail "no sword_attack_mode -> parent_mode transition (the climber on him)"
[[ "$(one_line '^s12-judge ' s12-judge)" =~ ^s12-judge\ volume=KNIGHT_WEAPON_ATK[A-Z_]*\ .*\ sector=forward\  ]] ||
  fail "the first judge volume that held the player is not the slam's, in front of him"
[[ "$(one_line '^s12-slam ' s12-slam)" =~ ^s12-slam\ n=1\ choice=slam\ rays=1\  ]] ||
  fail "the first attack is not a slam with its three rays clear"
[[ "$(one_line '^s12-hit ' s12-hit)" =~ ^s12-hit\ row=KNIGHT_[A-Z0-9_]+\ amount=[1-9][0-9]*\  ]] ||
  fail "no damage row landed on the player"
[[ "$(one_line '^s12-dodge ' s12-dodge)" =~ ^s12-dodge\ sword_rows=0\  ]] || fail "the slam's blade landed on the player"
[[ "$(one_line '^s12-blade ' s12-blade)" =~ ^s12-blade\ window=9\.\.75\ grab_frame=[0-9]+\ close_frame=75\ close_t=0\.[6-9][0-9]\ carried=1\ band=forearm\ slams=1\  ]] ||
  fail "the blade was not grabbed, walked past 60 % and ridden up onto the forearm in one slam"
[[ "$(one_line '^s12-route ' s12-route)" =~ ^s12-route\ bands=blade,forearm,upper_arm,shoulder,neck,head\  ]] ||
  fail "the climb did not follow the band chain to the head"
sigils="$(grep -E '^s12-sigil ' <<<"$selftest_out" | sed 's/^s12-sigil done=\([a-z]*\) .* health=\([0-9]*\) .*/\1:\2/' | tr '\n' ' ' || true)"
[[ "$sigils" = "head:84 belly:0 " ]] || fail "the sigils were '$sigils' (expected head:84 belly:0)"
strikes="$(grep -E '^s12-strike ' <<<"$selftest_out" | sed 's/.*weakpoint=\([^ ]*\) .*/\1/' | uniq | tr '\n' ' ' || true)"
[[ "$strikes" = "weakpoint_01 weakpoint_02 " ]] || fail "the sigils were not struck head then belly: '$strikes'"
[[ "$(grep -E '^s12-strike ' <<<"$selftest_out" | tail -1)" =~ \ health=0\ killed=1\  ]] || fail "the last strike did not kill him"
[[ "$(one_line '^s12-shake ' s12-shake)" =~ ^s12-shake\ survived=1\ clip=knight_a_refuse_[a-z_]+\  ]] ||
  fail "no shake-off was survived"
deaths="$(grep -E '^s12-death ' <<<"$selftest_out" | sed 's/^s12-death phase=\([A-Za-z]*\) .* mode=\([A-Za-z_]*\) .*/\1:\2/' | tr '\n' ' ' || true)"
[[ "$deaths" = "Motion:Knight_death Collapse:Knight_death Done:Knight_death " ]] ||
  fail "the death phases are '$deaths' (expected the graph's root death mode throughout)"
[[ "$(one_line '^s12-corpse ' s12-corpse)" =~ ^s12-corpse\ live_body=1\ clip=knight_a_knight_death\ t=6\.000\ held=1\ .*\ return=Idle$ ]] ||
  fail "the corpse did not stay as his held death frame"
bound_line="$(one_line '^s12-bound ' s12-bound)"
bound_max="$(field "$bound_line" max)"
cmp_ok "$bound_max" "<=" 36 || fail "his root left his 36 m disc (Review Focus 5): $bound_line"
[[ "$(one_line '^s12-reset ' s12-reset)" = "s12-reset health=170 defeated=0 mode=Knight_route_patrol clip=knight_a_stand at_base=1 player_hp=100 pmode=grounded death=Idle" ]] ||
  fail "R did not bring Gaius back to his opening state"
perf_line="$(one_line '^s12-perf ' s12-perf)"
[[ "$perf_line" =~ ^s12-perf\ frames=240\ frame_ms=[0-9]+\.[0-9]+\ drawn=[0-9]+\ culled=[0-9]+$ ]] ||
  fail "s12-perf is malformed: $perf_line"
perf_frame="$(field "$perf_line" frame_ms)"
cmp_ok "$perf_frame" ">" 0 && cmp_ok "$perf_frame" "<=" 16.7 ||
  fail "the arena_03 frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
[[ "$(one_line '^s12-status ' s12-status)" =~ ^s12-status\ phases_done=10\  ]] ||
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
for name in blade head belly death; do
  path="$stills_dir/$name.png"
  [[ "$(grep -c -E "^s1-screenshot path=$path width=2560 height=1440\$" <<<"$selftest_out" || true)" = "1" ]] ||
    fail "still $name: expected one 2560x1440 s1-screenshot line for $path"
  row="$(check_png "$name" "$path")"
  stills="${stills}${stills:+ }$row"
done

if [[ -z "${VERIFY_QUICK:-}" ]]; then   # --quick skips the e2e run
  stamp "s12-e2e: the interactive loop from the arena_03 spawn to his death and R"
  if ! e2e_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s12-e2e 2>&1)"; then
    printf '%s\n' "$e2e_out" >&2
    fail "--s12-e2e exited non-zero"
  fi
  printf '%s\n' "$e2e_out"
  [[ "$(grep -c -E '^s12e2e-done frames=[0-9]+ ' <<<"$e2e_out" || true)" = "1" ]] || fail "no s12e2e-done line"
  grep -q -E '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' <<<"$e2e_out" ||
    fail "the e2e printed no s1-status"
  # returnWired is off for a non-Valus encounter: the loop loads the arena once and never the shrine.
  e2e_loads="$(grep -E '^s7-load ' <<<"$e2e_out" | awk '{ print $2 }' | tr '\n' ' ' || true)"
  [[ "$e2e_loads" = "stage=arena " ]] ||
    fail "the e2e loaded '$e2e_loads' (expected the arena once: no shrine after his death)"
  # The interactive loop played the selftest's fight: the same lines, the timings aside.
  self_lines="$(grep -E '^s12-' <<<"$selftest_out" | grep -v -E '^s12-(load|perf|status) ' | sed 's/^s12-//' || true)"
  e2e_lines="$(grep -E '^s12e2e-' <<<"$e2e_out" | grep -v -E '^s12e2e-(load|done) ' | sed 's/^s12e2e-//' || true)"
  [[ -n "$self_lines" && "$self_lines" = "$e2e_lines" ]] ||
    fail "the e2e's lines differ from the selftest's: $(diff <(printf '%s\n' "$self_lines") <(printf '%s\n' "$e2e_lines") | head -6 | tr '\n' ' ')"
fi
}

run_section "$1"
