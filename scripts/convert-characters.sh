#!/usr/bin/env bash
#
# convert-characters.sh — convert the SOTC character model subtree, then report.
#
# A thin, resumable wrapper around the pinned CLI surface:
#
#   bp-convert convert --root "$SOTC_GAME_ROOT" --out "$OUT_LIB" \
#       --under models/shadow/characters --ext cmsh --jobs "$JOBS"
#   bp-convert report  --out "$OUT_LIB"
#
# The character meshes live under models/shadow/characters (the characters/ tree itself holds
# only .cchr character data, no meshes). Rerunning skips models already recorded ok and retries
# previously-failed ones; FORCE=1 reconverts everything in scope. The game tree is read-only.
#
# Environment:
#   SOTC_GAME_ROOT  required — the extracted game tree
#   OUT_LIB         output library (default: <repo>/out/library)
#   JOBS            conversion parallelism (default: 8)
#   FORCE           1 to reconvert everything in scope (adds --force)
#
# The published CLI (out/bin/, Task 7's publish step) is used when present — via `dotnet exec`,
# which sidesteps this machine's apphost/DOTNET_ROOT quirk (docs/notes/aqua-core-api.md §0);
# otherwise the wrapper falls back to `dotnet run` against the source tree.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${SOTC_GAME_ROOT:?set SOTC_GAME_ROOT to the extracted game tree}"
out_lib="${OUT_LIB:-$repo_root/out/library}"
jobs="${JOBS:-8}"

# Bash 3.2 arrays + `set -u`: only expand the flag array when non-empty.
force_args=()
if [[ "${FORCE:-0}" == "1" ]]; then
  force_args+=(--force)
fi

if [[ -f "$repo_root/out/bin/bp-convert.dll" ]]; then
  cli=(dotnet exec "$repo_root/out/bin/bp-convert.dll")
else
  cli=(dotnet run --project "$repo_root/src/BpConvert.Cli" -c Release --)
fi

echo "convert-characters: game=$SOTC_GAME_ROOT out=$out_lib jobs=$jobs force=${FORCE:-0}"
"${cli[@]}" convert \
  --root "$SOTC_GAME_ROOT" \
  --out "$out_lib" \
  --under models/shadow/characters \
  --ext cmsh \
  --jobs "$jobs" \
  ${force_args[@]+"${force_args[@]}"}

"${cli[@]}" report --out "$out_lib"
