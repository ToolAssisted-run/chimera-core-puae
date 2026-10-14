#!/bin/sh
# The core's tests. Each line says what was compared. The script ends with a
# count of tests that passed, failed and were skipped, and with an error if
# any failed. A skipped test is not a pass.
#
#   waterbox/run-gate.sh
#
# CHIMERA_ROOT=<a Chimera checkout> adds the test that runs the package in
# Chimera's own headless runner (waterbox/build-package.sh -r <it> first).
# AMIGA_GAME=<folder> holds a game on one disk, an .adf file
# (tests/games-local/ninja when not given), AMIGA_GAME2=<folder> a game on two
# disks (tests/games-local/budokan), and AMIGA_KICKSTARTS=<folder> the
# Kickstart ROMs under the names the package gives them (tests/roms-local).
#
# Needs the native build (meson setup build/meson-native ... && ninja) and
# the sandboxed core (waterbox/setup-guest.sh && ninja -C build/meson-guest):
# docs/BUILDING.md.
#
# WHAT RUNS WITH NO FILES OF YOURS. No game and no Kickstart of Commodore's
# may be put in this repository or on a CI runner. PUAE carries AROS's free
# replacement Kickstart, and tests/make-amiga-disk.py writes a disk whose boot
# block is a small test program, so the "amiga:" tests run everywhere: the
# program runs, every control reaches it (both ports' joysticks, the mouse,
# the keyboard), its reads of the controls are seen, a write to the disk
# comes out as save data, a disk change reaches the drive, and the sandbox
# gives the native build's results with savestates. The "game:" tests need
# files of yours and say SKIP without them.
#
# The ways a test passes on a broken thing (Chimera's docs/gates.md) that
# these are written against: a machine that did nothing agrees with itself,
# so a run is first checked to have got somewhere; the end of a run hides a
# run that went another way and came back, so runs are compared as a stream
# of digests; and a comparison that cannot fail proves nothing, so each
# "the same" has a run beside it that must come out different.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
native="$root/build/meson-native/run-native"
runwbx="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
chimera_root="${CHIMERA_ROOT:-}"
game="${AMIGA_GAME:-$root/tests/games-local/ninja}"
game2="${AMIGA_GAME2:-$root/tests/games-local/budokan}"
kicks="${AMIGA_KICKSTARTS:-$root/tests/roms-local}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
ok=0; failed=0; skipped=0
report() { # <test> <PASS|FAIL|SKIP> <what>
	printf '%-24s %-6s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; FAIL) failed=$((failed + 1)) ;; *) skipped=$((skipped + 1)) ;; esac
}
# check <test> <what> <condition...>
check() {
	ck_test="$1"; ck_what="$2"; shift 2
	if "$@"; then report "$ck_test" PASS "$ck_what"; else report "$ck_test" FAIL "$ck_what"; fi
}
stream() { grep '^frame' "$1"; }
same() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2"; }
differ() { [ -s "$1" ] && [ -s "$2" ] && ! cmp -s "$1" "$2"; }
# field <file> <name>: the value after a name in the file's last report line
field() { stream "$1" | tail -1 | awk -v f="$2" '{for (i = 1; i <= NF; i++) if ($i == f) { print $(i + 1); exit }}'; }
# the program's numbers (tests/make-amiga-disk.py keeps them at 0x7E000 of
# Chip RAM, big-endian): half <dump> <offset> is a 16-bit one, in hex
half() { od -An -tx1 -j $((0x7E000 + $2)) -N2 "$1" 2> /dev/null | tr -d ' '; }
byte() { od -An -tx1 -j $((0x7E000 + $2)) -N1 "$1" 2> /dev/null | tr -d ' '; }
count() { printf '%d' "0x$(od -An -tx1 -j $((0x7E00C)) -N4 "$1" 2> /dev/null | tr -d ' ')"; }
# folder <name> <settings json> <disk>...: a work folder as the sandbox
# mounts a project - every file under its name
folder() {
	fd_dir="$work/$1"; fd_settings="$2"; shift 2
	mkdir -p "$fd_dir"
	fd_disks=""
	for fd_f in "$@"; do
		ln -sf "$fd_f" "$fd_dir/$(basename "$fd_f")"
		fd_disks="$fd_disks${fd_disks:+,}\"$(basename "$fd_f")\""
	done
	printf '{"disk":[%s]}' "$fd_disks" > "$fd_dir/slots"
	printf '%s' "$fd_settings" > "$fd_dir/settings"
}
nat() { nt_out="$1"; nt_dir="$2"; shift 2; "$native" "$work/$nt_dir" "$@" > "$work/$nt_out.out" 2> "$work/$nt_out.err"; }
box() { bx_out="$1"; bx_dir="$2"; shift 2; "$runwbx" "$core" "$work/$bx_dir" "$@" > "$work/$bx_out.out" 2> "$work/$bx_out.err"; }
# index <folder> <control>: the control's place in the panel
index() { "$native" "$work/$1" --frames 1 --list-panel 2> /dev/null | grep "'$2'" | awk '{print $2}'; }
# repeats <file> <first> <second> <count> <step>: do the report lines from
# frame <second> on repeat those from frame <first> on (run-wbx --rewind).
# The count of lag frames is the runner's own and goes on counting, so it is
# left out.
repeats() {
	awk -v a="$2" -v b="$3" -v n="$4" -v step="$5" '
		/^frame/ { f = $2; $1 = ""; $2 = ""; sub(/ lag [0-9]+$/, ""); line[f] = $0 }
		END {
			for (i = step; i < n; i += step) {
				if (!((a + i) in line) || !((b + i) in line) || line[a + i] != line[b + i]) exit 1
				seen++
			}
			exit seen > 0 ? 0 : 1
		}' "$1"
}

if [ ! -x "$native" ] || [ ! -x "$runwbx" ] || [ ! -f "$core" ]; then
	echo "not built: this needs build/meson-native/run-native and run-wbx, and build/meson-guest/core.wbx (docs/BUILDING.md)" >&2
	exit 1
fi

# ------------------------------------------------- the source and the package
# PUAE is the pinned commit with exactly the patches of patches/
patches="$(ls "$root"/patches/*.patch 2> /dev/null | wc -l)"
check "source:patched" "extern/libretro-uae is the pinned commit with the $patches patches of patches/" \
	sh -c "sh '$here/apply-patches.sh' > '$work/patches.log' 2>&1 && [ \"\$(git -C '$root' rev-parse HEAD:extern/libretro-uae)\" = \"\$(git -C '$root/extern/libretro-uae' rev-parse HEAD)\" ]"

# a sandbox build that failed must not be tested as the previous one
stale=""
for src in "$here"/cinterface.c "$root"/patches/*.patch; do
	[ -e "$src" ] || continue
	[ "$core" -nt "$src" ] || stale="$stale $(basename "$src")"
done
check "package:fresh" "core.wbx is not older than its sources${stale:+ (older than:$stale)}" [ -z "$stale" ]

# ------------------------------------------------- the Amiga, everywhere
mkdir -p "$work/disks"
python3 "$root/tests/make-amiga-disk.py" "$work/disks/prog.adf" > /dev/null
python3 "$root/tests/make-amiga-disk.py" "$work/disks/quiet.adf" nopad > /dev/null
python3 - "$work/disks/blank.adf" <<'PY'
import sys
open(sys.argv[1], "wb").write(bytes(901120))
PY
prog="$work/disks/prog.adf"
aros='{"kickstart":"aros"}'
folder pr "$aros" "$prog"
folder qu "$aros" "$work/disks/quiet.adf"

# the buttons and axes the package declares are the ones the core has, in order
"$native" "$work/pr" --frames 1 --list-panel > "$work/panel.out" 2> /dev/null
check "package:controls" "the package declares the core's own $(grep -c '^panel' "$work/panel.out") buttons and $(grep -c '^axis' "$work/panel.out") axes, in the core's order" \
	python3 - "$here/waterbox.config" "$work/panel.out" "$here/cinterface.c" <<'PY'
import json, re, sys
cfg = json.load(open(sys.argv[1]))
buttons, axes = [], 0
for line in open(sys.argv[2]):
    if line.startswith("panel "):
        buttons.append(line[line.index("'") + 1:line.rindex("'")])
    elif line.startswith("axis "):
        axes += 1
src = open(sys.argv[3]).read()
names = re.findall(r'"([^"]+)"', re.search(r"kAxisNames\[kAxes\] = \{([^}]*)\}", src).group(1))
m = cfg["machines"]
ok = len(m) == 1 and m[0]["input"]["buttons"] == buttons and [a["name"] for a in m[0]["input"]["axes"]] == names and len(names) == axes
sys.exit(0 if ok and len(buttons) == 119 and axes == 2 else 1)
PY

# AROS starts the boot block at about frame 630; the controls are held from
# frame 700 to the end, and the program's copy is read at frame 900
nat pr-idle pr --frames 900 --report 50 --dump-domain "Chip RAM" "$work/pr-idle.ram"
pictures="$(stream "$work/pr-idle.out" | awk '{print $7}' | sort -u | wc -l)"
check "amiga:runs" "a program this script writes on a disk boots with AROS's Kickstart: its frame count reached $(count "$work/pr-idle.ram") in 900 frames, with $pictures different pictures in 18 reports" \
	[ "$(count "$work/pr-idle.ram")" -gt 200 -a "$pictures" -gt 5 ]

held() { # held <name> <control>: the program run with one control held from 700
	nat "$1" pr --frames 900 --press "$(index pr "$2")":700:300 --dump-domain "Chip RAM" "$work/$1.ram"
}
held up "P1 Up"; held down "P1 Down"; held left "P1 Left"; held right "P1 Right"; held fire "P1 Fire"; held fire2 "P1 Fire 2"
# JOY1DAT's direction bits: left is bit 9, right bit 1, up bit 9 xor bit 8,
# down bit 1 xor bit 0 (the other bits are the port's counters)
dirs() { v=$((0x$(half "$1" 0))); echo "$(( (v >> 9 & 1) )) $(( (v >> 1 & 1) )) $(( (v >> 9 ^ v >> 8) & 1 )) $(( (v >> 1 ^ v) & 1 ))"; }
check "amiga:joystick" "P1's joystick in port 2: each direction held alone reads as that direction in JOY1DAT (left right up down: idle $(dirs "$work/pr-idle.ram"), up $(dirs "$work/up.ram"), down $(dirs "$work/down.ram"), left $(dirs "$work/left.ram"), right $(dirs "$work/right.ram")); Fire clears CIA-A's bit 7 ($(byte "$work/fire.ram" 4), idle $(byte "$work/pr-idle.ram" 4)) and Fire 2 POTGOR's bit 14 ($(half "$work/fire2.ram" 6), idle $(half "$work/pr-idle.ram" 6))" \
	sh -c "[ '$(dirs "$work/pr-idle.ram")' = '0 0 0 0' ] && [ '$(dirs "$work/up.ram")' = '0 0 1 0' ] && [ '$(dirs "$work/down.ram")' = '0 0 0 1' ] && [ '$(dirs "$work/left.ram")' = '1 0 1 0' -o '$(dirs "$work/left.ram")' = '1 0 0 0' ] && [ '$(dirs "$work/right.ram")' = '0 1 0 0' -o '$(dirs "$work/right.ram")' = '0 1 0 1' ] && [ '$(byte "$work/fire.ram" 4)' = 7c ] && [ '$(byte "$work/pr-idle.ram" 4)' = fc ] && [ '$(half "$work/fire2.ram" 6)' = 1500 ] && [ '$(half "$work/pr-idle.ram" 6)' = 5500 ]"

held mleft "Mouse Left"; held mright "Mouse Right"
nat mmove pr --frames 900 --axis 0:5:700:10 --axis 1:-3:700:10 --dump-domain "Chip RAM" "$work/mmove.ram"
check "amiga:mouse" "the mouse in port 1: moved 5 right and 3 up for 10 frames, JOY0DAT's counters read $(half "$work/mmove.ram" 2) (50 across, -30 down: e232); Mouse Left clears CIA-A's bit 6 ($(byte "$work/mleft.ram" 4)) and Mouse Right POTGOR's bit 10 ($(half "$work/mright.ram" 6))" \
	sh -c "[ '$(half "$work/mmove.ram" 2)' = e232 ] && [ '$(half "$work/pr-idle.ram" 2)' = 0000 ] && [ '$(byte "$work/mleft.ram" 4)' = bc ] && [ '$(half "$work/mright.ram" 6)' = 5100 ]"

held keyA "Key A"
nat keytap pr --frames 900 --press "$(index pr "Key A")":700:10 --dump-domain "Chip RAM" "$work/keytap.ram"
check "amiga:keyboard" "a key reaches the program as the keyboard sends it: A held, its code 20 once ($(byte "$work/keyA.ram" 8), $(printf '%d' "0x$(half "$work/keyA.ram" 10)") key); A pressed and let go, 20 then a0 ($(byte "$work/keytap.ram" 8), $(printf '%d' "0x$(half "$work/keytap.ram" 10)") keys)" \
	sh -c "[ '$(byte "$work/keyA.ram" 8)' = 20 ] && [ '$(half "$work/keyA.ram" 10)' = 0001 ] && [ '$(byte "$work/keytap.ram" 8)' = a0 ] && [ '$(half "$work/keytap.ram" 10)' = 0002 ]"

folder p1 '{"kickstart":"aros","port1":"joystick"}' "$prog"
nat p1up p1 --frames 900 --press "$(index p1 "P2 Up")":700:300 --dump-domain "Chip RAM" "$work/p1up.ram"
nat p1fire p1 --frames 900 --press "$(index p1 "P2 Fire")":700:300 --dump-domain "Chip RAM" "$work/p1fire.ram"
check "amiga:port1-joystick" "with Port 1 set to a joystick, P2's controls reach port 1: Up in JOY0DAT ($(half "$work/p1up.ram" 2)), Fire as CIA-A's bit 6 ($(byte "$work/p1fire.ram" 4))" \
	sh -c "[ '$(half "$work/p1up.ram" 2)' = 0100 ] && [ '$(byte "$work/p1fire.ram" 4)' = bc ]"

folder none '{"kickstart":"aros","port1":"none","port2":"cd32pad"}' "$prog"
"$native" "$work/none" --frames 1 --list-panel > "$work/none-panel.out" 2> /dev/null
check "amiga:controls" "which controls are live follows the ports: $(grep -c ' active$' "$work/panel.out") with a mouse and a joystick, $(grep -c ' active$' "$work/none-panel.out") with nothing in port 1 and the CD32's pad in port 2 (its five more buttons, no mouse)" \
	sh -c "grep -q \"'P1 Play' active\" '$work/none-panel.out' && ! grep -q \"'Mouse Left' active\" '$work/none-panel.out' && grep -q \"'Mouse Left' active\" '$work/panel.out' && ! grep -q \"'P1 Play' active\" '$work/panel.out'"

# every kind of control in one run, native and sandbox
keysrun="--frames 900 --report 25 --press $(index pr "P1 Up"):700:20 --press $(index pr "P1 Fire"):720:10 --press $(index pr "Mouse Left"):740:10 --press $(index pr "Key Space"):800:5 --press $(index pr "P1 Fire 2"):820:30"
allrun="$keysrun --axis 0:7:760:20 --axis 1:2:770:20"
nat all pr $allrun
box all-box pr $allrun
box keys-box pr $keysrun
for n in all all-box keys-box pr-idle; do stream "$work/$n.out" > "$work/$n.stream"; done
check "amiga:sandbox" "native and sandbox print the same $(wc -l < "$work/all.stream") digests of memory, picture and sound, with every kind of control used and the mouse moved" \
	same "$work/all.stream" "$work/all-box.stream"
check "amiga:controls-change-the-run" "the run with the controls used differs from the idle one, and moving the mouse changes it again" \
	sh -c "! cmp -s '$work/keys-box.stream' '$work/pr-idle.stream' && ! cmp -s '$work/all-box.stream' '$work/keys-box.stream'"

# lag: the program reads its controls every frame, its twin never does;
# both count lag frames while AROS boots
nat q qu --frames 900 --report 100
lag700() { stream "$1" | awk '$2 == 700 {print $NF}'; }
check "amiga:lag" "from frame 700 to 900 the program that reads its controls has no lag frame ($(lag700 "$work/pr-idle.out") then $(field "$work/pr-idle.out" lag)) and its twin that reads none has 200 ($(lag700 "$work/q.out") then $(field "$work/q.out" lag))" \
	[ "$(field "$work/pr-idle.out" lag)" = "$(lag700 "$work/pr-idle.out")" -a $(( $(field "$work/q.out" lag) - $(lag700 "$work/q.out") )) = 200 ]

box all-rerecord pr $allrun --rerecord
box all-session pr $allrun --session
box rewind pr --frames 1600 --report 5 --press "$(index pr "P1 Up")":700:900 --rewind
stream "$work/all-rerecord.out" > "$work/all-rerecord.stream"
stream "$work/all-session.out" > "$work/all-session.stream"
check "amiga:state-every-frame" "a state saved and loaded every frame changes nothing" same "$work/all-box.stream" "$work/all-rerecord.stream"
check "amiga:state-other-box" "a state moved to a new sandbox half way changes nothing" same "$work/all-box.stream" "$work/all-session.stream"
check "amiga:state-rewind" "a state kept at frame 800 and loaded at 1200: the frames after the load repeat those after the save" \
	repeats "$work/rewind.out" 800 1200 400 5

# the disk: the program writes its first 512 bytes to sector 1700; the disk
# comes out changed as save data, the user's file stays as it was
mkdir -p "$work/saved"
box save pr --frames 700 --savedata-out "$work/saved"
check "amiga:disk-write" "the program's write reaches the disk: Export Save Data gives prog.adf, which differs from the file only in sector 1700, where the boot block's first 512 bytes are; the file itself is unchanged" \
	python3 - "$prog" "$work/saved/prog.adf" "$work/disks/prog.adf" <<'PY'
import sys
orig = open(sys.argv[1], "rb").read()
try:
    out = open(sys.argv[2], "rb").read()
except OSError:
    sys.exit(1)
diff = [i for i in range(len(orig)) if orig[i] != out[i]] if len(out) == len(orig) else [0]
ok = diff and min(diff) >= 1700 * 512 and max(diff) < 1701 * 512 and out[1700 * 512:1701 * 512] == orig[:512]
sys.exit(0 if ok else 1)
PY

# a disk change: a blank disk in the drive, the program the second disk
folder ch "$aros" "$work/disks/blank.adf" "$prog"
nat ch-stay ch --frames 1500 --dump-domain "Chip RAM" "$work/ch-stay.ram"
nat ch-swap ch --frames 1500 --press "$(index ch "Next Disk")":100:5 --press "$(index ch "Eject Disk")":120:10 --dump-domain "Chip RAM" "$work/ch-swap.ram"
check "amiga:disk-change" "with a blank disk in the drive nothing boots (the program's count: $(count "$work/ch-stay.ram")); Next Disk and Eject Disk put the program's disk in and it runs ($(count "$work/ch-swap.ram") frames by frame 1500)" \
	[ "$(count "$work/ch-swap.ram")" -gt 100 -a "$(count "$work/ch-swap.ram")" -lt 1500 -a \( "$(count "$work/ch-stay.ram")" = 0 -o "$(count "$work/ch-stay.ram")" -gt 4000000000 \) ]

# ------------------------------------------------- what is refused
mkdir -p "$work/nodisk"
printf '{"disk":[]}' > "$work/nodisk/slots"; printf '{}' > "$work/nodisk/settings"
nat nodisk nodisk --frames 1
check "refused:no-disk" "an Amiga with no disk is refused, and says so" grep -q "Init failed.*no disk was given" "$work/nodisk.out" "$work/nodisk.err"
folder nokick '{"model":"A1200"}' "$prog"
nat nokick nokick --frames 1
check "refused:kickstart-missing" "an A1200 with no Kickstart ROM is refused, by the ROM's name" \
	grep -q "Init failed.*amiga-os-310-a1200.rom" "$work/nokick.out" "$work/nokick.err"

# ------------------------------------------------- Chimera's engine
runner="$chimera_root/build/dll/chimera-run"
package="$chimera_root/build/Cores/puae.chimeraCore"
if [ -n "$chimera_root" ] && [ -x "$runner" ] && [ -f "$package" ]; then
	mkdir -p "$work/proj"
	cp "$prog" "$work/proj/"
	python3 "$root/tests/make-project.py" "$package" "$work/proj/prog.chimeraProject" 900 kickstart=aros \
		"press=P1 Up:699:20" "press=Key Space:719:5" panel="$work/panel.out" -- disk="$work/proj/prog.adf" > /dev/null
	"$runner" --project "$work/proj/prog.chimeraProject" "$package" --files "$work/proj" --frames 900 \
		--dump "Chip RAM=$work/engine.ram" > "$work/engine.log" 2>&1
	box eng pr --frames 900 --press "$(index pr "P1 Up")":700:20 --press "$(index pr "Key Space")":720:5 --dump-domain "Chip RAM" "$work/eng.ram"
	check "engine:amiga" "a project of the program with presses plays in Chimera's headless runner to the sandbox runner's Chip RAM ($(wc -c < "$work/eng.ram" 2> /dev/null) bytes, the program's count $(count "$work/engine.ram")), another than the idle run's" \
		sh -c "cmp -s '$work/engine.ram' '$work/eng.ram' && ! cmp -s '$work/engine.ram' '$work/pr-idle.ram'"
else
	report "engine:amiga" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/puae.chimeraCore"
fi

# ------------------------------------------------- games
adf="$(ls "$game"/*.adf 2> /dev/null | head -1)"
if [ -n "$adf" ] && [ -f "$kicks/amiga-os-130.rom" ]; then
	folder gm '{}' "$adf"; ln -sf "$kicks/amiga-os-130.rom" "$work/gm/amiga-os-130.rom"
	# (the mouse is not moved here: when the Amiga's own system reads it,
	# native and sandbox part by a count - docs/PLAN.md, "Open problems")
	gmrun="--frames 2600 --report 50 --press $(index gm "P1 Fire"):1600:30 --press $(index gm "Mouse Left"):1700:10 --press $(index gm "Key Space"):1800:5"
	nat gm-idle gm --frames 2600 --report 50
	nat gm gm $gmrun
	box gm-box gm $gmrun
	box gm-rerecord gm $gmrun --rerecord
	box gm-session gm $gmrun --session
	for n in gm gm-idle gm-box gm-rerecord gm-session; do stream "$work/$n.out" > "$work/$n.stream"; done
	pictures="$(awk '{print $7}' "$work/gm.stream" | sort -u | wc -l)"
	check "game:runs" "$(basename "$adf") boots on Kickstart 1.3 (an A500): $pictures different pictures in 52 reports, and the controls change the run" \
		sh -c "[ $pictures -gt 3 ] && ! cmp -s '$work/gm.stream' '$work/gm-idle.stream'"
	check "game:sandbox" "native and sandbox print the same $(wc -l < "$work/gm.stream") digests with presses" same "$work/gm.stream" "$work/gm-box.stream"
	check "game:states" "a state saved and loaded every frame, and a state moved to a new sandbox, change nothing" \
		sh -c "cmp -s '$work/gm-box.stream' '$work/gm-rerecord.stream' && cmp -s '$work/gm-box.stream' '$work/gm-session.stream'"
	if [ -f "$kicks/amiga-os-310-a1200.rom" ]; then
		folder a12 '{"model":"A1200"}' "$adf"; ln -sf "$kicks/amiga-os-310-a1200.rom" "$work/a12/amiga-os-310-a1200.rom"
		nat a12 a12 --frames 1500 --report 50
		box a12-box a12 --frames 1500 --report 50
		for n in a12 a12-box; do stream "$work/$n.out" > "$work/$n.stream"; done
		folder a12c '{"model":"A1200","initialTime":"1262304000"}' "$adf"; ln -sf "$kicks/amiga-os-310-a1200.rom" "$work/a12c/amiga-os-310-a1200.rom"
		nat a12c a12c --frames 1500 --report 50
		stream "$work/a12c.out" > "$work/a12c.stream"
		check "game:clock" "Kickstart 3.1 reads the A1200's clock: with the Starting Clock on the first of January 2010 the run is another one than with the default" \
			differ "$work/a12c.stream" "$work/a12.stream"
		check "game:a1200" "on an A1200 with Kickstart 3.1 the disk boots ($(awk '{print $7}' "$work/a12.stream" | sort -u | wc -l) different pictures), another run than the A500's, and the sandbox gives the native build's digests" \
			sh -c "[ \$(awk '{print \$7}' '$work/a12.stream' | sort -u | wc -l) -gt 2 ] && cmp -s '$work/a12.stream' '$work/a12-box.stream' && ! cmp -s '$work/a12.stream' '$work/gm-idle.stream'"
	else
		report "game:a1200" SKIP "no Kickstart 3.1 for the A1200 (amiga-os-310-a1200.rom in AMIGA_KICKSTARTS)"
		report "game:clock" SKIP "no Kickstart 3.1 for the A1200 (amiga-os-310-a1200.rom in AMIGA_KICKSTARTS)"
	fi
else
	for t in game:runs game:sandbox game:states game:a1200 game:clock; do
		report "$t" SKIP "no game (AMIGA_GAME) or no Kickstart 1.3 (amiga-os-130.rom in AMIGA_KICKSTARTS)"
	done
fi
d1="$(ls "$game2"/*.adf 2> /dev/null | sort | sed -n 1p)"
d2="$(ls "$game2"/*.adf 2> /dev/null | sort | sed -n 2p)"
if [ -n "$d2" ] && [ -f "$kicks/amiga-os-130.rom" ]; then
	folder g2 '{}' "$d2" "$d1"; ln -sf "$kicks/amiga-os-130.rom" "$work/g2/amiga-os-130.rom"
	nat g2-stay g2 --frames 2000 --report 50
	nat g2-swap g2 --frames 2000 --report 50 --press "$(index g2 "Next Disk")":600:5 --press "$(index g2 "Eject Disk")":620:10
	box g2-swap-box g2 --frames 2000 --report 50 --press "$(index g2 "Next Disk")":600:5 --press "$(index g2 "Eject Disk")":620:10
	for n in g2-stay g2-swap g2-swap-box; do stream "$work/$n.out" > "$work/$n.stream"; done
	check "game:disk-change" "a game's second disk in the drive: changed to its first, the run leaves the one that stays ($(awk '{print $7}' "$work/g2-swap.stream" | sort -u | wc -l) pictures against $(awk '{print $7}' "$work/g2-stay.stream" | sort -u | wc -l)), and the sandbox gives the native build's digests" \
		sh -c "! cmp -s '$work/g2-swap.stream' '$work/g2-stay.stream' && cmp -s '$work/g2-swap.stream' '$work/g2-swap-box.stream' && [ \$(awk '{print \$7}' '$work/g2-swap.stream' | sort -u | wc -l) -gt \$(awk '{print \$7}' '$work/g2-stay.stream' | sort -u | wc -l) ]"
else
	report "game:disk-change" SKIP "no game on two disks (AMIGA_GAME2) or no Kickstart 1.3"
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
