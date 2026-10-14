#!/bin/sh
# Writes patches/ from the patched extern/libretro-uae tree: the reverse of
# apply-patches.sh. A patch is the tree's difference in a list of files. Edit
# the tree, run this, and apply-patches.sh must then say "already applied".
# A file is in ONE patch, whole.
set -eu
trap '' PIPE
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
tree="$root/extern/libretro-uae"

# name | files (space separated), in the order the patches apply
series='
0001-chimera-max-is-defined-and-the-core-adds-config-lines|libretro/libretro-core.c
0002-chimera-a-file-with-no-folder|libretro/libretro-glue.c
0003-chimera-disks-are-kept-in-memory|sources/src/disk.c
0004-chimera-reads-of-the-controls-are-counted|sources/src/inputdevice.c sources/src/cia.c
'

rm -f "$root"/patches/*.patch
printf '%s\n' "$series" | grep '|' | while IFS='|' read -r name files; do
	# shellcheck disable=SC2086
	git -C "$tree" diff -- $files > "$root/patches/$name.patch"
	echo "$name.patch: $(grep -c '^diff --git' "$root/patches/$name.patch") files"
done
# a changed file no patch names would be lost by a fresh clone
left="$(git -C "$tree" status --porcelain | wc -l)"
named="$(cat "$root"/patches/*.patch | grep -c '^diff --git')"
[ "$left" -eq "$named" ] || { echo "extern/libretro-uae has $left changed files and the patches name $named: add the rest to the series" >&2; exit 1; }
