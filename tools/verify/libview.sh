#!/bin/bash
# Per-branch library view: APFS clones (cp -cR, no data copied) of the shared library, so a branch's
# re-bakes and audio decodes never write through into out/library. Usage: libview.sh <branch> [base]
# Design §13.6: everything is cloned (models, textures, audio, coredata, catalog.json), no symlinks --
# the converter rewrites every texture PNG on each bake, a symlink would write through into the base.
# The view is built in a temp sibling and renamed into place last, so a view that exists is complete.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
branch="${1:?usage: libview.sh <branch> [base-library]}"
base="${2:-$root/out/library}"
name="$(printf '%s' "$branch" | tr -c 'A-Za-z0-9._-\n' '-')"   # feature/x -> feature-x
case "$name" in ''|.|..) echo "libview: bad branch name '$branch'" >&2; exit 2;; esac
[ -f "$base/catalog.json" ] || { echo "libview: no library at $base" >&2; exit 2; }
views="$root/out/libviews"
view="$views/$name"
if [ -e "$view" ]; then
  echo "libview: $view exists (complete), reusing (rm -rf it to re-clone)" >&2
  echo "export SOTC_LIB=$view"; exit 0
fi
mkdir -p "$views"
# cp -c silently degrades to a full copy off APFS or across volumes: refuse instead (12 GB, disk is tight).
dev() { df -P "$1" | awk 'NR==2{print $1}'; }
vdev="$(dev "$views")"
[ "$vdev" = "$(dev "$base")" ] || { echo "libview: $views and $base are on different volumes; cp -c cannot clone" >&2; exit 2; }
mount | grep -q "^$vdev on .*(apfs[,)]" || { echo "libview: $vdev is not APFS; refusing a full copy" >&2; exit 2; }
tmp="$view.tmp.$$"
trap 'rm -rf "$tmp"' EXIT
rm -rf "$view".tmp.* 2>/dev/null || true   # leftovers of a killed run (SIGKILL skips the trap)
mkdir "$tmp"
t0=$(date +%s)
for e in "$base"/*; do
  cp -cR "$e" "$tmp/"
  echo "libview: cloned $(basename "$e") ($(( $(date +%s) - t0 ))s elapsed)" >&2
done
mv "$tmp" "$view"
echo "export SOTC_LIB=$view"
