# Plan and design log - PUAE core for Chimera

This is the design log of the core: what was decided, why, and what was
measured. Entries are dated. Read it before changing how the core works.

## Why this core (2026-10-14)

The owner wants every system BizHawk supports to be supported in Chimera
too. The Amiga is one of the gaps. BizHawk runs it on PUAE, the libretro
port of UAE. The owner asked for a fresh port of the latest PUAE, taking
nothing from BizHawk's port but ideas.

## What was read (2026-10-14)

libretro-uae (PUAE) at its latest commit, 7387937 (2026-10-07), based on
WinUAE 5.3.1, GPL version 2. From its source: the emulator (`sources/src`),
the libretro layer around it (`libretro/`), and the host pieces UAE asks
for (`retrodep/`).

## The eight questions (docs/new-core.md in chimera)

1. **Is it a new core?** Yes. No core here runs the Amiga.
2. **May it be distributed and modified?** Yes: GPL version 2. The package
   is GPL-2.0.
3. **One thread on one clock?** Yes for disks. The emulator leaves its
   processor loop at the end of every frame (`m68k_go`), so a frame is one
   call. UAE starts threads for hard disk folders, hard disk images, the
   CD32's and CDTV's drives and networking; none of them is used for
   floppy disks.
4. **Where does the picture come from?** The emulator draws into a buffer
   of its own (`retro_bmp`) and hands it to the libretro layer's video
   callback once a frame: 32-bit pixels.
5. **Does it recompile?** No: upstream's recompiler is not built (upstream
   leaves it out too).
6. **What must the user bring?** A Kickstart ROM of the machine and the
   game's disks. PUAE also carries AROS's free replacement Kickstart.
7. **What can a public runner test?** A small program on a disk the tests
   build themselves, booted with AROS's Kickstart (to be done).
8. **How big is it?** Small: an Amiga 500 has 1 MB of memory, an A1200
   2 MB and up to 8 MB more.

## How the core drives PUAE (2026-10-14)

PUAE is a libretro core: a program calls its `retro_*` functions and
answers its questions through callbacks. `waterbox/cinterface.c` is that
program, inside the sandbox. It is given PUAE's options when PUAE declares
them; a project's setting named like an option less `puae_` sets it, and
any other option keeps PUAE's default (the first value when the option
names none, as libretro has it). PUAE's own machine presets, Kickstart
choice and picture handling are used as upstream has them.

The libretro layer also carries about 9,000 lines of folder handling (temp
folders, unpacking archives, WHDLoad). The sandbox has no folders, so the
core gives PUAE a disk's file directly and none of that is used.

## What the first step found (2026-10-14)

- **Upstream does not build here as it is**: `libretro-core.c` uses `MAX`
  and no header it includes defines it; GCC 14 refuses the implicit
  declaration. Patch 0001 includes `retro_miscellaneous.h`.
- **A file with no folder.** The sandbox's files have no folders, so the
  Kickstarts are looked for in the folder "". PUAE's own `path_join` puts a
  "/" between a folder and a name even when the folder is empty. Patch
  0002: no folder, the bare name.
- **Disks are kept in memory.** Native and sandbox first differed in one
  byte of Slow RAM from frame 200 on. It was the disk's write protection:
  PUAE opens a disk for writing and treats it as write protected when that
  fails, and the sandbox's files cannot be written. With the disk made
  read-only natively, the two ran the same. Patch 0003 reads a disk whole
  into memory when it is put in a drive; the Amiga writes there. The
  user's file is never written to, a savestate holds the disk as the
  machine left it, and a disk can be written to as on the machine.
- Measured with Ninja Mission (1987, Mastertronic) on Kickstart 1.3, an
  Amiga 500 (PUAE's default for a disk): the title screen after 1,500
  frames. Native and sandbox print the same 15 digests of memory, picture
  and sound. A state saved and loaded every frame, a state moved to a new
  sandbox, and a state kept at frame 800 and loaded at 1,200 (79 digests
  after the load repeat those after the save) change nothing.

## The controls (2026-10-14)

- **What is in each port is a setting**: Port 1 (the mouse port) has a
  mouse by default, or a joystick, or nothing; Port 2 (the joystick port)
  has a joystick by default, or the CD32's pad, or nothing. The core adds
  UAE's own configuration lines for them (`joyport0=`, `joyport1=`,
  `joyport1mode=`) at the end of the configuration PUAE writes: patch 0001
  gives the core that hook, which later settings use too.
- **The controls reach UAE directly**, through UAE's own input functions
  (`setjoystickstate`, `setjoybuttonstate`, `setmousestate`,
  `setmousebuttonstate`, `inputdevice_do_keyboard`), once a frame before
  the frame runs. PUAE's libretro gamepad mapping (a gamepad's buttons as a
  mouse, turbo fire, a keyboard on screen, mouse speed) is given nothing:
  a movie holds the Amiga's own controls.
- **Controls**: P1 Up, Down, Left, Right, Fire, Fire 2 for the joystick in
  port 2, with Play, Rewind, Forward, Green and Yellow when it is the
  CD32's pad (Fire and Fire 2 are then its red and blue buttons); P2 the
  same for a joystick in port 1; Mouse Left, Right and Middle, and the axes
  Mouse X and Mouse Y, how far the mouse moves in a frame (-128 to 127, the
  mouse's own counts); and the 96 keys of the Amiga's keyboard, under the
  names printed on them. A key is told to the machine when it goes down and
  when it comes up.
- **Lag frames**: a frame in which the machine did not read its controls.
  Patch 0004 counts the reads of the two ports' counters (JOY0DAT,
  JOY1DAT), the paddle and button registers (POT0DAT, POT1DAT, POTGOR) and
  CIA-A's port A (the fire buttons), but not CIA-A's port A when the
  Kickstart ROM reads it: it does all the time, for the drive's status bits
  in the same byte. The keyboard is not a read: the machine is told of a
  key by an interrupt.

Measured: a press of fire, of the mouse's button and of a key each change
the run of Ninja Mission; native and sandbox print the same 52 digests with
all of them used and the mouse moved. Budokan reaches its intro, Prince of
Persia (Softgold, German) its title. Another copy of Prince of Persia
(`pop.adf`) stays on a red screen and never reads its controls; it was not
looked into. Ninja Mission on an A1200 with Kickstart 3.1 boots to the
disk's own anti-virus boot block.

## The package and the disks (2026-10-14)

- **One system, `AMIGA` ("Commodore Amiga")**, with the machine as a
  setting, Model: PUAE's presets A500 (the default), A500OG, A500PLUS,
  A600, A1200OG, A1200, A2000OG, A2000, A4030 and A4040. The CDTV and CD32
  presets are left out for now: their drives start threads (akiko.c,
  cdtv.c).
- **Settings named after PUAE's options** reach them with no code: Model is
  `puae_model`, Kickstart is `puae_kickstart` ('auto', the Model's own, or
  'aros', the free replacement PUAE carries), Video Standard is
  `puae_video_standard` (PAL or NTSC, where PUAE's default would guess from
  the file's name). Port 1 and Port 2 are the core's own.
- **Firmware**: the Kickstart ROM of each Model, under the names Amiga
  Forever gives them (`amiga-os-130.rom` and so on), which PUAE looks for,
  each known by its SHA-1, required when Kickstart is 'auto' and the Model
  is one that uses it.
- **Disks**: a project holds up to 20; the first is in the drive at the
  start. Previous Disk and Next Disk move a selector over them; Eject Disk
  takes the disk out for as long as it is held and puts the selected one
  in when it is let go (as the Saturn's drive in the Mednafen core).
  Patch 0003 keeps every disk the machine has had in a drive in a table in
  memory, so a disk taken out and put back still has what was written to
  it, and Emulator > Export Save Data writes each disk that differs from
  its file, under its own name.
- **Key bindings**: the joystick on the arrow keys with Fire on Left Ctrl
  and Fire 2 on Left Alt; the mouse on the host's mouse (its movement and
  its left and right buttons); each Amiga key on the host's key of the same
  name. The Amiga's own cursor keys, Ctrl and Left Alt ship unbound,
  because the joystick uses those host keys.

Measured: with Budokan's second disk in the drive Kickstart asks for a
disk; Next Disk and Eject Disk put the first in and its intro starts; with
no change it keeps asking. Native and sandbox print the same 40 digests
over that change, also with a state saved and loaded every frame. The
package passes Chimera's package checks (18 of 18). A project of Ninja
Mission with a key held at frame 1400 plays in Chimera's headless runner
(`chimera-run --project`, the Kickstart given with `--firmware`) to the
Chip RAM the sandbox runner leaves, and another than the idle run's.

## The tests, and what they found (2026-10-14)

**A program of the tests' own.** No Kickstart of Commodore's and no game
may be on a CI runner. PUAE carries AROS's free replacement Kickstart, and
`tests/make-amiga-disk.py` writes a disk whose boot block is a small 68000
program, hand-assembled in the script. It writes its own first 512 bytes to
sector 1700 through the drive, takes the machine over, and every frame
copies JOY1DAT, JOY0DAT, CIA-A's port A, POTGOR and the last key into Chip
RAM at 0x7E000, counts the frames, and colours the screen from what it
read. It boots with AROS (at about frame 630) and with Kickstart 1.3.

**The test script**, `waterbox/run-gate.sh`: 26 tests, 26 passed with every
file, in under two minutes. 19 need no file of anyone's: each joystick
direction and both buttons, the mouse's movement and buttons, a key going
down and up, a joystick in port 1, which controls are live for each port
setting, native and sandbox with every kind of control used, lag frames,
the three savestate tests, the disk write coming out as save data, a disk
change, and the two refusals; and in Chimera's headless runner a project of
the program. With Ninja Mission and the Kickstarts: the game runs, native
and sandbox agree, savestates, an A1200, the Starting Clock, and a change
from Budokan's second disk to its first.

**Broken on purpose once**: with Fire 2 never sent, a key never let go,
every frame said to have read the controls, Next Disk doing nothing, the
Starting Clock ignored and no disk exported, seven tests failed:
`amiga:joystick`, `amiga:keyboard`, `amiga:lag`, `amiga:disk-write`,
`amiga:disk-change`, `game:clock` and `game:disk-change`.

**What the tests found:**

- **Memory files were write protected.** UAE takes any disk held in memory
  for a packed image and write protects it (`zfile_iscompressed`). Patch
  0003 exempts the core's own copies, and a disk is now known to have
  changed by a checksum taken when it was read; a packed disk goes out as
  an `.adf`.
- **The host's clock reached the machine.** Kickstart 3.1 reads the A1200's
  battery-backed clock, and AROS reads one too; UAE takes it from `time()`
  and `localtime()`, so native (the real time, and the host's time zone)
  and sandbox (its fixed clock, UTC) ran differently from frame 40. The core
  now defines `time`, `gettimeofday`, `clock_gettime`, `localtime` and
  `localtime_r` itself: the time is the new Starting Clock setting (the
  first of January 2000 by default) plus the frames run, in UTC.
- **A missing Kickstart is refused** by its name (PUAE starts without it,
  to a grey screen).
- **Upstream reads `bplcon0` as an `int`** in `libretro-core.c`, where
  custom.c defines it as 16 bits, so two bytes of the next variable came
  with it (found by AddressSanitizer). Patch 0001 declares it as custom.c
  has it.
- **The save folder.** PUAE looks two characters back from the end of the
  save folder's name; with "." that is before the name. The save folder is
  now "saves" (nothing is saved there).
- **Locals read before they are written.** A native build with every local
  filled with zeros and one with a pattern part ways once the mouse moves,
  so PUAE reads at least one local before writing it. Both builds now fill
  locals with zeros (`-ftrivial-auto-var-init=zero`).
- **The sound rate on AROS.** After AROS starts, PUAE computed its sound
  rate from a frame of 569 lines while the frames are 313, and handed out
  486 samples a frame instead of 882. See "The two problems, fixed".

## The two problems, fixed (2026-10-14)

**The sound rate after a write of VPOSW.** When a program writes VPOSW
(the beam position), UAE measures the frame it made and takes its length
as the machine's: `maxvpos_nom`, from which it computes how many samples
of sound a frame has. AROS writes VPOSW once as it starts, making one
frame of 569 lines; UAE took 569 for good, because it looks again only
while VPOSW keeps being written, and handed out 486 samples a frame
instead of 882 from then on. Patch 0005: when the frames are back to the
machine's own length and VPOSW is no longer written, the rate is the
machine's own again. The one long frame keeps its 1,600 samples, as it
should. Ninja Mission's single short frame while loading was the same
thing. Test: `amiga:sound-rate` (frames 420 to 900 on AROS, each 882);
with the patch switched off it failed (489 frames not 882).

**The mouse, native against sandbox.** With the Amiga's own system
reading the mouse, a moved mouse left the native build and the sandbox a
count apart. The cause was in PUAE: with the x86 bridgeboard compiled out,
its stub `x86_mouse()` returned nothing (`bool x86_mouse(...) {}`). At
each frame's end `mouseupdate()` asks it whether a PC mouse is fitted; the
answer was whatever the return register held, which differs with how the
code was compiled, and it decides whether the frame's movement is handed
out at once or spread over the lines. Found by logging, in memory, the
mouse code's figures in a build at -O0 and one at -O2: they parted at the
first vertical blank after the mouse moved, where one build split the
movement with a share of 0 and the other with 1000. Patch 0007 returns
false. `-Wreturn-type` over every file PUAE compiles finds no other
function like it.

On the way: under libretro, `target_cfgfile_load()` used two name buffers
it never filled (`fname`, `cname`; the code that fills them is compiled
out), and copied one into `config_filename`; a build that fills locals
with a pattern stopped there on glibc's buffer check. Patch 0006 empties
them.

Measured: five runs with the mouse moved (Ninja Mission twice, Budokan
after a disk change, an A1200, the test program on AROS) give the same
digests in the native build at -O2 and at -O0, with locals filled with
zeros and with a pattern, with the sandbox's code flags, and in the
sandbox. `game:sandbox` moves the mouse again; with patch 0007 switched
off it failed.

Counts: the test script 27 passed, 0 failed, 0 skipped; the window test 4
passed.

## 2026-10-14: the default bindings name keys Chimera has

**Found.** Chimera hands a key to the bindings under one name only: the
name DistinctKeyNameOverrides gives it ("Number1", "Enter", "Backtick",
"KeypadAdd"), or else its DistinctKey name (Input.cs). By default it also
reads the left and right Shift, Ctrl and Alt as one key each, "Shift",
"Ctrl" and "Alt" (Config.MergeLAndRModifierKeys). A binding to any other
name never fires, so those controls shipped unbound.

**Decided.** The bindings use those names; where both keys of a modifier
were bound, the left one is on the merged key and the right one ships
unbound. The test `package:key-names` (`tests/check-key-names.py`) reads the
names from Chimera's source and checks every binding.

Measured: 20 bindings were changed: the digits, Backtick, Enter, the keypad's
+, - and ., Fire on Ctrl and Fire 2 on Alt (were LeftControl and LeftAlt),
Left Shift on Shift; Right Shift and Right Alt now ship unbound (Alt is
Fire 2's).
