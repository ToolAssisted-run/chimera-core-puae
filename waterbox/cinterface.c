/* cinterface.c - PUAE's Amiga as a Chimera core: the functions Chimera calls.
 *
 * PUAE is a libretro core: a program around it calls its retro_* functions
 * and answers its questions through callbacks. This file is that program,
 * inside the sandbox. It answers the questions from the project (its
 * settings and its files), hands the picture and the sound out, and gives
 * the emulator the controls Chimera sets for each frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "libretro.h"

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "inputdevice.h"
#include "disk.h"
#include "libretro-mapper.h"   /* RETRO_DEVICE_PUAE_CD32PAD */

/* ---- what went wrong, for Chimera to show -------------------------------- */
/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). TEXT_ARG is a
 * string of its own so the byte after it is never read as more of its hex. */
#define TEXT_ARG "\x1f"

static char g_loadError[512];

/* ---- the emulator's options ------------------------------------------------
 * PUAE declares its options (key, values, default) when it is given its
 * environment. A project's setting with the option's name less "puae_" sets
 * it; any other option keeps PUAE's default, which is the first value when
 * the option names none (libretro's rule). */
#define MAX_OPTIONS 256
static struct { const char *key; const char *def; char value[128]; } g_options[MAX_OPTIONS];
static int g_optionCount;

static void take_option(const char *key, const char *def)
{
	if (!key || g_optionCount >= MAX_OPTIONS) return;
	for (int i = 0; i < g_optionCount; i++)
		if (!strcmp(g_options[i].key, key)) return;
	g_options[g_optionCount].key = key;
	g_options[g_optionCount].def = def ? def : "";
	g_options[g_optionCount].value[0] = '\0';
	const char *name = !strncmp(key, "puae_", 5) ? key + 5 : key;
	if (wbx_setting_str(name, g_options[g_optionCount].value, sizeof g_options[g_optionCount].value) <= 0)
		snprintf(g_options[g_optionCount].value, sizeof g_options[g_optionCount].value, "%s", g_options[g_optionCount].def);
	g_optionCount++;
}

static const char *option_value(const char *key)
{
	for (int i = 0; i < g_optionCount; i++)
		if (!strcmp(g_options[i].key, key)) return g_options[i].value;
	return NULL;
}

/* ---- the picture -------------------------------------------------------- */
#define MAX_W 1440
#define MAX_H 576
ECL_INVISIBLE static uint32_t g_picture[MAX_W * MAX_H];
static int g_width = 720, g_height = 576;
static int g_render = 1;

/* ---- the sound ---------------------------------------------------------- */
#define MAX_SAMPLES 4096
ECL_INVISIBLE static int16_t g_audio[MAX_SAMPLES * 2];
static int g_audioFrames;

/* ---- the controls -------------------------------------------------------
 * The Amiga's two ports and its keyboard, given to UAE's own input
 * functions (inputdevice.c) once a frame, before the frame runs. What is in
 * each port is a setting: port 1 (the mouse port) has a mouse by default,
 * port 2 (the joystick port) a joystick. UAE's joystick 0 is the one in
 * port 2, its joystick 1 the one in port 1, its mouse 0 the one in port 1. */
enum {
	/* the joystick, or the CD32's pad, in port 2 */
	kP1Up, kP1Down, kP1Left, kP1Right, kP1Fire, kP1Fire2,
	kP1Play, kP1Rewind, kP1Forward, kP1Green, kP1Yellow,
	/* the joystick in port 1 */
	kP2Up, kP2Down, kP2Left, kP2Right, kP2Fire, kP2Fire2,
	/* the mouse in port 1 */
	kMouseLeft, kMouseRight, kMouseMiddle,
	/* the drive: a selector over the project's disks, and the disk out */
	kPreviousDisk, kNextDisk, kEjectDisk,
	kFirstKey
};
static const struct { const char *name; int code; } kKeys[] = {
	{ "Esc", 0x45 }, { "F1", 0x50 }, { "F2", 0x51 }, { "F3", 0x52 }, { "F4", 0x53 }, { "F5", 0x54 },
	{ "F6", 0x55 }, { "F7", 0x56 }, { "F8", 0x57 }, { "F9", 0x58 }, { "F10", 0x59 },
	{ "`", 0x00 }, { "1", 0x01 }, { "2", 0x02 }, { "3", 0x03 }, { "4", 0x04 }, { "5", 0x05 }, { "6", 0x06 },
	{ "7", 0x07 }, { "8", 0x08 }, { "9", 0x09 }, { "0", 0x0A }, { "-", 0x0B }, { "=", 0x0C }, { "\\", 0x0D },
	{ "Backspace", 0x41 }, { "Del", 0x46 }, { "Help", 0x5F },
	{ "Tab", 0x42 }, { "Q", 0x10 }, { "W", 0x11 }, { "E", 0x12 }, { "R", 0x13 }, { "T", 0x14 }, { "Y", 0x15 },
	{ "U", 0x16 }, { "I", 0x17 }, { "O", 0x18 }, { "P", 0x19 }, { "[", 0x1A }, { "]", 0x1B }, { "Return", 0x44 },
	{ "Ctrl", 0x63 }, { "Caps Lock", 0x62 }, { "A", 0x20 }, { "S", 0x21 }, { "D", 0x22 }, { "F", 0x23 },
	{ "G", 0x24 }, { "H", 0x25 }, { "J", 0x26 }, { "K", 0x27 }, { "L", 0x28 }, { ";", 0x29 }, { "'", 0x2A },
	{ "#", 0x2B },
	{ "Left Shift", 0x60 }, { "<>", 0x30 }, { "Z", 0x31 }, { "X", 0x32 }, { "C", 0x33 }, { "V", 0x34 },
	{ "B", 0x35 }, { "N", 0x36 }, { "M", 0x37 }, { ",", 0x38 }, { ".", 0x39 }, { "/", 0x3A }, { "Right Shift", 0x61 },
	{ "Left Alt", 0x64 }, { "Left Amiga", 0x66 }, { "Space", 0x40 }, { "Right Amiga", 0x67 }, { "Right Alt", 0x65 },
	{ "Cursor Up", 0x4C }, { "Cursor Down", 0x4D }, { "Cursor Left", 0x4F }, { "Cursor Right", 0x4E },
	{ "Keypad (", 0x5A }, { "Keypad )", 0x5B }, { "Keypad /", 0x5C }, { "Keypad *", 0x5D },
	{ "Keypad 7", 0x3D }, { "Keypad 8", 0x3E }, { "Keypad 9", 0x3F }, { "Keypad -", 0x4A },
	{ "Keypad 4", 0x2D }, { "Keypad 5", 0x2E }, { "Keypad 6", 0x2F }, { "Keypad +", 0x5E },
	{ "Keypad 1", 0x1D }, { "Keypad 2", 0x1E }, { "Keypad 3", 0x1F }, { "Keypad Enter", 0x43 },
	{ "Keypad 0", 0x0F }, { "Keypad .", 0x3C },
};
#define KEY_COUNT ((int)(sizeof kKeys / sizeof kKeys[0]))
#define BUTTON_COUNT (kFirstKey + KEY_COUNT)
static const char *const kPortButtonNames[kFirstKey] = {
	"P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 Fire", "P1 Fire 2",
	"P1 Play", "P1 Rewind", "P1 Forward", "P1 Green", "P1 Yellow",
	"P2 Up", "P2 Down", "P2 Left", "P2 Right", "P2 Fire", "P2 Fire 2",
	"Mouse Left", "Mouse Right", "Mouse Middle",
	"Previous Disk", "Next Disk", "Eject Disk",
};
static char g_keyNames[KEY_COUNT][24];
static int g_held[BUTTON_COUNT], g_was[BUTTON_COUNT];
enum { kMouseX, kMouseY, kAxes };
static const char *const kAxisNames[kAxes] = { "Mouse X", "Mouse Y" };
static int g_axis[kAxes];
static int g_inputRead;
extern int chimera_input_reads;   /* inputdevice.c (patch 0004) */

/* what is in each port */
enum { kNone, kMouse, kJoystick, kPad };
static int g_port1 = kMouse, g_port2 = kJoystick;

static int port_setting(const char *name, int otherwise)
{
	char v[32];
	if (wbx_setting_str(name, v, sizeof v) <= 0) return otherwise;
	if (!strcmp(v, "none")) return kNone;
	if (!strcmp(v, "mouse")) return kMouse;
	if (!strcmp(v, "joystick")) return kJoystick;
	if (!strcmp(v, "cd32pad")) return kPad;
	return otherwise;
}

/* the project's disks, in order; the first is in the drive at the start */
#define MAX_DISKS 20
static char g_disks[MAX_DISKS][256];
static int g_diskCount, g_selected;
/* the CD32 and the CDTV take a disc, not floppy disks */
static int g_isCd, g_isCdtv;
static char g_nvram[256];   /* the save memory the project brings, or "" */

static int button_active(int i)
{
	if (i >= kFirstKey) return 1;
	if (i <= kP1Fire2) return g_port2 == kJoystick || g_port2 == kPad;
	if (i <= kP1Yellow) return g_port2 == kPad;
	if (i <= kP2Fire2) return g_port1 == kJoystick;
	if (i <= kMouseMiddle) return g_port1 == kMouse;
	if (g_isCd) return 0;
	return g_diskCount > 1 || i == kEjectDisk;
}

/* UAE's configuration lines for the ports (patch 0005 adds them last) */
static char g_extraConfig[1024];
const char *chimera_extra_config(void)
{
	static const char *const port1[] = { "joyport0=none\n", "joyport0=mouse\n", "joyport0=joy1\njoyport0mode=djoy\n", "" };
	static const char *const port2[] = { "joyport1=none\n", "", "joyport1=joy0\njoyport1mode=djoy\n", "joyport1=joy0\njoyport1mode=cd32joy\n" };
	/* the CD32's save memory is read from the project's file, if it brings
	 * one, and then kept in memory only (patch 0009) */
	snprintf(g_extraConfig, sizeof g_extraConfig, "%s%sflash_file=%s\n", port1[g_port1], port2[g_port2], g_nvram);
	return g_extraConfig;
}

/* one joystick's directions and two buttons, as UAE takes them */
static void give_joystick(int joy, int up)
{
	setjoystickstate(joy, 0, g_held[up + 2] ? -1 : g_held[up + 3] ? 1 : 0, 1);
	setjoystickstate(joy, 1, g_held[up] ? -1 : g_held[up + 1] ? 1 : 0, 1);
	setjoybuttonstate(joy, 0, g_held[up + 4]);
	setjoybuttonstate(joy, 1, g_held[up + 5]);
}

/* A joystick's buttons are numbered as the device's own (not as UAE's bits
 * for what the port reads): on the CD32's pad in a port in its pad mode,
 * UAE gives 0 and 1 to red and blue and 2 to 6 to green, yellow, rewind,
 * forward and play (inputdevice.c, the CD32 mapping; libretro-mapper.c
 * numbers them so). */
enum { kPadGreen = 2, kPadYellow, kPadRewind, kPadForward, kPadPlay };

static void give_controls(void)
{
	if (g_port2 == kJoystick || g_port2 == kPad)
		give_joystick(0, kP1Up);
	if (g_port2 == kPad)
	{
		setjoybuttonstate(0, kPadPlay, g_held[kP1Play]);
		setjoybuttonstate(0, kPadRewind, g_held[kP1Rewind]);
		setjoybuttonstate(0, kPadForward, g_held[kP1Forward]);
		setjoybuttonstate(0, kPadGreen, g_held[kP1Green]);
		setjoybuttonstate(0, kPadYellow, g_held[kP1Yellow]);
	}
	if (g_port1 == kJoystick)
		give_joystick(1, kP2Up);
	if (g_port1 == kMouse)
	{
		if (g_axis[kMouseX]) setmousestate(0, 0, g_axis[kMouseX], 0);
		if (g_axis[kMouseY]) setmousestate(0, 1, g_axis[kMouseY], 0);
		for (int b = 0; b < 3; b++)
			if (g_held[kMouseLeft + b] != g_was[kMouseLeft + b])
				setmousebuttonstate(0, b, g_held[kMouseLeft + b]);
	}
	/* the drive: Previous and Next move the selector (on the press); Eject
	 * takes the disk out for as long as it is held, and puts the selected
	 * one in when it is let go */
	if (g_held[kPreviousDisk] && !g_was[kPreviousDisk] && g_diskCount > 0)
		g_selected = (g_selected + g_diskCount - 1) % g_diskCount;
	if (g_held[kNextDisk] && !g_was[kNextDisk] && g_diskCount > 0)
		g_selected = (g_selected + 1) % g_diskCount;
	if (g_held[kEjectDisk] && !g_was[kEjectDisk])
		disk_eject(0);
	else if (!g_held[kEjectDisk] && g_was[kEjectDisk] && g_diskCount > 0)
		disk_insert(0, g_disks[g_selected]);
	/* a key is told when it goes down and when it comes up, as a keyboard does */
	for (int k = 0; k < KEY_COUNT; k++)
		if (g_held[kFirstKey + k] != g_was[kFirstKey + k])
			inputdevice_do_keyboard(kKeys[k].code, g_held[kFirstKey + k]);
	memcpy(g_was, g_held, sizeof g_was);
}

/* ---- the clock ----------------------------------------------------------
 * UAE reads the time for the battery-backed clock of the A500+, A600, A1200
 * and A4000 (rtc.c, cia.c) and for the dates it writes. The time it is given
 * is the project's Starting Clock plus the time the machine has run, counted
 * in frames: the same in every run and in every build. These definitions
 * take the place of the C library's, for UAE and for anything else in the
 * core; localtime() is UTC, as the sandbox's C library has it. */
static int64_t g_startTime = 946684800;   /* the first of January 2000 */
static uint64_t g_frameCount;
static double g_fps = 50.0;

static int64_t emulated_usec(void)
{
	return (int64_t)((double)g_frameCount * 1000000.0 / g_fps);
}
time_t time(time_t *t)
{
	time_t v = (time_t)(g_startTime + emulated_usec() / 1000000);
	if (t) *t = v;
	return v;
}
int gettimeofday(struct timeval *restrict tv, void *restrict tz)
{
	(void)tz;
	int64_t us = emulated_usec();
	if (tv)
	{
		tv->tv_sec = (time_t)(g_startTime + us / 1000000);
		tv->tv_usec = (suseconds_t)(us % 1000000);
	}
	return 0;
}
int clock_gettime(clockid_t id, struct timespec *ts)
{
	(void)id;
	int64_t us = emulated_usec();
	ts->tv_sec = (time_t)(g_startTime + us / 1000000);
	ts->tv_nsec = (long)(us % 1000000) * 1000;
	return 0;
}
struct tm *localtime(const time_t *t) { return gmtime(t); }
struct tm *localtime_r(const time_t *restrict t, struct tm *restrict out) { return gmtime_r(t, out); }

/* ---- the callbacks PUAE calls ------------------------------------------- */
static struct retro_keyboard_callback g_keyboard;

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
	static int all = -1;
	if (all < 0) all = getenv("CHIMERA_PUAE_LOG") != NULL;
	if (level < RETRO_LOG_WARN && !all) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool environment(unsigned cmd, void *data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = log_printf;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 2;
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
	{
		const struct retro_core_options_v2 *o = data;
		for (const struct retro_core_option_v2_definition *d = o ? o->definitions : NULL; d && d->key; d++)
			take_option(d->key, d->default_value ? d->default_value : d->values[0].value);
		return true;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
	{
		const struct retro_core_options_v2_intl *o = data;
		for (const struct retro_core_option_v2_definition *d = o && o->us ? o->us->definitions : NULL; d && d->key; d++)
			take_option(d->key, d->default_value ? d->default_value : d->values[0].value);
		return true;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		struct retro_variable *v = data;
		v->value = option_value(v->key);
		return v->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	/* The sandbox's files have no folders: a file is found by its bare name.
	 * So the folder the Kickstarts are looked for in is "", which PUAE joins
	 * to a name as the name alone. (The save folder is a name of its own,
	 * at least two characters long, because PUAE looks at the last two
	 * characters of it; nothing is saved there.) */
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
		*(const char **)data = "";
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = "saves";
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		g_keyboard = *(const struct retro_keyboard_callback *)data;
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		g_fps = ((const struct retro_system_av_info *)data)->timing.fps;
		return true;
	default:
		return false;
	}
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
	if (!data || !g_render) return;
	if (width > MAX_W) width = MAX_W;
	if (height > MAX_H) height = MAX_H;
	g_width = (int)width;
	g_height = (int)height;
	for (unsigned y = 0; y < height; y++)
	{
		const uint32_t *src = (const uint32_t *)((const uint8_t *)data + y * pitch);
		uint32_t *dst = g_picture + (size_t)y * width;
		for (unsigned x = 0; x < width; x++)
			dst[x] = src[x] | 0xFF000000u;
	}
}

static void audio_sample(int16_t left, int16_t right)
{
	if (g_audioFrames >= MAX_SAMPLES) return;
	g_audio[g_audioFrames * 2] = left;
	g_audio[g_audioFrames * 2 + 1] = right;
	g_audioFrames++;
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
	size_t n = frames;
	if (n > (size_t)(MAX_SAMPLES - g_audioFrames)) n = (size_t)(MAX_SAMPLES - g_audioFrames);
	memcpy(g_audio + g_audioFrames * 2, data, n * 4);
	g_audioFrames += (int)n;
	return frames;
}

static void input_poll(void) {}

/* The libretro layer's own gamepad mapping is given nothing: the controls
 * reach UAE directly (give_controls). */
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)port; (void)device; (void)index; (void)id;
	return 0;
}

/* ---- the memory --------------------------------------------------------- */
struct memory_area { const char *name; uint8_t *bytes; int64_t size; };
static struct memory_area g_memory[3];
static int g_memoryCount;

static void add_memory(const char *name, addrbank *bank)
{
	if (bank->baseaddr && bank->allocated_size > 0)
		g_memory[g_memoryCount++] = (struct memory_area){ name, bank->baseaddr, (int64_t)bank->allocated_size };
}

/* ---- Chimera's calls ---------------------------------------------------- */
ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }
ECL_EXPORT const char *SuggestSettings(void) { return "{\"values\":{},\"note\":\"\",\"firmware\":[]}"; }
ECL_EXPORT const char *DescribeDips(void) { return ""; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
	char model[32] = "A500", kick[32] = "auto";
	wbx_setting_str("model", model, sizeof model);
	wbx_setting_str("kickstart", kick, sizeof kick);
	g_isCdtv = !strcmp(model, "CDTV");
	g_isCd = !strcmp(model, "CD32") || !strcmp(model, "CD32FR") || g_isCdtv;
	const char *slot = g_isCd ? "disc" : "disk";
	g_diskCount = wbx_slot_count(slot);
	if (g_diskCount > MAX_DISKS) g_diskCount = MAX_DISKS;
	if (g_isCd && g_diskCount > 1) g_diskCount = 1;
	for (int i = 0; i < g_diskCount; i++)
		wbx_slot_name(slot, i, g_disks[i], sizeof g_disks[i]);
	g_nvram[0] = '\0';
	if (wbx_slot_count("savedata") > 0)
	{
		/* the save memory, as Export Save Data writes it: the CD32's 1 KB,
		 * the CDTV's 32 KB (as upstream keeps it). Any other file would not
		 * be read, so it is refused */
		wbx_slot_name("savedata", 0, g_nvram, sizeof g_nvram);
		FILE *f = fopen(g_nvram, "rb");
		long size = -1;
		if (f && fseek(f, 0, SEEK_END) == 0) size = ftell(f);
		if (f) fclose(f);
		const int cdtv = !strcmp(model, "CDTV");
		const long want = cdtv ? 32768 : 1024;
		if (!g_isCd)
		{
			snprintf(g_loadError, sizeof g_loadError, "error.save_not_this_model" TEXT_ARG "%s" TEXT_ARG "%s", g_nvram, model);
			return 0;
		}
		if (size != want)
		{
			snprintf(g_loadError, sizeof g_loadError, "error.save_size" TEXT_ARG "%s" TEXT_ARG "%s" TEXT_ARG "%ld" TEXT_ARG "%ld", g_nvram, cdtv ? "CDTV" : "CD32", want, size);
			return 0;
		}
	}
	if (g_diskCount == 0)
	{
		/* a disk opened with no project is mounted under its own name */
		FILE *f = fopen("rom.name", "rb");
		if (f)
		{
			size_t got = fread(g_disks[0], 1, sizeof g_disks[0] - 1, f);
			g_disks[0][got] = '\0';
			fclose(f);
			g_diskCount = g_disks[0][0] != '\0';
		}
	}
	if (g_diskCount == 0)
	{
		snprintf(g_loadError, sizeof g_loadError, g_isCd ? "error.no_disc" : "error.no_disk");
		return 0;
	}
	{
		/* a disc on a floppy Amiga, or a floppy on a CD32, is a wrong Model */
		const char *dot = strrchr(g_disks[0], '.');
		static const char *const kDisc[] = { ".cue", ".ccd", ".iso", ".chd", ".nrg", ".mds" };
		int disc = 0;
		for (size_t i = 0; dot && i < sizeof kDisc / sizeof kDisc[0]; i++)
			disc |= !strcasecmp(dot, kDisc[i]);
		if (disc != g_isCd)
		{
			snprintf(g_loadError, sizeof g_loadError, disc ? "error.is_cd" TEXT_ARG "%s" : "error.not_cd" TEXT_ARG "%s" TEXT_ARG "%s", g_disks[0], model);
			return 0;
		}
	}
	g_selected = 0;

	{
		char v[32];
		if (wbx_setting_str("initialTime", v, sizeof v) > 0) g_startTime = strtoll(v, NULL, 10);
	}
	g_frameCount = 0;
	g_port1 = port_setting("port1", kMouse);
	/* the CD32's own pad on the CD32, a joystick elsewhere (the CDTV's
	 * controllers were a joystick, a mouse and its remote): the package says
	 * the same */
	g_port2 = port_setting("port2", g_isCd && strcmp(model, "CDTV") ? kPad : kJoystick);
	if (g_port1 == kPad) g_port1 = kJoystick;   /* the CD32's pad goes in port 2 only */
	for (int k = 0; k < KEY_COUNT; k++)
		snprintf(g_keyNames[k], sizeof g_keyNames[k], "Key %s", kKeys[k].name);

	/* the Model's Kickstart, unless it is AROS's: refused by its name when
	 * it is not there, which PUAE would otherwise start without */
	{
		static const struct { const char *model, *rom; } kRoms[] = {
			{ "A500", "amiga-os-130.rom" }, { "A500OG", "amiga-os-130.rom" }, { "A2000OG", "amiga-os-130.rom" },
			{ "A500PLUS", "amiga-os-204.rom" }, { "A600", "amiga-os-310-a600.rom" }, { "A2000", "amiga-os-310-a600.rom" },
			{ "A1200OG", "amiga-os-310-a1200.rom" }, { "A1200", "amiga-os-310-a1200.rom" },
			{ "A4030", "amiga-os-310-a4000.rom" }, { "A4040", "amiga-os-310-a4000.rom" },
			{ "CD32", "amiga-os-310-cd32.rom" }, { "CD32", "amiga-os-310-cd32-ext.rom" },
			{ "CD32FR", "amiga-os-310-cd32.rom" }, { "CD32FR", "amiga-os-310-cd32-ext.rom" },
			{ "CDTV", "amiga-os-130.rom" }, { "CDTV", "amiga-os-130-cdtv-ext.rom" },
		};
		for (size_t i = 0; i < sizeof kRoms / sizeof kRoms[0]; i++)
		{
			if (strcmp(kRoms[i].model, model) || strcmp(kick, "auto")) continue;
			FILE *f = fopen(kRoms[i].rom, "rb");
			if (!f)
			{
				snprintf(g_loadError, sizeof g_loadError, "error.no_kickstart" TEXT_ARG "%s" TEXT_ARG "%s", model, kRoms[i].rom);
				return 0;
			}
			fclose(f);
		}
	}

	retro_set_environment(environment);
	retro_set_video_refresh(video_refresh);
	retro_set_audio_sample(audio_sample);
	retro_set_audio_sample_batch(audio_batch);
	retro_set_input_poll(input_poll);
	retro_set_input_state(input_state);
	retro_init();
	/* the CD32's pad: upstream's input maps the device's seven buttons to the
	 * pad's only for a port the frontend says holds one (libretro-glue.c,
	 * input_get_default_joystick); otherwise only red and blue arrive.
	 * libretro's port 0 is the Amiga's port 2. */
	if (g_port2 == kPad)
		retro_set_controller_port_device(0, RETRO_DEVICE_PUAE_CD32PAD);

	struct retro_game_info info = { g_disks[0], NULL, 0, NULL };
	if (!retro_load_game(&info))
	{
		snprintf(g_loadError, sizeof g_loadError, "error.not_started" TEXT_ARG "%s", g_disks[0]);
		return 0;
	}
	struct retro_system_av_info av;
	retro_get_system_av_info(&av);
	g_fps = av.timing.fps;

	g_memoryCount = 0;
	add_memory("Chip RAM", &chipmem_bank);
	add_memory("Slow RAM", &bogomem_bank);
	add_memory("Fast RAM", &fastmem_bank[0]);
	return 1;
}

ECL_EXPORT int GetButtonCount(void) { return BUTTON_COUNT; }
ECL_EXPORT const char *GetButtonName(int32_t i)
{
	if (i < 0 || i >= BUTTON_COUNT) return "";
	return i < kFirstKey ? kPortButtonNames[i] : g_keyNames[i - kFirstKey];
}
ECL_EXPORT int IsButtonActive(int32_t i) { return i >= 0 && i < BUTTON_COUNT && button_active(i); }
ECL_EXPORT void SetButton(int32_t i, int32_t state)
{
	if (i >= 0 && i < BUTTON_COUNT) g_held[i] = state != 0;
}

ECL_EXPORT int GetAxisCount(void) { return kAxes; }
ECL_EXPORT const char *GetAxisName(int32_t i) { return i >= 0 && i < kAxes ? kAxisNames[i] : ""; }
ECL_EXPORT int IsAxisActive(int32_t i) { return i >= 0 && i < kAxes && g_port1 == kMouse; }
/* how far the mouse moves in this frame, in the mouse's own counts */
ECL_EXPORT void SetAxis(int32_t i, int32_t value)
{
	if (i >= 0 && i < kAxes) g_axis[i] = value < -128 ? -128 : value > 127 ? 127 : value;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;
	g_audioFrames = 0;
	give_controls();
	chimera_input_reads = 0;
	retro_run();
	g_frameCount++;
	g_inputRead = chimera_input_reads > 0;
}

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_picture; }
ECL_EXPORT int GetVideoWidth(void) { return g_width; }
ECL_EXPORT int GetVideoHeight(void) { return g_height; }
ECL_EXPORT int GetDisplayAspectX(void) { return 4; }
ECL_EXPORT int GetDisplayAspectY(void) { return 3; }
ECL_EXPORT int GetVsyncNumerator(void) { return (int)(g_fps * 1000.0 + 0.5); }
ECL_EXPORT int GetVsyncDenominator(void) { return 1000; }

ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_audioFrames; }

ECL_EXPORT int GetMemoryDomainCount(void) { return g_memoryCount; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return i >= 0 && i < g_memoryCount ? g_memory[i].name : ""; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return i >= 0 && i < g_memoryCount ? g_memory[i].bytes : NULL; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return i >= 0 && i < g_memoryCount ? g_memory[i].size : 0; }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i >= 0 && i < g_memoryCount; }

ECL_EXPORT const int64_t *GetBusRanges(int32_t bus) { (void)bus; return NULL; }
ECL_EXPORT const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len) { (void)bus; (void)addr; (void)len; return NULL; }
ECL_EXPORT void PokeBus(int32_t bus, int32_t addr, int32_t value) { (void)bus; (void)addr; (void)value; }

/* Export Save Data: each disk the Amiga has written to, as it is now (the
 * table patch 0003 keeps in disk.c), as an .adf under the disk's own name */
struct chimera_disk { char name[MAX_DPATH]; uae_u8 *data; size_t size; uae_u32 crc; };
extern struct chimera_disk chimera_disks[];
extern int chimera_disk_count;
extern void chimera_disks_sync(void);
extern int chimera_disk_changed(int i);
static int g_saveIndex[32], g_saveCount;
static char g_saveNames[32][MAX_DPATH + 8];

/* On the CD32 and the CDTV it is the save memory, as cd32.nvr or cdtv.nvr,
 * once anything is in it */
extern uae_u8 *chimera_cd32_nvram(int *size);
extern uae_u8 *chimera_cdtv_nvram(int *size);
static const uint8_t *g_nvramBytes;
static int g_nvramSize;

ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	g_saveCount = 0;
	if (g_isCd)
	{
		g_nvramBytes = g_isCdtv ? chimera_cdtv_nvram(&g_nvramSize) : chimera_cd32_nvram(&g_nvramSize);
		for (int i = 0; g_nvramBytes && i < g_nvramSize; i++)
			if (g_nvramBytes[i]) return 1;
		return 0;
	}
	chimera_disks_sync();
	for (int i = 0; i < chimera_disk_count && g_saveCount < 32; i++)
	{
		if (!chimera_disk_changed(i)) continue;
		/* a packed disk (.adz, .dms) is held unpacked: it goes out as an .adf */
		char *name = g_saveNames[g_saveCount];
		snprintf(name, MAX_DPATH + 8, "%s", chimera_disks[i].name);
		char *dot = strrchr(name, '.');
		if (!dot || strcasecmp(dot, ".adf"))
		{
			if (dot) *dot = '\0';
			strcat(name, ".adf");
		}
		g_saveIndex[g_saveCount++] = i;
	}
	return g_saveCount;
}
ECL_EXPORT const char *GetSaveDataFileName(int32_t i)
{
	if (g_isCd) return i == 0 ? (g_isCdtv ? "cdtv.nvr" : "cd32.nvr") : "";
	return i >= 0 && i < g_saveCount ? g_saveNames[i] : "";
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
	if (g_isCd) return i == 0 ? g_nvramSize : 0;
	return i >= 0 && i < g_saveCount ? (int64_t)chimera_disks[g_saveIndex[i]].size : 0;
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	if (g_isCd) return i == 0 ? g_nvramBytes : NULL;
	return i >= 0 && i < g_saveCount ? chimera_disks[g_saveIndex[i]].data : NULL;
}
