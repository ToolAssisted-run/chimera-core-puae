# AGENTS.md - PUAE core for Chimera

This repository builds PUAE (https://github.com/libretro/libretro-uae), the
libretro port of the Amiga emulator UAE, as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `puae.chimeraCore`: PUAE compiled to run
inside Chimera's sandbox (miniBox), a small program around it, and the
declarations Chimera reads (buttons, settings, file types, firmware).

Read [docs/BUILDING.md](docs/BUILDING.md) before building and
[docs/PLAN.md](docs/PLAN.md) before changing how the core works: it is the
design log, with what was decided, why, and what was measured.

## Layout

- `extern/libretro-uae` - PUAE at a commit of its main branch, a submodule.
  Never edited in place: a change is a numbered patch in `patches/`, laid
  over the submodule by `waterbox/apply-patches.sh` and written by
  `waterbox/make-patches.sh` (its list says which file is in which patch; a
  file is in one patch, whole). Every change is inside `#ifdef
  CHIMERA_CORE` with a comment that says why.
- `meson.build` - the build of both flavors, with PUAE's files listed by
  hand from its own `Makefile.common`.
- `waterbox/cinterface.c` - the program around PUAE: it answers PUAE's
  libretro questions from the project (its settings and files), drives
  `retro_load_game` and `retro_run`, gives the controls to UAE's input
  functions, and is what Chimera's engine calls. It also defines the C
  library's time functions (the core's own clock). The same file is
  compiled for the sandbox and for the host.
- `waterbox/guest-syscalls.c` - sandbox-only answers to "is this file
  there" (the sandbox has files and no folders).
- `waterbox/ipf-reader.c` - IPF disks: MAME's reader, ported to C, in place
  of PUAE's `caps/caps.c`, which loads the SPS library (its licence forbids
  commercial use, so it cannot be built in). `tests/ipf-check/` builds it
  alone for the tests.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json`, `licenses/` - what the package declares.
- `waterbox/run-native.c`, `waterbox/run-wbx.c` - the native runner and the
  sandbox runner. They take the same options and print the same lines
  (`waterbox/gate-harness.h` is the part they share).
- `waterbox/*.sh` - `apply-patches.sh`, `make-patches.sh`, `setup-guest.sh`
  (configures the sandbox build), `build-package.sh`, `run-gate.sh` (the
  test script).
- `tests/` - `make-amiga-disk.py` (writes the disk with the test program),
  `make-ipf.py` (writes an IPF image of a disk, with weak bits and timed
  tracks when asked),
  `run-frontend.sh` (the window test, with its helpers), `make-project.py`
  (writes a project for a game).
- `build/` - every build output. `tests/roms-local/` - Kickstart ROMs, and
  `tests/games-local/` - games, for the tests that need them. All three
  are ignored by git.

## Build and test

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest
waterbox/build-package.sh -r <chimera checkout>
CHIMERA_ROOT=<chimera checkout> waterbox/run-gate.sh
TMPDIR=$PWD/build/t bash tests/run-frontend.sh --chimera-root <chimera checkout>
```

Run both test scripts before every commit that touches what they cover, and
say in the commit what ran, with the numbers. A new test is broken on
purpose once and seen to fail.

## Rules of the repository

- **One thread.** A core runs on one thread on one clock. PUAE starts
  threads only for hard disks, CD drives and networking, which the core
  does not use; the CD32 and CDTV models are left out for that reason.
- **No host clock.** The core defines `time`, `gettimeofday`,
  `clock_gettime`, `localtime` and `localtime_r`: the time is the Starting
  Clock setting plus the frames run, in UTC. The battery-backed clock of
  the A500+, A600, A1200 and A4000 reads it. The test `game:clock` holds it.
- **The machine's state is its memory.** A savestate is the sandbox's copy
  of the core's memory, so nothing the machine changes may live in a file.
  Every disk is read whole into memory and written there (patch 0003); the
  user's file is never written to, and Export Save Data writes the disks
  that changed.
- **The controls reach UAE directly**, not through PUAE's libretro gamepad
  mapping (`give_controls` in `cinterface.c`). They are in two places that
  must agree: the tables in `cinterface.c` and the `input` of the machine
  in `waterbox.config`. The test `package:controls` compares them;
  `package:key-names` checks that the default bindings name keys Chimera
  knows (`tests/check-key-names.py`).
- **PUAE's options are settings by name**: a setting named like an option
  less `puae_` sets it (`model` is `puae_model`). An option that names no
  default has its first value, as libretro has it.
- **The test program keeps a press only while it lasts.** A test that
  compares memory at the end holds the control to the end. A movie's row N
  is the test runner's frame N + 1. AROS starts the test program at about
  frame 630.
- **No Kickstart ROM of Commodore's, no game** is committed, uploaded to CI
  or attached to an issue. Test content stays under `tests/roms-local` and
  `tests/games-local`, and is copied there, never linked from where the
  owner keeps it.
- **UB in PUAE shows as native against sandbox.** A function that returns
  nothing, a local read before it is written, a variable declared with
  another type than it has: each made the two builds part. Build the
  native runner at -O0 and with `-ftrivial-auto-var-init=pattern`; if the
  results move, find the cause (docs/PLAN.md has how the last ones were
  found).
- **Say what was not tried.** A model that started and never ran a game
  has not been shown to work.
- **Write in plain language**: commit messages, documents, comments and
  setting descriptions are read by people who have never seen this
  repository. `.md` files are ASCII.
- **Commits**: `type(scope): what changed`; a short body with what changed,
  why, and how it was tested, with the numbers. Stage explicit paths. No
  amending, no force push.

## What is the owner's to decide

Publishing, the roster row in Chimera, moving to another PUAE commit, and
anything a licence does not plainly allow.
