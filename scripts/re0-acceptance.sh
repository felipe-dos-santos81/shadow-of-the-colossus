#!/usr/bin/env bash
#
# re0-acceptance.sh — the RE-0 acceptance pass: unwrap the retail eboot.bin (PS4 SELF) with the
# port's own prepare.py, verify the artifacts, and baseline them in a headless Ghidra project.
# Re-runnable end to end.
#
# Steps (RE-0 foundation: the unwrap everything downstream reads):
#
#   1. preflight — the inputs exist ($SOTC_UROOT/eboot.bin, the port's scripts/prepare.py, the
#      committed tools/re/ghidra/PrintSummary.java), python3 and java are on PATH, and the
#      pinned Ghidra bundle's analyzeHeadless is executable;
#   2. stage + unwrap — the extraction keeps PKG-level metadata in extracted/pkg-meta/ and the
#      PKG filesystem in extracted/uroot/, so its uroot carries no sce_sys/param.sfo (which
#      prepare.py requires): out/re/uroot is a staged view — symlinks to the read-only tree,
#      plus a copy of pkg-meta/param.sfo when the tree lacks one — and
#      `python3 $SOTC_PC_PORT/scripts/prepare.py out/re/uroot --out out/re` unwraps it (the
#      port's script prints TITLE | entry | image and the import/relocation counts);
#   3. verify — out/re/eboot.elf carries the \x7fELF magic and is >= 1 MiB; out/re/analysis.json
#      parses as JSON; the eboot.bin sha256 matches the extraction manifest's `uroot/eboot.bin`
#      line (a mismatch is dump drift — fail);
#   4. negative check (Review Focus #1) — the same prepare.py invocation on junk input must fail
#      with `expected PS4 SELF`; the check is silent on success;
#   5. headless Ghidra baseline (Review Focus #2/#3) — analyzeHeadless imports out/re/eboot.elf
#      into the project out/re/ghidra/sotc and runs tools/re/ghidra/PrintSummary.java, whose one
#      stable `summary program=… functions=…` line the script asserts with a floor of 1000 (a
#      truncated import is a failure, not a green run); the project is rebuilt every run, so
#      re-runs are deterministic. Ghidra's ProjectLocator rejects '.'-prefixed path elements
#      (this worktree lives under ~/.local), so the location is passed as a dot-free temp
#      symlink to out/re/ghidra. Headless prefixes script output with the script name and
#      appends its logger name (actual format:
#      `INFO  PrintSummary.java> summary … (GhidraScript)`), so the match is on the message body,
#      not the start of the line. The full log is out/re/ghidra.log; ERROR lines are counted and
#      printed, informational only. Analysis of a game eboot takes minutes.
#
# Inputs are read-only: the extracted uroot, the port repo, and the Ghidra bundle are never
# written to — prepare.py consumes the staged view under out/re/, not the tree. The only output
# is out/re/ (removed and regenerated on every run).
#
# Environment:
#   SOTC_UROOT          the extracted game tree (default: <repo>/../SOTC-extract/extracted/uroot)
#   SOTC_PC_PORT        the port repo carrying scripts/prepare.py
#                       (default: $HOME/code/mine/ShadowOfTheColossus-PC)
#   GHIDRA_HOME         the pinned Ghidra distribution
#                       (default: $HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC)
#   SOTC_UROOT_SHA256   the extraction manifest (default: <SOTC_UROOT>/../../logs/uroot.sha256,
#                       i.e. SOTC-extract/logs/uroot.sha256 — derived from SOTC_UROOT so the
#                       script works when the worktree sits outside the canonical checkout)
#
# Exit: 0 on success; != 0 with a one-line `re0-acceptance: <cause>` on stderr otherwise.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${SOTC_UROOT:=$repo_root/../SOTC-extract/extracted/uroot}"
: "${SOTC_PC_PORT:=$HOME/code/mine/ShadowOfTheColossus-PC}"
: "${GHIDRA_HOME:=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC}"
out="$repo_root/out/re"

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }
fail() { echo "re0-acceptance: $*" >&2; exit 1; }

started="$(date +%s)"

# SOTC_UROOT must be absolute (the staged symlinks point into it); the manifest default hangs
# off the resolved path: <uroot>/../../logs/uroot.sha256 = SOTC-extract/logs/uroot.sha256.
resolved="$(cd "$SOTC_UROOT" && pwd)" || fail "SOTC_UROOT is not a directory: $SOTC_UROOT"
SOTC_UROOT="$resolved"
: "${SOTC_UROOT_SHA256:=$(dirname "$(dirname "$SOTC_UROOT")")/logs/uroot.sha256}"

# --- Step 1: preflight --------------------------------------------------------

[[ -f "$SOTC_UROOT/eboot.bin" ]] || fail "input not found: $SOTC_UROOT/eboot.bin (set SOTC_UROOT)"
[[ -f "$SOTC_PC_PORT/scripts/prepare.py" ]] ||
  fail "port script not found: $SOTC_PC_PORT/scripts/prepare.py (set SOTC_PC_PORT)"
[[ -f "$repo_root/tools/re/ghidra/PrintSummary.java" ]] ||
  fail "Ghidra summary script not found: $repo_root/tools/re/ghidra/PrintSummary.java"
command -v python3 >/dev/null 2>&1 || fail "python3 not on PATH"
command -v java >/dev/null 2>&1 || fail "java not on PATH"
[[ -x "$GHIDRA_HOME/support/analyzeHeadless" ]] ||
  fail "headless analyzer not executable: $GHIDRA_HOME/support/analyzeHeadless (set GHIDRA_HOME)"

# --- Step 2: stage the input tree + unwrap eboot.bin (SELF -> ELF) ------------
#
# Stage a prepare.py-consumable view of the read-only tree under out/re/uroot: symlinks for
# every top-level entry, symlinks for sce_sys/*, plus a copy of the extraction's
# pkg-meta/param.sfo when the tree's sce_sys lacks one (the extraction workflow keeps
# PKG-level metadata in pkg-meta/, so its uroot has no param.sfo). A full tree (param.sfo
# already present) skips the copy. Rebuilt every run by the rm -rf below.

rm -rf "$out"
stage="$out/uroot"
stamp "stage: $stage <- $SOTC_UROOT"
mkdir -p "$stage/sce_sys"
for e in "$SOTC_UROOT"/*; do
  b="$(basename "$e")"
  if [[ "$b" != "sce_sys" ]]; then
    ln -s "$e" "$stage/$b"
  fi
done
if [[ -d "$SOTC_UROOT/sce_sys" ]]; then
  for e in "$SOTC_UROOT"/sce_sys/*; do
    [[ -e "$e" ]] || continue  # empty dir: the glob stayed literal
    ln -s "$e" "$stage/sce_sys/$(basename "$e")"
  done
fi
if [[ ! -f "$stage/sce_sys/param.sfo" ]]; then
  sfo="$(dirname "$SOTC_UROOT")/pkg-meta/param.sfo"
  [[ -f "$sfo" ]] || fail "param.sfo not found: neither $SOTC_UROOT/sce_sys/param.sfo nor $sfo exists"
  cp "$sfo" "$stage/sce_sys/param.sfo"
  stamp "stage: param.sfo <- $sfo (uroot lacks one)"
fi
stamp "unwrap: $SOTC_PC_PORT/scripts/prepare.py $stage --out $out"
# Captured (then printed) so the summary block can quote prepare.py's own entry/image and
# imports/relocations lines; stderr still streams and a failure still exits with its message.
prepare_out="$(python3 "$SOTC_PC_PORT/scripts/prepare.py" "$stage" --out "$out")"
printf '%s\n' "$prepare_out"
prepare_entry_line="$(printf '%s\n' "$prepare_out" | grep -m1 'entry=' || true)"
prepare_imports_line="$(printf '%s\n' "$prepare_out" | grep -m1 'imported symbols' || true)"

# --- Step 3: artifact verification + digest cross-check -----------------------

[[ -f "$out/eboot.elf" ]] || fail "prepare.py did not write $out/eboot.elf"
magic="$(head -c4 "$out/eboot.elf" | od -An -tx1 | tr -d ' \n')"
[[ "$magic" = "7f454c46" ]] || fail "$out/eboot.elf lacks the ELF magic (got $magic)"
elf_bytes="$(wc -c < "$out/eboot.elf" | tr -d '[:space:]')"
[[ "$elf_bytes" -ge 1048576 ]] || fail "$out/eboot.elf is only $elf_bytes bytes (need >= 1 MiB)"

[[ -f "$out/analysis.json" ]] || fail "prepare.py did not write $out/analysis.json"
python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$out/analysis.json" ||
  fail "$out/analysis.json is not valid JSON"

# The digest cross-check ties the input to the extraction manifest: a mismatch means the dump
# under SOTC_UROOT is not the one the extraction was recorded from (dump drift).
[[ -f "$SOTC_UROOT_SHA256" ]] ||
  fail "digest manifest not found: $SOTC_UROOT_SHA256 (set SOTC_UROOT_SHA256)"
manifest_line="$(awk '$2 == "uroot/eboot.bin" { print; exit }' "$SOTC_UROOT_SHA256")"
[[ -n "$manifest_line" ]] || fail "no 'uroot/eboot.bin' line in $SOTC_UROOT_SHA256"
shasum_line="$(shasum -a 256 "$SOTC_UROOT/eboot.bin")"
printf '%s\n' "$shasum_line"
actual_hash="${shasum_line%% *}"
manifest_hash="${manifest_line%% *}"
[[ "$actual_hash" = "$manifest_hash" ]] ||
  fail "dump drift: eboot.bin sha256 $actual_hash != manifest $manifest_hash ($SOTC_UROOT_SHA256)"
stamp "digest: eboot.bin matches the extraction manifest ($SOTC_UROOT_SHA256)"

# --- Step 4: negative check (Review Focus #1) ---------------------------------
# The same invocation on junk input must be rejected with 'expected PS4 SELF' — proof the unwrap
# above consumed a real SELF rather than any 65 MB file. Silent on success.

tmp="$(mktemp -d)"
printf 'not a self' > "$tmp/eboot.bin"
if python3 "$SOTC_PC_PORT/scripts/prepare.py" "$tmp" --out "$tmp/out" 2>"$tmp/err"; then
  rm -rf "$tmp"
  fail "junk input was accepted (prepare.py must reject it)"
fi
if ! grep -q 'expected PS4 SELF' "$tmp/err"; then
  cat "$tmp/err" >&2
  rm -rf "$tmp"
  fail "junk input was rejected for an unexpected cause"
fi
rm -rf "$tmp"

# --- Step 5: headless Ghidra baseline (Review Focus #2/#3) ---------------------
#
# Import + auto-analyze the unwrapped ELF into a fresh project on every run — the rm -rf is what
# makes re-runs deterministic (Review Focus #3). Ghidra requires the project location directory
# to exist, and its ProjectLocator rejects any path element starting with '.' (this worktree
# lives under ~/.local), so the location is passed as a dot-free temp symlink to out/re/ghidra —
# the project files land under out/re/ghidra through it (verified: the location string is checked
# once, at ProjectLocator construction). Script output is prefixed by headless — the log carries
# `INFO  PrintSummary.java> summary … (GhidraScript)` — so the anchor matches the message body,
# not the start of the line. Everything goes to out/re/ghidra.log; analysis of a game eboot takes
# minutes.

rm -rf "$out/ghidra" "$out/ghidra.log"
mkdir -p "$out/ghidra"
proj_link_dir="$(mktemp -d)"
ln -s "$out/ghidra" "$proj_link_dir/ghidra"
stamp "ghidra: analyzeHeadless $out/ghidra sotc -import $out/eboot.elf (takes minutes; log: $out/ghidra.log)"
if ! "$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc \
     -import "$out/eboot.elf" \
     -scriptPath "$repo_root/tools/re/ghidra" \
     -postScript PrintSummary.java \
     -analysisTimeoutPerFile 3600 > "$out/ghidra.log" 2>&1; then
  rm -rf "$proj_link_dir"
  tail -n 40 "$out/ghidra.log" >&2
  fail "analyzeHeadless failed — see $out/ghidra.log"
fi
rm -rf "$proj_link_dir"

summary_line="$(grep -m1 'summary program=' "$out/ghidra.log" || true)"
if [[ -z "$summary_line" ]]; then
  tail -n 40 "$out/ghidra.log" >&2
  fail "no summary line in $out/ghidra.log — the import or PrintSummary.java failed"
fi
printf '%s\n' "$summary_line"
functions="$(printf '%s\n' "$summary_line" | sed -n 's/.*functions=\([0-9][0-9]*\).*/\1/p')"
[[ -n "$functions" ]] || fail "cannot parse functions= from the Ghidra summary line: $summary_line"
if [[ "$functions" -lt 1000 ]]; then
  tail -n 40 "$out/ghidra.log" >&2
  fail "Ghidra baseline is truncated: functions=$functions < 1000 — see $out/ghidra.log"
fi
error_count="$(grep -c 'ERROR' "$out/ghidra.log" || true)"
stamp "ghidra: functions=$functions (>= 1000); ERROR lines: $error_count"

# --- Summary ------------------------------------------------------------------
# The block the RE notes quote: prepare.py's entry/image and imports/relocations lines, the
# Ghidra summary line + ERROR count, the input's SHA-256, artifact paths, and total elapsed.

elapsed="$(( $(date +%s) - started ))"
stamp "summary"
printf 're0-acceptance: uroot     %s\n' "$SOTC_UROOT"
printf 're0-acceptance: prepare   %s\n' "$prepare_entry_line"
printf 're0-acceptance: prepare   %s\n' "$prepare_imports_line"
printf 're0-acceptance: ghidra    %s\n' "$summary_line"
printf 're0-acceptance: ghidra    ERROR lines: %s\n' "$error_count"
printf 're0-acceptance: sha256    %s  eboot.bin\n' "$actual_hash"
printf 're0-acceptance: elf       %s  %s bytes\n' "$out/eboot.elf" "$elf_bytes"
printf 're0-acceptance: analysis  %s\n' "$out/analysis.json"
printf 're0-acceptance: project   %s  (project sotc)\n' "$out/ghidra"
printf 're0-acceptance: log       %s\n' "$out/ghidra.log"
printf 're0-acceptance: elapsed   %ss\n' "$elapsed"
printf 're0-acceptance: PASS — SELF unwrapped via the port prepare.py; ELF + analysis.json verified; digest matches the extraction manifest; junk input rejected; Ghidra baseline functions=%s >= 1000\n' "$functions"
