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
0005-chimera-a-write-of-vposw-does-not-stay|sources/src/custom.c
0006-chimera-config-names-are-set|sources/src/misc.c
0007-chimera-the-pc-mouse-stub-returns-false|sources/src/x86.c
0008-chimera-threads-run-one-at-a-time|retrodep/threaddep/thread.h retrodep/threaddep/thread.c sources/src/include/sleep.h sources/src/devices.c
0009-chimera-the-cd32-save-memory-stays-in-memory|sources/src/akiko.c
'

# a patch that did not change keeps its file (and its time: the test script
# asks whether the core is newer than its patches)
keep="$(mktemp -d)"
trap 'rm -rf "${keep:?}"' EXIT
cp -p "$root"/patches/*.patch "$keep"/ 2> /dev/null || true
rm -f "$root"/patches/*.patch
printf '%s\n' "$series" | grep '|' | while IFS='|' read -r name files; do
	# shellcheck disable=SC2086
	git -C "$tree" diff -- $files > "$root/patches/$name.patch"
	if cmp -s "$root/patches/$name.patch" "$keep/$name.patch"; then
		cp -p "$keep/$name.patch" "$root/patches/$name.patch"
	fi
	echo "$name.patch: $(grep -c '^diff --git' "$root/patches/$name.patch") files"
done
# a changed file no patch names would be lost by a fresh clone
left="$(git -C "$tree" status --porcelain | wc -l)"
named="$(cat "$root"/patches/*.patch | grep -c '^diff --git')"
[ "$left" -eq "$named" ] || { echo "extern/libretro-uae has $left changed files and the patches name $named: add the rest to the series" >&2; exit 1; }
