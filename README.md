# chimera-core-puae

A core for [Chimera](https://github.com/ToolAssisted-run/chimera), the
frontend for tool-assisted speedruns, for the **Commodore Amiga**. It is
built on [PUAE](https://github.com/libretro/libretro-uae), the libretro port
of the Amiga emulator UAE, and produces one file, `puae.chimeraCore`, that
goes in Chimera's `Cores` folder.

## Status

New, and not published yet.

- Games on floppy disks run, and the sandboxed core gives the same results
  as a plain native build: picture, sound, joystick, keyboard, the mouse,
  lag frames, savestates and disk changes. Tried: Ninja Mission
  (on an A500 and an A1200), Budokan (two disks) and Prince of Persia.
- On Windows, Chimera's headless runner plays a project of a game and of
  the core's test program to the same memory and picture as on Linux.
  Chimera's window was tried on Linux only.
- Not there: hard disks and WHDLoad, the CD32 and CDTV, a second floppy
  drive, IPF disk images.

## What you bring

- The game's disks: `.adf` files (or `.adz`, `.dms`, `.fdi`). A project
  holds up to 20; the first is in the drive at the start.
- The Kickstart ROM of the model, as Chimera's Firmware page asks for it:
  `amiga-os-130.rom` (Kickstart 1.3) for the A500, `amiga-os-310-a1200.rom`
  for the A1200, and so on. With Kickstart set to `aros`, nothing is needed:
  PUAE carries AROS's free replacement Kickstart, which some games run with.

## Buttons and settings

- **Port 2** (the joystick port): P1 Up, Down, Left, Right, Fire and
  Fire 2, and with the CD32's pad Play, Rewind, Forward, Green and Yellow.
- **Port 1** (the mouse port): Mouse Left, Right and Middle, and Mouse X and
  Mouse Y, how far the mouse moves in a frame; or P2's joystick.
- **The keyboard**: all 96 keys, under the names printed on them.
- **The drive**: Previous Disk and Next Disk choose a disk; Eject Disk
  takes the disk out while it is held and puts the chosen one in.
- **Settings**: Model (A500, A500+, A600, A1200, A2000, A4000, and the
  smaller versions of some), Kickstart, Video Standard (PAL or NTSC),
  Starting Clock (the battery-backed clock), and what is in each port. All
  of them are part of the machine, so a movie needs the same values.

The Amiga writes to copies of its disks in memory, never to your files, and
a savestate holds them. Emulator > Export Save Data writes each disk the
Amiga changed, as an `.adf`.

RAM Watch and RAM Search read the Amiga's memory as Chip RAM, Slow RAM and
Fast RAM.

## Building and testing

[docs/BUILDING.md](docs/BUILDING.md) has every step. [docs/PLAN.md](docs/PLAN.md)
is the design log: what was decided, why, and what was measured.

## Licence

This repository's own code is MIT. PUAE is under the GNU General Public
License version 2, and so are the patches to it here and the program in the
built package. The package carries the licence texts of everything in it,
including AROS's replacement Kickstart (AROS Public License). It contains no
Kickstart of Commodore's and no game. See [LICENSE](LICENSE).
