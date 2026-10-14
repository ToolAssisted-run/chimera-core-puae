#!/bin/bash
# The package in Chimera's own window (Chimera.exe under Mono, on a private
# Xvfb display), which the headless runner cannot show:
#  - the test program (tests/make-amiga-disk.py) boots with AROS's Kickstart
#    in the window and leaves the Chip RAM the headless runner leaves at the
#    same frame, with the picture PUAE gives (720x287);
#  - the keys the package ships become Chimera's keys for the Amiga;
#  - with a game and Kickstart 1.3 (AMIGA_GAME and AMIGA_KICKSTARTS, as
#    waterbox/run-gate.sh takes them): a project takes the Kickstart from
#    Chimera's firmware, runs in the window and leaves the headless runner's
#    Chip RAM; without the Kickstart it is refused and the file is named.
#
# Usage: tests/run-frontend.sh --chimera-root <chimera checkout> [--frames N] [--keep]
#   --keep   leave the work folder (logs, configurations, projects) and say where it is
# Needs: mono, Xvfb, Chimera built with its window (dotnet build), and the
# package installed in that checkout (waterbox/build-package.sh -r <chimera>).
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
wb="$root/waterbox"
frames=900
chimera_root=""
keep=""
while [ $# -gt 0 ]; do
	case "$1" in
		--keep) keep=1 ;;
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "say where Chimera is: --chimera-root <chimera checkout>" >&2; exit 2; }
chimera_root="$(cd "$chimera_root" && pwd)"
game="${AMIGA_GAME:-$here/games-local/ninja}"
kicks="${AMIGA_KICKSTARTS:-$here/roms-local}"

emu_exe="$chimera_root/build/Chimera.exe"
package="$chimera_root/build/Cores/puae.chimeraCore"
runwbx="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
runner="$chimera_root/build/meson-linux/chimera-run"
[ -f "$emu_exe" ] || { echo "Chimera's window is not built: $emu_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "the package is not installed: $package (run waterbox/build-package.sh -r $chimera_root)" >&2; exit 1; }
[ -x "$runwbx" ] && [ -f "$core" ] || { echo "the sandbox build is not there: $runwbx, $core" >&2; exit 1; }
[ -x "$runner" ] || { echo "Chimera's headless runner is not built: $runner" >&2; exit 1; }

work="$(mktemp -d)"
xvfb_pid=""
cleanup() {
	[ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null
	if [ -n "$keep" ]; then echo "work folder kept: $work"; else rm -rf "$work"; fi
}
trap cleanup EXIT

export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
# always a display of this test's own: nobody's desktop is drawn on
command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
for n in 90 91 92 93 94 95 96; do
	if [ ! -e "/tmp/.X11-unix/X$n" ]; then
		Xvfb ":$n" -screen 0 1024x768x24 -nolisten tcp > /dev/null 2>&1 & xvfb_pid=$!
		export DISPLAY=":$n"; break
	fi
done
[ -n "$xvfb_pid" ] || { echo "no free display number for Xvfb" >&2; exit 1; }
sleep 1

config="$work/config.ini"
( cd "$chimera_root" && timeout 120 mono "$emu_exe" --headless "--config=$config" "--lua=$here/exit.lua" ) > "$work/bootstrap.log" 2>&1
[ -f "$config" ] || { echo "Chimera did not write a configuration:"; tail -5 "$work/bootstrap.log"; exit 1; } >&2
sed -i 's/"DispMethod": [0-9]/"DispMethod": 1/' "$config"

ok=0
failed=0
skipped=0
report() {
	printf '%-16s %-5s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; SKIP) skipped=$((skipped + 1)) ;; *) failed=$((failed + 1)) ;; esac
}

# make_project <tag> <frames> <disk file> [<setting>=<value> | press=... | firmware=<id>=<file>]...
# A project with the disk beside it, where Chimera looks, and a movie of
# that many frames over the Amiga's controls.
make_project() {
	local tag="$1" nframes="$2" diskfile="$3"; shift 3
	local dir="$work/$tag"
	mkdir -p "$dir"
	cp "$diskfile" "$dir/"
	python3 "$here/make-project.py" "$package" "$dir/$tag.chimeraProject" "$nframes" "panel=$work/panel.out" "$@" \
		-- "disk=$dir/$(basename "$diskfile")" > "$work/$tag.log" 2>&1
}
# run_frontend <tag> <configuration> <frames> <domain or "">: the project in the window
run_frontend() {
	local tag="$1" cfg="$2" nframes="$3" domain="$4" job="$work/job.$1.txt"
	{
		echo "frames=$nframes"
		echo "out=$work/$tag.ram.bin"
		echo "meta=$work/$tag.meta.txt"
		echo "shot="
		echo "domain=$domain"
	} > "$job"
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
		"--config=$cfg" "--project=$work/$tag/$tag.chimeraProject" "--lua=$here/frontend-ram.lua" ) >> "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}
value() { sed -n "s/^$2=//p" "$work/$1.meta.txt" 2>/dev/null; }

python3 "$here/settings-config.py" "$config" "$work/config.base.ini" '{}'

# ---- the test program: needs no file of anyone's
mkdir -p "$work/prog" "$work/probe"
python3 "$here/make-amiga-disk.py" "$work/prog/prog.adf" > /dev/null
ln -sf "$work/prog/prog.adf" "$work/probe/prog.adf"
printf '{"disk":["prog.adf"]}' > "$work/probe/slots"; printf '{"kickstart":"aros"}' > "$work/probe/settings"
"$runwbx" "$core" "$work/probe" --frames 1 --list-panel > "$work/panel.out" 2> /dev/null
# the movie is longer than the run: a window whose movie ends stops there;
# P1 Up is held from frame 700 (row 699) to the end
make_project am $((frames + 100)) "$work/prog/prog.adf" kickstart=aros "press=P1 Up:699:$frames"
"$runner" --project "$work/am/am.chimeraProject" "$package" --files "$work/am" --frames "$frames" \
	--dump "Chip RAM=$work/engine.ram.bin" > "$work/engine.log" 2>&1
up="$(od -An -tx1 -j $((0x7E000)) -N2 "$work/engine.ram.bin" 2> /dev/null | tr -d ' ')"
if ! run_frontend am "$work/config.base.ini" "$frames" "Chip RAM"; then
	report "window:amiga" FAIL "Chimera did not finish the run: $(value am detail) $(tail -2 "$work/am.log" | tr '\n' ' ' | cut -c1-200)"
elif [ "$(value am frames)" != "$frames" ] || [ "$(value am width)" != 720 ] || [ "$(value am height)" != 287 ]; then
	report "window:amiga" FAIL "frames $(value am frames) of $frames, picture $(value am width)x$(value am height) and not 720x287"
elif [ -s "$work/engine.ram.bin" ] && cmp -s "$work/engine.ram.bin" "$work/am.ram.bin" && [ "$up" = 0100 ]; then
	report "window:amiga" PASS "the test program booted with AROS's Kickstart and ran $frames frames in Chimera's window, 720x287, with P1 Up held from frame 700 (JOY1DAT $up); its Chip RAM ($(wc -c < "$work/am.ram.bin") bytes) is what the headless runner leaves"
else
	report "window:amiga" FAIL "the Chip RAM differs from the headless runner's (or one of the two is missing), or P1 Up did not reach the program (JOY1DAT $up)"
fi

python3 "$here/forget-controller.py" "$work/config.base.ini" "$work/config.keys.ini" "Amiga Controls"
mkdir -p "$work/keys"; cp "$work/am/prog.adf" "$work/keys/"; sed 's/"puae-test-[^"]*"/"puae-test-keys"/' "$work/am/am.chimeraProject" > "$work/keys/keys.chimeraProject"
if run_frontend keys "$work/config.keys.ini" 1 ""; then
	if python3 "$here/check-keybinds.py" "$work/config.keys.ini" "$wb/default_keybinds.json" "Amiga Controls" "P1 Fire" > "$work/keys.txt" 2>&1; then
		report "window:keys" PASS "$(cat "$work/keys.txt")"
	else
		report "window:keys" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "window:keys" FAIL "Chimera did not finish the run: $(tail -2 "$work/keys.log" | tr '\n' ' ' | cut -c1-200)"
fi

# ---- a game, with Kickstart 1.3 from Chimera's firmware
adf="$(ls "$game"/*.adf 2> /dev/null | head -1)"
kick="$kicks/amiga-os-130.rom"
if [ -z "$adf" ] || [ ! -f "$kick" ]; then
	report "window:game" SKIP "needs a game (AMIGA_GAME) and Kickstart 1.3 (amiga-os-130.rom in AMIGA_KICKSTARTS)"
	report "window:refusal" SKIP "needs a game (AMIGA_GAME) and Kickstart 1.3 (amiga-os-130.rom in AMIGA_KICKSTARTS)"
else
	# the Kickstart lives in a folder of its own, which Chimera is told of only
	# by the configuration of the run that is meant to find it
	fwdir="$(mktemp -d)"
	cp "$kick" "$fwdir/amiga-os-130.rom"
	make_project nokick 100 "$adf" "firmware=amiga-os-130.rom=$fwdir/amiga-os-130.rom"
	if run_frontend nokick "$work/config.base.ini" 1 ""; then
		report "window:refusal" FAIL "the project opened without the Kickstart"
	elif grep -q "amiga-os-130.rom" "$work/nokick.log"; then
		report "window:refusal" PASS "without the Kickstart the project is not opened, and the file is named: $(grep -o "[^.]*amiga-os-130\.rom[^.]*" "$work/nokick.log" | head -1 | cut -c1-120)"
	else
		report "window:refusal" FAIL "refused, but the file is not named: $(tail -2 "$work/nokick.log" | tr '\n' ' ' | cut -c1-200)"
	fi
	python3 "$here/settings-config.py" "$config" "$work/config.fw.ini" '{}' "{\"amiga-os-130.rom\": \"$fwdir/amiga-os-130.rom\"}"
	gframes=1500
	make_project gm $((gframes + 100)) "$adf" "firmware=amiga-os-130.rom=$fwdir/amiga-os-130.rom" "press=Key Right Amiga:1399:30"
	"$runner" --project "$work/gm/gm.chimeraProject" "$package" --files "$work/gm" --firmware "amiga-os-130.rom=$fwdir/amiga-os-130.rom" \
		--frames "$gframes" --dump "Chip RAM=$work/gm-engine.ram.bin" > "$work/gm-engine.log" 2>&1
	if ! run_frontend gm "$work/config.fw.ini" "$gframes" "Chip RAM"; then
		report "window:game" FAIL "Chimera did not finish the run: $(value gm detail) $(tail -2 "$work/gm.log" | tr '\n' ' ' | cut -c1-200)"
	elif [ "$(value gm frames)" = "$gframes" ] && [ -s "$work/gm-engine.ram.bin" ] && cmp -s "$work/gm-engine.ram.bin" "$work/gm.ram.bin"; then
		report "window:game" PASS "a project of $(basename "$adf") took Kickstart 1.3 from Chimera's firmware, ran $gframes frames in the window with a key pressed, $(value gm width)x$(value gm height), and left the Chip RAM the headless runner leaves"
	else
		report "window:game" FAIL "frames $(value gm frames) of $gframes, or the Chip RAM differs from the headless runner's"
	fi
	rm -rf "$fwdir"
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
