#!/bin/bash
# Per-branch library view: APFS clones (cp -cR, no data copied) of the shared library, so a branch's
# re-bakes and audio decodes never write through into out/library. Usage: libview.sh <branch> [base]
# Design §13.6: everything is cloned (models, textures, audio, coredata, catalog.json), no symlinks --
# the converter rewrites every texture PNG on each bake, a symlink would write through into the base.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
branch="${1:?usage: libview.sh <branch> [base-library]}"
base="${2:-$root/out/library}"
name="$(printf '%s' "$branch" | tr -c 'A-Za-z0-9._-\n' '-')"   # feature/x -> feature-x
case "$name" in ''|.|..) echo "libview: bad branch name '$branch'" >&2; exit 2;; esac
[ -f "$base/catalog.json" ] || { echo "libview: no library at $base" >&2; exit 2; }
view="$root/out/libviews/$name"
if [ -e "$view" ]; then
  echo "libview: $view exists, reusing (rm -rf it to re-clone)" >&2
else
  mkdir -p "$view"
  t0=$(date +%s)
  for e in "$base"/*; do
    cp -cR "$e" "$view/"
    echo "libview: cloned $(basename "$e") ($(( $(date +%s) - t0 ))s elapsed)" >&2
  done
fi
echo "export SOTC_LIB=$view"
