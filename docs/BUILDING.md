# Building

This core is built on Linux. The result, `core.wbx`, is one file that runs in
Chimera on Linux and on Windows.

## What is needed

- GCC 13 or newer, meson, ninja, git, python3.
- A checkout of Chimera with miniBox built, for the sandbox toolchain
  (Chimera's `AGENTS.md`, "Build"). Below, `<miniBox>` is its
  `extern/chimera-common-minibox` folder.

## PUAE's source

PUAE is a submodule, `extern/libretro-uae`, at a commit of its main branch
(docs/PLAN.md says which, and why):

```sh
git submodule update --init
```

A change to PUAE is kept as a numbered patch in `patches/`.
`waterbox/apply-patches.sh` lays the patches over the submodule (the build
runs it; it does nothing to a tree that has them). To make a patch, edit the
files in `extern/libretro-uae`, build and test, add the file to the list in
`waterbox/make-patches.sh` if it is new, and run that script.

## What is compiled

`meson.build` lists PUAE's files: the same list as its own
`Makefile.common`, without the recompiler (upstream leaves it out too), the
CD32's video decoder and CHD discs, and without `caps/caps.c`, which
loads the SPS library for IPF disks: `waterbox/ipf-reader.c` takes its place
(docs/PLAN.md, "IPF disks"). Upstream's libretro layer is compiled
and used: `waterbox/cinterface.c` calls its `retro_*` functions and answers
its questions, as a libretro frontend does.

## The two flavors

- `native`: for the host. `run-native` is the reference the tests compare
  the sandbox with. It also builds `ipf-check`, the IPF reader alone, which
  the tests drive as the floppy drive does.
- `guest`: for miniBox's sandbox. This is `core.wbx`, and `run-wbx` runs it.

```sh
meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest
```

Both runners take a work folder that holds what the sandbox would see: the
disks under their names, a file `slots` (`{"disk":["game.adf"]}`), a file
`settings` (`{}` or, say, `{"model":"A1200"}`), and the Kickstart ROMs
under the names the package gives them (`amiga-os-130.rom` and so on).
`waterbox/gate-harness.h` lists the options they share (`--frames`,
`--report`, `--press`, `--axis`, `--vid-out`, `--dump-domain`,
`--savedata-out`, `--list-panel`). `CHIMERA_PUAE_LOG=1` makes the native
runner print PUAE's whole log, with the configuration it generated.

## The package

```sh
waterbox/build-package.sh -r <chimera checkout>
```

builds the sandbox core and writes `build/Cores/puae.chimeraCore` in that
checkout. `tests/make-project.py` writes a project for a game, which
Chimera's `chimera-run --project` then plays (give it the Kickstart with
`--firmware amiga-os-130.rom=<file>`).

## The tests

```sh
CHIMERA_ROOT=<chimera checkout> waterbox/run-gate.sh
```

runs every test and says, for each, what was compared. The "amiga:" and
"ipf:" tests need nothing: `tests/make-amiga-disk.py` writes a disk whose
boot block is a small test program, which boots with the free Kickstart
PUAE carries (AROS's), and `tests/make-ipf.py` writes IPF images of it. The others need files that cannot be in this repository, and say
SKIP without them:

| Tests | What they need | Where the script looks | Or name it with |
|---|---|---|---|
| `game:` | a game on one disk, as a folder with an `.adf` file | `tests/games-local/ninja` | `AMIGA_GAME` |
| `game:disk-change` | a game on two disks | `tests/games-local/budokan` | `AMIGA_GAME2` |
| `game:ipf` | a game on one disk as an `.ipf` file (the test's timings are Prince of Persia's) | `tests/games-local/pop` | `AMIGA_IPF_GAME` |
| `game:` | Kickstart 1.3 (and 3.1 for the A1200), under the package's names | `tests/roms-local/amiga-os-130.rom`, `amiga-os-310-a1200.rom` | `AMIGA_KICKSTARTS` (a folder) |
| `cd32:` | the CD32's Kickstart and extended ROM; the tests write an audio CD of their own (`tests/make-audio-cd.py`) | `tests/roms-local/amiga-os-310-cd32.rom`, `amiga-os-310-cd32-ext.rom` | `AMIGA_KICKSTARTS` |
| `cd32:game` | a CD32 game, as a folder with a `.cue` and its track files | `tests/games-local/worms` | `AMIGA_CD32_GAME` |
| `cdtv:` | Kickstart 1.3 and the CDTV's extended ROM; the same audio CD | `tests/roms-local/amiga-os-130.rom`, `amiga-os-130-cdtv-ext.rom` | `AMIGA_KICKSTARTS` |

Copy the files there; do not link them from where you keep them. Without
`CHIMERA_ROOT` the test that runs the package in Chimera's engine is
skipped.

`bash tests/run-frontend.sh --chimera-root <chimera checkout>` is the window
test: the package in Chimera's own window (run headless: it needs no
display; Chimera's `tools/ci-chimera.sh` builds it). It takes the same files.

A third native build keeps upstream's own threads, which wait on the
computer's clock, in place of the core's (`waterbox/chimera-threads.c`):
`meson setup build/meson-realthreads -Dminibox_dir=<miniBox>
-Dc_args=-DCHIMERA_REAL_THREADS -Dc_link_args=-lpthread`. Its runs are not
the same twice; it answers whether something the core does with a CD is
upstream's doing or the thread layer's.

## What CI does

`.github/workflows/chimera.yml` builds the core from a fresh clone, runs
both test scripts and Chimera's checks of the package, and publishes the
package. No Kickstart of Commodore's and no game may be put on a runner, so
the tests that need one are skipped there; the "amiga:" tests, with the
program on its own disk and AROS's Kickstart, run in full.
