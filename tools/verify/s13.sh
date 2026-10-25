#!/usr/bin/env bash
# s13.sh — the runner's adapter for S13 (Phaedra): a second copy of scripts/s13-acceptance.sh's
# assertions, like s3…s12 (the frozen S12 chain and the build are run.py's; the gates are verbatim).
#   gpu: the preflight (arena_04.manifest selects Phaedra; her bake; the 16 colossus exports, KNIGHT's
#        13/53/40/171 with control_defs and KIRIN's 15/84/97/314; her routes, terrain and events
#        sidecar valid; the coverage report with KIRIN 0 missing), the four flag refusals + the --stage
#        refusal (exit 2), the selftest + four 2560x1440 stills (the timeline's gates, the coverage,
#        the perf budget, the pixel spans), the e2e (skipped by --quick): one arena load and no
#        shrine, and its lines equal the selftest's.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

arena04_manifest="$repo_root/out/re/stage/arena_04.manifest"
kirin_dir="$lib/models/shadow/characters/colossus_04_kirin/_cmn"
kirin_clips_json="$kirin_dir/c04_kirin.clips.json"
kirin_events_json="$kirin_dir/c04_kirin.events.json"
kirin_graph_json="$lib/behaviour/KIRIN.graph.json"
knight_graph_json="$lib/behaviour/KNIGHT.graph.json"
kirin_route_json="$lib/behaviour/kirin.route.json"
arena04_terrain_json="$lib/stage/arena_04.terrain.json"
stills_dir="$out_dir/stills"
coverage_dump="$VERIFY_RUN/scr_registry.dump"
coverage_md="$out_dir/coverage.md"

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
  [[ -f "$arena04_manifest" ]] ||
    fail "arena_04 manifest not found: $arena04_manifest (make arena04-manifest)"
  grep -q '^encounter kirin$' "$arena04_manifest" ||
    fail "$arena04_manifest does not select Phaedra (encounter kirin)"
  [[ -f "$kirin_clips_json" ]] || fail "Phaedra's clip list not found: $kirin_clips_json (make bake-kirin)"
  python3 -I -c 'import json, sys
c = json.load(open(sys.argv[1]))["clips"]
names = {x if isinstance(x, str) else x["name"] for x in c}
sys.exit(0 if len(names) == 75 and {"kirin_a_stand", "kirin_a_watch_loop", "kirin_a_death", "kirin_a_add_neck_down_2_loop"} <= names else 1)' "$kirin_clips_json" ||
    fail "$kirin_clips_json lacks her bake (75 clips: stand/watch_loop/death/add_neck_down_2_loop: make bake-kirin)"
  [[ -f "$kirin_events_json" ]] || fail "Phaedra's events sidecar not found: $kirin_events_json (make bake-kirin)"
  [[ -f "$kirin_graph_json" ]] || fail "Phaedra's exported graph not found: $kirin_graph_json (make behaviour)"
  # The export counts (spec §6, tools/re/scr PINNED): modes, conditions, transitions, edges.
  python3 -I -c 'import json, sys
g = json.load(open(sys.argv[1]))
n = (len(g["modes"]), len(g["conditions"]), sum(len(m["transitions"]) for m in g["modes"]), len(g["motion_graph"]))
sys.exit(0 if g["format"] == "sotc-scr-graph/0" and g["boss"] == "KIRIN" and n == (15, 84, 97, 314) else 1)' "$kirin_graph_json" ||
    fail "$kirin_graph_json is not KIRIN's export (15 modes, 84 conditions, 97 transitions, 314 motion edges: make behaviour)"
  python3 -I -c 'import json, sys
r = json.load(open(sys.argv[1]))
t = json.load(open(sys.argv[2]))
sys.exit(0 if r["format"] == "sotc-route/0" and len(r["routes"]) == 5 and len(r["gps"]) == 11
         and t["format"] == "sotc-terrain/0" and t["stage_tris"] and t["boss_ground"] else 1)' "$kirin_route_json" "$arena04_terrain_json" ||
    fail "$kirin_route_json / $arena04_terrain_json are not her routes (sotc-route/0, 5 routes, 11 GPs) and arena terrain (sotc-terrain/0): make routes terrain"
  python3 -I -c 'import json, sys
e = json.load(open(sys.argv[1]))
sys.exit(0 if e["format"] == "sotc-objanim-events/1" and e["skeleton"] == "kirin_a" and e["frame"] == "glb"
         and "kirin_walk_dmg" in e["spawned"] and "kirin_a_watch_start" in e["clips"] else 1)' "$kirin_events_json" ||
    fail "$kirin_events_json is not a valid kirin_a sidecar (format/skeleton/frame, the walk damage spawn: make bake-kirin)"
  # All 16 colossi exported (spec D2): <lib>/behaviour/<BOSS>.graph.json, sotc-scr-graph/0, its own boss.
  python3 -I -c 'import json, os, sys
bosses = "MINOTOUR_A MAMMOTH KNIGHT KIRIN YAMORI_B BUDDHA MINOTOUR_B BIRD KAME EEL MINOTOUR_C LEO NARGA POSEIDON SNAKE CERBERUS_R".split()
path = lambda b: os.path.join(sys.argv[1], b + ".graph.json")
ok = lambda b: os.path.isfile(path(b)) and json.load(open(path(b)))["format"] == "sotc-scr-graph/0" and json.load(open(path(b)))["boss"] == b
sys.exit(0 if all(ok(b) for b in bosses) else 1)' "$lib/behaviour" ||
    fail "$lib/behaviour lacks one of the 16 colossus graphs (sotc-scr-graph/0, one per boss: make behaviour)"
  # Gaius's counts are S12's (spec §13.1: the new keys are additive).
  python3 -I -c 'import json, sys
g = json.load(open(sys.argv[1]))
n = (len(g["modes"]), len(g["conditions"]), sum(len(m["transitions"]) for m in g["modes"]), len(g["motion_graph"]))
sys.exit(0 if g["format"] == "sotc-scr-graph/0" and g["boss"] == "KNIGHT" and n == (13, 53, 40, 171) and g["control_defs"] else 1)' "$knight_graph_json" ||
    fail "$knight_graph_json is not KNIGHT's S13 export (13 modes, 53 conditions, 40 transitions, 171 motion edges, control_defs: make behaviour)"
  # make coverage: the per-boss interpreter report over the 16 exports; Phaedra's graph has 0 missing.
  "$build_dir/scr_registry_dump" > "$coverage_dump" &&
    python3 -I "$repo_root/tools/re/scr/coverage.py" "$lib/behaviour" "$coverage_dump" "$coverage_md" ||
    fail "the coverage report failed (make coverage: $build_dir/scr_registry_dump, tools/re/scr/coverage.py)"
  [[ "$(grep -c -E '^\| [A-Z_]+ \| [0-9]+/[0-9]+/[0-9]+ \| ' "$coverage_md" || true)" = "16" ]] ||
    fail "$coverage_md does not have one row per colossus (16)"
  grep -q -E '^\| KIRIN \| ([0-9]+/[0-9]+/0 \| ){4}0 \|$' "$coverage_md" ||
    fail "KIRIN's graph uses an item the interpreter does not know: $(grep -E '^\| KIRIN ' "$coverage_md" || true)"
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
  grep -q -- '--s13-' <<<"$err" || fail "$label: exit 2 without an --s13-* message: $err"
  printf 's13: refused (exit 2) %s\n' "$label"
}
base_args=(--library "$lib" --manifest "$arena04_manifest" --model "$player")
refused "--s13-stills without --s13-selftest" "${base_args[@]}" --s13-stills "$stills_dir"
refused "--s13-selftest with --s13-e2e" "${base_args[@]}" --s13-selftest --s13-e2e
refused "--s13-e2e with --stage shrine" "${base_args[@]}" --stage shrine --s13-e2e
refused "--s13-selftest without --manifest" --library "$lib" --model "$player" --s13-selftest
# --stage with the selftest meets main.cpp's general rule first (a frozen-style selftest).
stage_rc=0
stage_err="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --stage plain --s13-selftest 2>&1)" || stage_rc=$?
[[ "$stage_rc" = "2" ]] && grep -q -- '--stage plain is an interactive start' <<<"$stage_err" ||
  fail "--s13-selftest with --stage plain: exit $stage_rc (expected the usage refusal, exit 2): $stage_err"

stamp "s13-selftest + stills: $arena04_manifest"
if ! selftest_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s13-selftest \
  --s13-stills "$stills_dir" 2>&1)"; then
  printf '%s\n' "$selftest_out" >&2
  fail "--s13-selftest --s13-stills exited non-zero"
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

# The timeline's events in order (the mode/route/climb/strike lines in between vary with the fight).
s13_events="$(grep -E '^s13-(load|stomp|dodge|hide|peep|entry|shake_hung|neck|route_climb|shake|sigil|death|corpse|reset|coverage|perf|status) ' <<<"$selftest_out" | awk '{ print $1 }' | tr '\n' ' ' || true)"
[[ "$s13_events" = "s13-load s13-stomp s13-dodge s13-hide s13-peep s13-entry s13-shake_hung s13-neck s13-neck s13-route_climb s13-shake s13-sigil s13-death s13-death s13-death s13-corpse s13-reset s13-coverage s13-perf s13-status " ]] ||
  fail "the s13-* events are '$s13_events' (expected the timeline's order)"

load_line="$(one_line '^s13-load ' s13-load)"
[[ "$load_line" =~ ^s13-load\ stage=arena\ encounter=kirin\ meshes=[0-9]+\ ground=[0-9]+x[0-9]+\ layers=64\ .*\ return=0\ graph=KIRIN\ modes=15\ edges=314\ add_edges=74\ routes=5\ gps=11\ nodes=52\ terrain=[0-9]+\ boss_ground=[0-9]+\ start=Kirin_withdrawal_return_mode\ clip=kirin_a_stand\ ms=[0-9]+\.[0-9]$ ]] ||
  fail "s13-load is malformed (her graph from its retry start, routes, terrain, a layered ground, no return): $load_line"
# The graph's own mode transitions, named: noticed (attack), the tunnel's GP, depth peeping, attached.
for t in 'from=Kirin_route_patrol to=Kirin_Normal_attack_mode' 'to=Kirin_underpass_GP_mode' \
  'from=Kirin_underpass_GP_mode to=Kirin_underpass_included_peeping_\(depth\)_mode' \
  'from=Kirin_underpass_included_peeping_\(depth\)_mode to=Kirin_parent_mode' 'to=Giraffe_death'; do
  grep -q -E "^s13-mode .*$t " <<<"$selftest_out" || fail "no mode transition '$t'"
done
[[ "$(one_line '^s13-stomp ' s13-stomp)" =~ ^s13-stomp\ clip=kirin_a_(atack_leg_[a-z_]+|threat)\ mode=Kirin_Normal_attack_mode\  ]] ||
  fail "her first attack is not a stomp from her attack mode"
# The dodge: no stomp damage volume (posed, his and the spawned) overlapped the player.
dodge_line="$(one_line '^s13-dodge ' s13-dodge)"
[[ "$dodge_line" =~ ^s13-dodge\ clip=kirin_a_[a-z_]+\ rows=0\ off_line=[0-9.]+\ nearest=[0-9.]+\ player_hp=100\  ]] ||
  fail "a stomp's volumes reached the player during the dodge: $dodge_line"
# The tunnel: the west stair (terrain class 7), route 4, its inner GP 0, depth peeping on the ruins.
[[ "$(one_line '^s13-hide ' s13-hide)" =~ ^s13-hide\ class=7\  ]] || fail "the player did not hide in the class-7 stair"
grep -q -E '^s13-route event=pick route=4 gp=0 kind=8 mode=Kirin_underpass_GP_mode ' <<<"$selftest_out" ||
  fail "she did not route to tunnel 4's inner GP 0"
[[ "$(one_line '^s13-route event=arrive ' s13-route-arrive)" =~ ^s13-route\ event=arrive\ route=4\ gp=0\ kind=8\ .*\ arrived=[0-9.]+\  ]] ||
  fail "she did not arrive at tunnel 4's inner GP 0"
peep_line="$(one_line '^s13-peep ' s13-peep)"
[[ "$peep_line" =~ ^s13-peep\ mode=Kirin_underpass_included_peeping_\(depth\)_mode\ route=4\ gp=0\ ground=0x400000\ arrived=[0-9.]+\ clip=kirin_a_watch_ ]] ||
  fail "she did not peep deep (mode 12) from GP 0 on the ruins' boss ground: $peep_line"
# The entry: the tail, grabbed inside a watch_* window (30 fps frames, R-AD), no footstep hit.
entry_line="$(one_line '^s13-entry ' s13-entry)"
[[ "$entry_line" =~ ^s13-entry\ kind=tail\ band=tail\ clip=kirin_a_watch_(start|loop|end)\ frame=[0-9]+\ .*\ player_hp=100\  ]] ||
  fail "the tail was not grabbed in a watch_* window: $entry_line"
necks="$(grep -E '^s13-neck ' <<<"$selftest_out" | sed 's/^s13-neck stage=\([0-9]\) .* layers=\([^ ]*\) flags=\([0-9,]*\) .*/\1:\3/' | tr '\n' ' ' || true)"
[[ "$necks" = "1:1,0 2:1,1 " ]] || fail "the neck-down stages were '$necks' (expected 1:1,0 2:1,1)"
grep -E '^s13-neck stage=1 ' <<<"$selftest_out" | grep -q 'kirin_a_add_neck_down_1' ||
  fail "neck down 1's additive clip is not playing at stage 1"
grep -E '^s13-neck stage=2 ' <<<"$selftest_out" | grep -q 'kirin_a_add_neck_down_2' ||
  fail "neck down 2's additive clip is not playing at stage 2"
route_line="$(one_line '^s13-route_climb ' s13-route_climb)"
[[ "$route_line" =~ ^s13-route_climb\ bands=tail,back,withers,neck_low,.*neck_high,head\  ]] ||
  fail "the climb did not follow the band chain from the tail to the head"
# After the peep her root's ground hands off the ruins' boss ground (during an unsteered kind-1 walk,
# ruling R-AL: the step off it is possible, not a chosen exit).
[[ "$(one_line '^s13-ground_handoff ' s13-ground_handoff)" =~ ^s13-ground_handoff\ from=0x400000\ to=0x[0-9a-f]+\  ]] ||
  fail "her root never handed off the ruins' boss ground after the peep"
# The hung neck: a shake met there drains the grip (no regeneration on a hung band).
hung_line="$(one_line '^s13-shake_hung ' s13-shake_hung)"
[[ "$hung_line" =~ ^s13-shake_hung\ clip=kirin_a_[a-z0-9_]*refuse[a-z_]*\ band=neck_low\ drain=[1-9][0-9.]*\  ]] ||
  fail "no shake was met on the hung neck: $hung_line"
hung_loss="$(field "$hung_line" loss)"
cmp_ok "$hung_loss" ">" 0 || fail "the hung-neck shake cost no grip: $hung_line"
shake_line="$(one_line '^s13-shake ' s13-shake)"
[[ "$shake_line" =~ ^s13-shake\ survived=1\ clip=kirin_a_[a-z0-9_]*refuse[a-z_]*\ band=head\ drain=[1-9][0-9.]*\ start=[0-9.]+\ low=[0-9.]+\ end=[0-9.]+\ loss=-?[0-9.]+\  ]] ||
  fail "no shake-off (with its measured drain) was survived on the head: $shake_line"
[[ "$(one_line '^s13-sigil ' s13-sigil)" =~ ^s13-sigil\ target=head\ strikes=[0-9]+\ spent=160\ health=0\  ]] ||
  fail "the head sigil did not take her 160 HP"
[[ "$(grep -E '^s13-strike ' <<<"$selftest_out" | tail -1)" =~ \ health=0\ killed=1\  ]] || fail "the last strike did not kill her"
deaths="$(grep -E '^s13-death ' <<<"$selftest_out" | sed 's/^s13-death phase=\([A-Za-z]*\) .* mode=\([A-Za-z_]*\) .* lift=\([0-9.]*\)$/\1:\2:\3/' | tr '\n' ' ' || true)"
[[ "$deaths" = "Motion:Giraffe_death:9.86 Collapse:Giraffe_death:2.44 Done:Giraffe_death:2.44 " ]] ||
  fail "the death phases are '$deaths' (expected the graph's death mode and the 9.86 -> 2.44 m root drop)"
[[ "$(one_line '^s13-corpse ' s13-corpse)" =~ ^s13-corpse\ live_body=1\ clip=kirin_a_death_loop\ held=1\ lift=2\.44\ .*\ return=Idle$ ]] ||
  fail "the corpse did not stay as her held death_loop at the drop's end"
[[ "$(one_line '^s13-reset ' s13-reset)" = "s13-reset health=160 defeated=0 mode=Kirin_withdrawal_return_mode clip=kirin_a_stand at_base=1 layers=none player_hp=100 pmode=grounded death=Idle" ]] ||
  fail "R did not bring Phaedra back to her opening state"
[[ "$(one_line '^s13-coverage ' s13-coverage)" =~ ^s13-coverage\ boss=KIRIN\ .*\ total=[0-9]+/[0-9]+/0$ ]] ||
  fail "her graph uses an item her registry does not know"
perf_line="$(one_line '^s13-perf ' s13-perf)"
[[ "$perf_line" =~ ^s13-perf\ frames=240\ frame_ms=[0-9]+\.[0-9]+\ drawn=[0-9]+\ culled=[0-9]+$ ]] ||
  fail "s13-perf is malformed: $perf_line"
perf_frame="$(field "$perf_line" frame_ms)"
cmp_ok "$perf_frame" ">" 0 && cmp_ok "$perf_frame" "<=" 16.7 ||
  fail "the arena_04 frame costs ${perf_frame} ms (outside the 60 fps budget of 16.7 ms)"
[[ "$(one_line '^s13-status ' s13-status)" =~ ^s13-status\ phases_done=12\  ]] ||
  fail "the selftest did not reach its last phase (R)"
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
for name in peeping climb neck_down_2 death; do
  path="$stills_dir/$name.png"
  [[ "$(grep -c -E "^s1-screenshot path=$path width=2560 height=1440\$" <<<"$selftest_out" || true)" = "1" ]] ||
    fail "still $name: expected one 2560x1440 s1-screenshot line for $path"
  row="$(check_png "$name" "$path")"
  stills="${stills}${stills:+ }$row"
done

if [[ -z "${VERIFY_QUICK:-}" ]]; then   # --quick skips the e2e run
  stamp "s13-e2e: the interactive loop from the arena_04 start to her death and R"
  if ! e2e_out="$(caffeinate -dimsu "$viewer" "${base_args[@]}" --s13-e2e 2>&1)"; then
    printf '%s\n' "$e2e_out" >&2
    fail "--s13-e2e exited non-zero"
  fi
  printf '%s\n' "$e2e_out"
  [[ "$(grep -c -E '^s13e2e-done frames=[0-9]+ ' <<<"$e2e_out" || true)" = "1" ]] || fail "no s13e2e-done line"
  grep -q -E '^s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0 ' <<<"$e2e_out" ||
    fail "the e2e printed no s1-status"
  # returnWired is off for a non-Valus encounter: the loop loads the arena once and never the shrine.
  e2e_loads="$(grep -E '^s7-load ' <<<"$e2e_out" | awk '{ print $2 }' | tr '\n' ' ' || true)"
  [[ "$e2e_loads" = "stage=arena " ]] ||
    fail "the e2e loaded '$e2e_loads' (expected the arena once: no shrine after her death)"
  e2e_load_line="$(grep -m1 -E '^s13e2e-load ' <<<"$e2e_out" || true)"
  [[ "$e2e_load_line" =~ ^s13e2e-load\ stage=arena\ encounter=kirin\ .*\ return=0\ graph=KIRIN\ modes=15\ edges=314\ .*\ start=Kirin_withdrawal_return_mode\ clip=kirin_a_stand\ ms=[0-9]+\.[0-9]$ ]] ||
    fail "s13e2e-load is malformed (Phaedra's arena and graph, no return): $e2e_load_line"
  # The interactive loop played the selftest's fight: the same lines, the timings aside.
  self_lines="$(grep -E '^s13-' <<<"$selftest_out" | grep -v -E '^s13-(load|perf|status) ' | sed 's/^s13-//' || true)"
  e2e_lines="$(grep -E '^s13e2e-' <<<"$e2e_out" | grep -v -E '^s13e2e-(load|done) ' | sed 's/^s13e2e-//' || true)"
  [[ -n "$self_lines" && "$self_lines" = "$e2e_lines" ]] ||
    fail "the e2e's lines differ from the selftest's: $(diff <(printf '%s\n' "$self_lines") <(printf '%s\n' "$e2e_lines") | head -6 | tr '\n' ' ')"
fi
}

run_section "$1"
