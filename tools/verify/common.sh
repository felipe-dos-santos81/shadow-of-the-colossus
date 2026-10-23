# common.sh — sourced by the per-milestone adapters tools/verify/sN.sh (design D2, §13.1).
#
# An adapter is a SECOND COPY of its frozen scripts/sN-acceptance.sh's assertions, said plainly: the
# viewer invocations, the bash gates and their helpers are copied verbatim (test_coverage.py proves
# every frozen gate condition is present), the builds/suites/nested regressions are dropped (run.py
# runs them once), and the `<<'PY'` heredocs are NOT copied — heredoc_py extracts them from the frozen
# script at run time, so the pixel/cross-check code keeps one source.
#
# run.py calls `bash tools/verify/sN.sh <section>` (probes | audio | gpu) with:
#   VERIFY_MS     the milestone (s3 … s8)          VERIFY_OUT   this section's work dir (out_dir)
#   VERIFY_CAND   this section's golden candidates VERIFY_RUN   this invocation's scratch (cleaned per run)
#   VERIFY_BIN    the runner-published bp-convert  VERIFY_QUICK 1 = skip the e2e runs (--quick)
#   SOTC_LIB, SOTC_GAME_ROOT as the frozen scripts read them.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
lib="${SOTC_LIB:-$repo_root/out/library}"
game_root="${SOTC_GAME_ROOT:-}"
build_dir="$repo_root/app/build"
viewer_bin="$build_dir/sotc-viewer"
viewer=sotc_viewer   # the frozen code's `"$viewer" …` calls resolve to the function below
player="models/shadow/characters/wander/_cmn/wander.glb"
cli="$VERIFY_BIN/bp-convert.dll"
ms_dir="$(dirname "$VERIFY_OUT")"   # out/verify/<worktree>-<hash>/<sN>; sections share it
out_dir="$VERIFY_OUT"
mkdir -p "$VERIFY_OUT" "$VERIFY_CAND" "$VERIFY_RUN/cache"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "verify $VERIFY_MS: $*" >&2; exit 1; }

# The frozen s42+ code runs `caffeinate -dimsu "$viewer" …`; a real caffeinate cannot exec a shell
# function, so this passes through and sotc_viewer (and scripts/verify itself) keep the machine awake.
caffeinate() {
  [[ "${1:-}" = -dimsu ]] && shift
  "$@"
}

# keep NAME TEXT — a golden candidate; run.py normalises paths, masks timings and diffs it.
keep() {
  printf '%s\n' "$2" > "$VERIFY_CAND/$1.txt"
}

# sotc_viewer ARGS… — one viewer run under caffeinate. Its output is printed (for the frozen code's
# $(…) capture) and kept as a candidate named after its arguments, headed by `# run: ARGS`.
# An identical argument list already run in this invocation is replayed, not re-run (s42's plain
# selftest is s5's audio-off run); perf-pair runs (--frames 240) always run.
sotc_viewer() {
  local key cache slug arg skip="" status=0 name n=1
  key="$(printf '%s\n' "$@" | shasum -a 256 | cut -c1-16)"
  cache="$VERIFY_RUN/cache/$key"
  slug=""
  for arg in "$@"; do
    if [[ -n "$skip" ]]; then skip=""; continue; fi
    case "$arg" in
      --library | --model) skip=1; continue ;;
      */*) arg="${arg##*/}"; arg="${arg%.*}" ;;
    esac
    slug="${slug}${slug:+_}${arg#--}"
  done
  slug="$(printf '%s' "$slug" | tr -c 'A-Za-z0-9._-' '-')"
  name="$slug"
  while [[ -e "$VERIFY_CAND/$name.txt" ]]; do n=$((n + 1)); name="$slug-$n"; done
  case " $* " in *" --frames 240 "*) rm -f "$cache.status" ;; esac
  if [[ ! -f "$cache.status" ]]; then
    command caffeinate -dimsu "$viewer_bin" "$@" > "$cache.out" 2>&1 || status=$?
    printf '%s' "$status" > "$cache.status"
  fi
  status="$(cat "$cache.status")"
  { printf '# run: %s\n' "$*"; cat "$cache.out"; } > "$VERIFY_CAND/$name.txt"
  cat "$cache.out"
  return "$status"
}

# heredoc_py SCRIPT N — the Nth <<'PY' heredoc of the frozen scripts/SCRIPT-acceptance.sh, written to
# a file under $VERIFY_RUN; prints its path.
heredoc_py() {
  local src="$repo_root/scripts/$1-acceptance.sh" dst="$VERIFY_RUN/$1-heredoc-$2.py"
  awk -v n="$2" '
    body && $0 == "PY" { exit }
    body { print; next }
    index($0, "<<'\''PY'\''") { if (++k == n) body = 1 }
  ' "$src" > "$dst"
  [[ -s "$dst" ]] || fail "no heredoc $2 in $src"
  printf '%s' "$dst"
}

# norm_source FILE — FILE with the `# source:` comment's worktree prefix normalised (compose-encounter
# records the composing worktree's absolute path there).
norm_source() {
  sed 's|^\(# source: \).*/\(out/re/stage/\)|\1<REPO>/\2|' "$1"
}

run_section() {
  declare -F "section_$1" > /dev/null || fail "no section '$1' in $VERIFY_MS"
  "section_$1"
}

# checked NAME CMD… — a frozen script's plain `set -e` check (a probe pin, a compose, a gate script):
# run it, print it, keep its output as a candidate, and fail loudly on a non-zero exit.
checked() {
  local name="$1" out status=0
  shift
  out="$("$@" 2>&1)" || status=$?
  printf '%s\n' "$out"
  keep "$name" "$out"
  [[ "$status" = 0 ]] || fail "$name: '$*' exited $status"
}
