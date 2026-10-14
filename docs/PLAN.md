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
  mouse's own counts); and the 95 keys of the Amiga's keyboard, under the
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
