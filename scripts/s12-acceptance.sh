#!/usr/bin/env bash
#
# s12-acceptance.sh — the S12 "Gaius" acceptance pass, re-runnable.
#
# Steps (the milestone: the third colossus on arena_03, run by his exported
# behaviour graph and his objanim events sidecar — dodge his slam, grab the
# stuck blade, ride it up onto the forearm, climb to the head sigil, hold
# through the shake-off, the belly sigil, his death; his fight is silent, D10):
#
#   1. (preflight) the inputs: SOTC_GAME_ROOT, the library with Gaius's bake
#      (his clips.json: knight_a_stand/atk1_3/knight_death), Wander;
#      out/re/stage/arena_03.manifest is composed if absent (probe.py compose
#      --stage arena_03) and must select Gaius (`encounter knight`); the
#      exported graph <lib>/behaviour/KNIGHT.graph.json (make behaviour) holds
#      KNIGHT's counts — 13 modes, 53 conditions, 40 transitions, 171
#      motion-graph edges; the events sidecar colossus_03_knight.events.json
#      (make bake-knight) is a valid sotc-objanim-events/1 file for knight_a in
#      the glb frame, under 64 KB, with the pinned rows (atk1 SWORD_ATK1_DMG,
#      atk1_3 ATK_SHOCK2 f0–94, walk FOOT_R [40, 42] -> knight_walk_dmg);
#   2. the **frozen S11 acceptance first** (`scripts/s11-acceptance.sh`, never
#      edited — exactly `make verify-long`'s chain before S12: it runs the frozen
#      S9 chain — both builds, both suites, S8 → S7 → S6 → S5 → S4.2 → S4.1 → S4
#      → S3 — then S11). Its outputs are not re-asserted here beyond its PASS line;
#   3. the flag refusals (exit 2): --s12-stills without --s12-selftest,
#      --s12-selftest with --s12-e2e, --s12-e2e with --stage shrine,
#      --s12-selftest without --manifest, --stage plain with --s12-selftest;
#   4. the deterministic proof — `--s12-selftest --s12-stills out/s12/stills`:
#      the events in order (load, judge, slam, dodge, blade, route, sigil,
#      shake, sigil, the three death phases, corpse, bound, reset, hit, perf,
#      status); the graph from its retry start (route_patrol) and its named
#      mode transitions; the slam chosen with its rays clear; the dodge clears
#      every slam row by overlap, off the line by at least the posed reach; the
#      blade grabbed in its window and ridden up onto the forearm in one slam
#      (rulings R-J, R-M); the band chain to the head; the head sigil then the
#      belly's (84 then 0), the last strike kills; a shake survived; the death
#      in the graph's root death mode, his held death frame (return=Idle); his
#      root inside the 36 m disc; R exact; one damage row landing in the
#      exposure (ruling R-L); the perf line <= 16.7 ms; phases_done=11;
#      windowed, cursor free;
#   5. the four stills (blade, head, belly, death): one 2560x1440 s1-screenshot
#      line each, PNG magic, > 5000 B, the viewer's channel span >= 30;
#   6. the end-to-end loop — `--s12-e2e`: the interactive loop at a fixed
#      1/60 s with scripted input; one arena load and no shrine load; its
#      s12e2e-* lines equal the selftest's s12-* lines (load/perf/status aside);
#   7. a summary block with every measured number.
#
# tools/verify/s12.sh is the runner's second copy of steps 1 and 3–6
# (test_coverage.py proves every gate below is in it). Every viewer and
# acceptance run is wrapped in `caffeinate -dimsu`. Never run two acceptances
# concurrently. The S11 chain takes ~15 min. CoreAudio refuses output to a
# background (non-Aqua) session: run from a desktop terminal, or the frozen
# e2e runs print an `audio: no output device` line.
#
# Environment:
#   SOTC_GAME_ROOT  the extracted game tree (required — never silently skipped)
#   SOTC_LIB        the converted asset library (default: <repo>/out/library;
#                   the branch ran it on its view out/libviews/s12-gaius, R-K)
#
# Identity is not scriptable: the lines prove the wiring, not that the fight
# feels like the game's. docs/notes/s12-results.md records every measured
# number; the user's walk checklist is PENDING there.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer="$build_dir/sotc-viewer"
probe="$repo_root/tools/re/stage/probe.py"
arena03_manifest="$repo_root/out/re/stage/arena_03.manifest"
knight_dir="$lib/models/shadow/characters/colossus_03_knight/_cmn"
knight_clips_json="$knight_dir/colossus_03_knight.clips.json"
knight_events_json="$knight_dir/colossus_03_knight.events.json"
knight_graph_json="$lib/behaviour/KNIGHT.graph.json"
player="models/shadow/characters/wander/_cmn/wander.glb"
out_dir="$repo_root/out/s12"
stills_dir="$out_dir/stills"
selftest_txt="$out_dir/s12-selftest.txt"
e2e_txt="$out_dir/s12-e2e.txt"
run_log="$repo_root/out/s12-acceptance-run.log"
s11_log="$repo_root/out/s12-s11-regression.log"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "s12-acceptance: FAIL — $*" >&2; exit 1; }

mkdir -p "$repo_root/out"
exec > >(tee "$run_log") 2>&1

need() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

for c in dotnet cmake ctest caffeinate shasum awk grep sed sort tee tr wc python3 od cmp diff cut uniq; do
  need "$c"
done

# --- Step 1: the inputs (before the ~15 min S11 chain) ------------------------

[[ -n "$game_root" ]] || fail "SOTC_GAME_ROOT is not set"
[[ -d "$game_root" ]] || fail "SOTC_GAME_ROOT not found: $game_root"
[[ -d "$lib" ]] || fail "asset library not found: $lib (set SOTC_LIB)"
[[ -f "$lib/$player" ]] || fail "player glb not found under the library: $lib/$player"
if [[ ! -f "$arena03_manifest" ]]; then
  stamp "compose: arena_03 -> $arena03_manifest"
  python3 "$probe" compose --stage arena_03 --library "$lib" --out "$arena03_manifest"
fi
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
# The export counts (spec D2; tools/re/scr/test_graph_export.py PINNED): modes, conditions, transitions, edges.
python3 -I -c 'import json, sys
g = json.load(open(sys.argv[1]))
n = (len(g["modes"]), len(g["conditions"]), sum(len(m["transitions"]) for m in g["modes"]), len(g["motion_graph"]))
sys.exit(0 if g["format"] == "sotc-scr-graph/0" and g["boss"] == "KNIGHT" and n == (13, 53, 40, 171) else 1)' "$knight_graph_json" ||
  fail "$knight_graph_json is not KNIGHT's export (13 modes, 53 conditions, 40 transitions, 171 motion edges: make behaviour)"
# The sidecar (plan Task 3): its format, skeleton and frame, < 64 KB, and the pinned rows.
python3 -I -c 'import json, os, sys
e = json.load(open(sys.argv[1]))
c = e["clips"]
row = lambda clip, kind, name: [r for r in c[clip][kind] if r["name"] == name]
foot = row("knight_a_walk", "effects", "KNIGHT_FOOT_R")
shock = row("knight_a_atk1_3", "damage", "KNIGHT_ATK_SHOCK2")
sys.exit(0 if e["format"] == "sotc-objanim-events/1" and e["skeleton"] == "knight_a" and e["frame"] == "glb"
         and os.path.getsize(sys.argv[1]) < 65536 and row("knight_a_atk1", "damage", "KNIGHT_SWORD_ATK1_DMG")
         and shock and shock[0]["windows"] == [[0, 94]] and foot and foot[0]["events"] == [[40, 42]]
         and foot[0]["spawns"] == ["knight_walk_dmg"] and "knight_walk_dmg" in e["spawned"] and e["judgeDefaults"] else 1)' "$knight_events_json" ||
  fail "$knight_events_json is not a valid knight_a sidecar (format/skeleton/frame, < 64 KB, the pinned rows: make bake-knight)"

export SOTC_GAME_ROOT="$game_root"
export SOTC_LIB="$lib"
started=$SECONDS

# --- Step 2: the frozen S11 acceptance (the S9 chain, then S11) ----------------

stamp "s11 regression: scripts/s11-acceptance.sh (the S9 chain: builds, both suites, S8 … S3; then S11)"
if ! caffeinate -dimsu "$repo_root/scripts/s11-acceptance.sh" > "$s11_log" 2>&1; then
  tail -40 "$s11_log" >&2
  fail "the frozen S11 acceptance regression failed (log: $s11_log)"
fi
s11_pass_line="$(grep -m1 -E '^s11-acceptance: PASS — ' "$s11_log" || true)"
[[ -n "$s11_pass_line" ]] || fail "the S11 regression did not print its final PASS — line"
printf '%s\n' "$s11_pass_line" | cut -c1-200
s11_seconds=$((SECONDS - started))
[[ -x "$viewer" ]] || fail "the S11 chain did not build $viewer"

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
[[ "$s12_events" = "s12-load s12-judge s12-slam s12-dodge s12-blade s12-route s12-sigil s12-shake s12-sigil s12-death s12-death s12-death s12-corpse s12-bound s12-reset s12-hit s12-perf s12-status " ]] ||
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
# The one hit: his next slam after R walked into (ruling R-L; any earlier landed row fails the run).
[[ "$(one_line '^s12-hit ' s12-hit)" =~ ^s12-hit\ row=KNIGHT_[A-Z0-9_]+\ amount=[1-9][0-9]*\  ]] ||
  fail "no damage row landed on the player in the exposure"
# The dodge clears every slam row by overlap (not by i-frames), off the line by the posed reach.
dodge_line="$(one_line '^s12-dodge ' s12-dodge)"
[[ "$dodge_line" =~ ^s12-dodge\ sword_rows=0\ off_line=[0-9.]+\ need=[0-9.]+\ widest=KNIGHT_[A-Z0-9_]+\ reach=[0-9.]+\ player_hp=100\  ]] ||
  fail "the slam's rows reached the player during the dodge"
dodge_off="$(field "$dodge_line" off_line)"
dodge_need="$(field "$dodge_line" need)"
cmp_ok "$dodge_off" ">=" "$dodge_need" || fail "the dodge spot is inside the slam's posed reach: $dodge_line"
blade_line="$(one_line '^s12-blade ' s12-blade)"
[[ "$blade_line" =~ ^s12-blade\ window=9\.\.75\ grab_frame=[0-9]+\ close_frame=75\ close_t=0\.[5-9][0-9]\ ride_from=0\.50\ margin=0\.[0-9]+\ carried=1\ band=forearm\ slams=1\  ]] ||
  fail "the blade was not grabbed, walked onto its upper half and ridden up onto the forearm in one slam"
route_line="$(one_line '^s12-route ' s12-route)"
[[ "$route_line" =~ ^s12-route\ bands=blade,forearm,upper_arm,shoulder,neck,head\  ]] ||
  fail "the climb did not follow the band chain to the head"
sigils="$(grep -E '^s12-sigil ' <<<"$selftest_out" | sed 's/^s12-sigil done=\([a-z]*\) .* health=\([0-9]*\) .*/\1:\2/' | tr '\n' ' ' || true)"
[[ "$sigils" = "head:84 belly:0 " ]] || fail "the sigils were '$sigils' (expected head:84 belly:0)"
strikes="$(grep -E '^s12-strike ' <<<"$selftest_out" | sed 's/.*weakpoint=\([^ ]*\) .*/\1/' | uniq | tr '\n' ' ' || true)"
[[ "$strikes" = "weakpoint_01 weakpoint_02 " ]] || fail "the sigils were not struck head then belly: '$strikes'"
[[ "$(grep -E '^s12-strike ' <<<"$selftest_out" | tail -1)" =~ \ health=0\ killed=1\  ]] || fail "the last strike did not kill him"
shake_line="$(one_line '^s12-shake ' s12-shake)"
[[ "$shake_line" =~ ^s12-shake\ survived=1\ clip=knight_a_refuse_[a-z_]+\  ]] ||
  fail "no shake-off was survived"
deaths="$(grep -E '^s12-death ' <<<"$selftest_out" | sed 's/^s12-death phase=\([A-Za-z]*\) .* mode=\([A-Za-z_]*\) .*/\1:\2/' | tr '\n' ' ' || true)"
[[ "$deaths" = "Motion:Knight_death Collapse:Knight_death Done:Knight_death " ]] ||
  fail "the death phases are '$deaths' (expected the graph's root death mode throughout)"
corpse_line="$(one_line '^s12-corpse ' s12-corpse)"
[[ "$corpse_line" =~ ^s12-corpse\ live_body=1\ clip=knight_a_knight_death\ t=6\.000\ held=1\ .*\ return=Idle$ ]] ||
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
[[ "$(one_line '^s12-status ' s12-status)" =~ ^s12-status\ phases_done=11\  ]] ||
  fail "the selftest did not reach its last phase (the exposure)"
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
printf '%s\n' "$selftest_out" > "$selftest_txt"

# --- Step 6: the end-to-end loop ---------------------------------------------

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
e2e_load_line="$(grep -m1 -E '^s12e2e-load ' <<<"$e2e_out" || true)"
[[ "$e2e_load_line" =~ ^s12e2e-load\ stage=arena\ encounter=knight\ meshes=[0-9]+\ ground=[0-9]+x[0-9]+\ layers=64\ .*\ return=0\ graph=KNIGHT\ modes=13\ edges=171\ start=Knight_route_patrol\ clip=knight_a_stand\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s12e2e-load is malformed (Gaius's arena and graph, a layered ground, no return): $e2e_load_line"
# The interactive loop played the selftest's fight: the same lines, the timings aside.
self_lines="$(grep -E '^s12-' <<<"$selftest_out" | grep -v -E '^s12-(load|perf|status) ' | sed 's/^s12-//' || true)"
e2e_lines="$(grep -E '^s12e2e-' <<<"$e2e_out" | grep -v -E '^s12e2e-(load|done) ' | sed 's/^s12e2e-//' || true)"
[[ -n "$self_lines" && "$self_lines" = "$e2e_lines" ]] ||
  fail "the e2e's lines differ from the selftest's: $(diff <(printf '%s\n' "$self_lines") <(printf '%s\n' "$e2e_lines") | head -6 | tr '\n' ' ')"
printf '%s\n' "$e2e_out" > "$e2e_txt"

# --- Step 7: summary ----------------------------------------------------------

stamp "summary"
load_ms="$(field "$load_line" ms)"
load_ground="$(field "$load_line" ground)"
load_meshes="$(field "$load_line" meshes)"
blade_grab="$(field "$blade_line" grab_frame)"
blade_close="$(field "$blade_line" close_t)"
blade_margin="$(field "$blade_line" margin)"
route_stamina="$(field "$route_line" stamina)"
shake_low="$(field "$shake_line" low)"
corpse_moved="$(field "$corpse_line" moved)"
hit_line="$(one_line '^s12-hit ' s12-hit)"
hit_row="$(field "$hit_line" row)"
hit_amount="$(field "$hit_line" amount)"
perf_drawn="$(field "$perf_line" drawn)"
perf_culled="$(field "$perf_line" culled)"
status_line="$(one_line '^s12-status ' s12-status)"
status_frames="$(field "$status_line" frames)"
status_seconds="$(field "$status_line" seconds)"
e2e_done="$(grep -m1 -E '^s12e2e-done ' <<<"$e2e_out")"
e2e_load_ms="$(field "$e2e_load_line" ms)"
total_seconds=$((SECONDS - started))
printf 's12-acceptance: library      %s\n' "$lib"
printf 's12-acceptance: manifest     %s\n' "$arena03_manifest"
printf 's12-acceptance: s11 chain    %s s\n' "$s11_seconds"
printf 's12-acceptance: %s\n' "$load_line" "$dodge_line" "$blade_line" "$route_line" "$shake_line" \
  "$corpse_line" "$bound_line" "$hit_line" "$perf_line" "$status_line" "$e2e_done"
printf 's12-acceptance: stills       %s\n' "$stills"
printf 's12-acceptance: PASS — the frozen S11 acceptance green (%s s: the S9 chain — builds, both suites, S8 → … → S3 — then S11 inside it); KNIGHT export 13/53/40/171 and the knight_a sidecar valid; five flag refusals exit 2; arena_03 load %s ms (%s meshes, layered ground %s, no return); the graph from route_patrol; the slam dodged clear of every row (off_line %s >= %s); the blade grabbed at f%s and ridden up onto the forearm (close_t %s, margin %s) in one slam; the climb to the head (stamina %s); head sigil 84, a shake survived (low %s), belly sigil 0, the kill; the death in Knight_death with his held death frame (moved %s m) and no shrine; root max %s m of 36; R exact; the exposure hit %s (%s); frame %s ms (drawn %s, culled %s); %s frames / %s s; the e2e loop (%s; load %s ms) equal to the selftest, one arena load; four stills 2560x1440; windowed, cursor free; %s s in all\n' \
  "$s11_seconds" "$load_ms" "$load_meshes" "$load_ground" "$dodge_off" "$dodge_need" "$blade_grab" "$blade_close" \
  "$blade_margin" "$route_stamina" "$shake_low" "$corpse_moved" "$bound_max" "$hit_row" "$hit_amount" "$perf_frame" \
  "$perf_drawn" "$perf_culled" "$status_frames" "$status_seconds" "${e2e_done#s12e2e-done }" "$e2e_load_ms" "$total_seconds"
