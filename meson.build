# chimera-core-puae: PUAE, the libretro port of the Amiga emulator UAE, in
# miniBox's sandbox, as a Chimera core.
#
# Native reference build (run-native + run-wbx under the build directory):
#     meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes:
#     sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest core.wbx
#
# Upstream builds with one Makefile for many platforms; its file list is
# Makefile.common. The list below is that list, without the recompiler (as
# upstream has it), the CD32's video decoder and CHD discs (both optional
# there). The emulator is started by waterbox/cinterface.c from a
# configuration it writes, not through upstream's libretro entry points;
# upstream's libretro files are compiled for the picture, sound and input
# code the emulator calls into. Both flavours compile the SAME sources; only
# emulibc.h differs (waterbox/native-shim for the reference).
project('chimera-core-puae', 'c',
  default_options: ['c_std=gnu99', 'optimization=2', 'debug=false', 'warning_level=0'])

ua = 'extern/libretro-uae'
emu = ua / 'sources/src'
lr = ua / 'libretro'
deps = ua / 'deps'
rd = ua / 'retrodep'
lrc = ua / 'libretro-common'

# the submodule pin is upstream as it is; the patches are laid over it
# (waterbox/apply-patches.sh does nothing to a tree that has them)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/chimera-common-minibox)')
  endif
endif

# upstream's own defines (Makefile), and what the patches test for
defs = [
  '-D__LIBRETRO__', '-DINLINE=inline', '-DHAVE_ZLIB', '-DHAVE_7ZIP', '-D_7ZIP_ST',
  '-DCHIMERA_CORE',
]
# signed overflow wraps, as UAE's CPU code expects; one answer from a
# floating-point expression on every machine: no fused multiply-adds
flags = [
  '-fwrapv',
  '-fno-strict-aliasing',
  '-ffp-contract=off',
  # PUAE reads at least one local variable before it writes it, somewhere
  # on the mouse's path (measured: a native build with every such variable
  # filled with zeros and one with a pattern part ways once the mouse moves).
  # What it read was whatever the C library left on the stack, which differs
  # between the native build (glibc) and the sandbox (musl). Filled with
  # zeros, it is the same in every build. (It is not the whole difference
  # between the two builds once the mouse moves: docs/PLAN.md.)
  '-ftrivial-auto-var-init=zero',
]
add_project_arguments(defs + flags, language: 'c')

inc = include_directories(
  'waterbox',
  emu, emu / 'include', ua, rd,
  deps / '7zip', deps / 'libz',
  lr, lrc / 'include',
)

uae_names = [
  lr / 'libretro-core.c', lr / 'libretro-mapper.c', lr / 'libretro-dc.c', lr / 'libretro-glue.c',
  lr / 'libretro-vkbd.c', lr / 'libretro-graph.c',
  deps / 'libz/unzip.c', deps / 'libz/ioapi.c',
]
foreach n : [
    'a2065.c', 'a2091.c', 'akiko.c', 'amax.c', 'ar.c', 'arcadia.c', 'aros.rom.c', 'audio.c', 'autoconf.c',
    'blitfunc.c', 'blittable.c', 'blitter.c', 'blkdev.c', 'blkdev_cdimage.c', 'bsdsocket.c', 'calc.c',
    'caps/uae_dlopen.c', 'cd32_fmv.c', 'cd32_fmv_genlock.c', 'cda_play.c', 'cdrom.c',
    'cdtv.c', 'cdtvcr.c', 'cfgfile.c', 'cia.c', 'cpuboard.c', 'cpudefs.c',
    'cpuemu_0.c', 'cpuemu_11.c', 'cpuemu_13.c', 'cpuemu_20.c', 'cpuemu_21.c', 'cpuemu_22.c', 'cpuemu_23.c',
    'cpuemu_24.c', 'cpuemu_31.c', 'cpuemu_32.c', 'cpuemu_33.c', 'cpuemu_34.c', 'cpuemu_35.c', 'cpuemu_40.c',
    'cpuemu_50.c', 'cpummu.c', 'cpummu30.c', 'cpustbl.c', 'crc32.c', 'custom.c', 'debug.c', 'debugmem.c',
    'devices.c', 'disasm.c', 'disk.c', 'diskutil.c', 'dongle.c', 'draco.c', 'drawing.c', 'driveclick.c',
    'ethernet.c', 'events.c', 'expansion.c', 'fdi2raw.c', 'filesys.c', 'filesys_unix.c', 'flashrom.c',
    'fpp.c', 'fpp_native.c', 'fpp_softfloat.c', 'fsdb.c', 'fsdb_unix.c', 'fsusage.c', 'gayle.c',
    'gfxboard.c', 'gfxlib.c', 'gfxutil.c', 'hardfile.c', 'hardfile_unix.c', 'hrtmon.rom.c', 'ide.c',
    'idecontrollers.c', 'identify.c', 'ini.c', 'inputdevice.c', 'isofs.c', 'keybuf.c', 'main.c', 'memory.c',
    'misc.c', 'missing.c', 'native2amiga.c', 'ncr_scsi.c', 'ncr9x_scsi.c', 'newcpu.c', 'newcpu_common.c',
    'pci.c', 'picasso96.c', 'readcpu.c', 'rommgr.c', 'rtc.c', 'sampler.c', 'sana2.c', 'savestate.c', 'scsi.c',
    'scsiemul.c', 'scsitape.c', 'sndboard.c', 'specialmonitors.c', 'statusline.c', 'test_card.c', 'traps.c',
    'uaelib.c', 'uaenet.c', 'uaeresource.c', 'uaeserial.c', 'writelog.c', 'x86.c', 'zfile.c',
    'zfile_archive.c', 'dsp3210/dsp_glue.c', 'dsp3210/DSP3210_emulation.c',
    'softfloat/softfloat.c', 'softfloat/softfloat_decimal.c', 'softfloat/softfloat_fpsp.c',
    'archivers/dms/crc_csum.c', 'archivers/dms/getbits.c', 'archivers/dms/maketbl.c', 'archivers/dms/pfile.c',
    'archivers/dms/tables.c', 'archivers/dms/u_deep.c', 'archivers/dms/u_heavy.c', 'archivers/dms/u_init.c',
    'archivers/dms/u_medium.c', 'archivers/dms/u_quick.c', 'archivers/dms/u_rle.c',
    'archivers/lha/crcio.c', 'archivers/lha/dhuf.c', 'archivers/lha/header.c', 'archivers/lha/huf.c',
    'archivers/lha/larc.c', 'archivers/lha/lhamaketbl.c', 'archivers/lha/lharc.c', 'archivers/lha/shuf.c',
    'archivers/lha/slide.c', 'archivers/lha/uae_lha.c', 'archivers/lha/util.c', 'archivers/mp2/kjmp2.c',
  ]
  uae_names += emu / n
endforeach
foreach n : ['gui.c', 'main.c', 'mman.c', 'parser.c', 'serial_host.c', 'machdep/support.c',
             'sounddep/sound.c', 'threaddep/thread.c', 'stubs/inputrecord.c']
  uae_names += rd / n
endforeach
foreach n : ['adler32.c', 'crc32.c', 'deflate.c', 'gzclose.c', 'gzlib.c', 'gzread.c', 'gzwrite.c',
             'inffast.c', 'inflate.c', 'inftrees.c', 'trees.c', 'zutil.c']
  uae_names += deps / 'libz' / n
endforeach
foreach n : ['7zArcIn.c', '7zBuf.c', '7zCrc.c', '7zCrcOpt.c', '7zDec.c', '7zFile.c', '7zStream.c',
             'Bcj2.c', 'Bra.c', 'Bra86.c', 'BraIA64.c', 'CpuArch.c', 'Delta.c', 'Lzma2Dec.c', 'LzmaDec.c']
  uae_names += deps / '7zip' / n
endforeach
foreach n : ['compat/compat_strl.c', 'compat/compat_strcasestr.c', 'compat/fopen_utf8.c',
             'encodings/encoding_utf.c', 'file/file_path.c', 'file/file_path_io.c', 'file/retro_dirent.c',
             'streams/file_stream.c', 'streams/file_stream_transforms.c', 'string/stdstring.c',
             'time/rtime.c', 'vfs/vfs_implementation.c']
  uae_names += lrc / n
endforeach

# IPF disks: PUAE's caps/caps.c loads the SPS library, which may not be built
# in (its licence forbids commercial use) and which the sandbox could not
# load; waterbox/ipf-reader.c gives disk.c the same calls on MAME's reader
core_sources = files(uae_names) + files('waterbox/cinterface.c', 'waterbox/chimera-threads.c', 'waterbox/ipf-reader.c')

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources + files('waterbox/guest-syscalls.c'),
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lgcc', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  # the IPF reader alone, checked against images tests/make-ipf.py writes
  executable('ipf-check', files('waterbox/ipf-reader.c', 'tests/ipf-check/ipf-check.c'),
    include_directories: include_directories('tests/ipf-check', emu / 'include'))

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so'],
    build_rpath: mbhost)
endif
