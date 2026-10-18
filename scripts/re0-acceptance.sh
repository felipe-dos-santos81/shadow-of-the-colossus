#!/usr/bin/env bash
#
# re0-acceptance.sh — the RE-0 acceptance pass: unwrap the retail eboot.bin (PS4 SELF) with the
# port's own prepare.py and verify the artifacts. Re-runnable end to end.
#
# Steps (RE-0 foundation: the unwrap everything downstream reads):
#
#   1. preflight — the inputs exist ($SOTC_UROOT/eboot.bin, the port's scripts/prepare.py),
#      python3 and java are on PATH, and the pinned Ghidra bundle's analyzeHeadless is executable;
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
#      with `expected PS4 SELF`; the check is silent on success.
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

# SOTC_UROOT must be absolute (the staged symlinks point into it); the manifest default hangs
# off the resolved path: <uroot>/../../logs/uroot.sha256 = SOTC-extract/logs/uroot.sha256.
resolved="$(cd "$SOTC_UROOT" && pwd)" || fail "SOTC_UROOT is not a directory: $SOTC_UROOT"
SOTC_UROOT="$resolved"
: "${SOTC_UROOT_SHA256:=$(dirname "$(dirname "$SOTC_UROOT")")/logs/uroot.sha256}"

# --- Step 1: preflight --------------------------------------------------------

[[ -f "$SOTC_UROOT/eboot.bin" ]] || fail "input not found: $SOTC_UROOT/eboot.bin (set SOTC_UROOT)"
[[ -f "$SOTC_PC_PORT/scripts/prepare.py" ]] ||
  fail "port script not found: $SOTC_PC_PORT/scripts/prepare.py (set SOTC_PC_PORT)"
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
python3 "$SOTC_PC_PORT/scripts/prepare.py" "$stage" --out "$out"

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

# --- Summary ------------------------------------------------------------------

stamp "summary"
printf 're0-acceptance: uroot    %s\n' "$SOTC_UROOT"
printf 're0-acceptance: elf      %s  %s bytes\n' "$out/eboot.elf" "$elf_bytes"
printf 're0-acceptance: analysis %s\n' "$out/analysis.json"
printf 're0-acceptance: PASS — SELF unwrapped via the port prepare.py; ELF + analysis.json verified; digest matches the extraction manifest; junk input rejected\n'
