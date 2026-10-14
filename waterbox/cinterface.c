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

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "libretro.h"

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"

/* ---- what went wrong, for Chimera to show -------------------------------- */
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

/* ---- the controls ------------------------------------------------------- */
enum { kUp, kDown, kLeft, kRight, kFire1, kFire2, kButtons };
static const char *const kButtonNames[kButtons] = { "P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 Fire", "P1 Fire 2" };
static int g_held[kButtons];
static int g_inputRead;

/* ---- the callbacks PUAE calls ------------------------------------------- */
static struct retro_keyboard_callback g_keyboard;
static double g_fps = 50.0;

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
	 * to a name as the name alone. (The save folder is "." because PUAE
	 * looks at the last character of it; nothing is saved there.) */
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
		*(const char **)data = "";
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = ".";
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

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)index;
	g_inputRead = 1;
	if (port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
	switch (id)
	{
	case RETRO_DEVICE_ID_JOYPAD_UP: return g_held[kUp];
	case RETRO_DEVICE_ID_JOYPAD_DOWN: return g_held[kDown];
	case RETRO_DEVICE_ID_JOYPAD_LEFT: return g_held[kLeft];
	case RETRO_DEVICE_ID_JOYPAD_RIGHT: return g_held[kRight];
	case RETRO_DEVICE_ID_JOYPAD_B: return g_held[kFire1];
	case RETRO_DEVICE_ID_JOYPAD_A: return g_held[kFire2];
	default: return 0;
	}
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
	static char disk[512];
	g_loadError[0] = '\0';
	if (wbx_slot_count("disk") > 0)
		wbx_slot_name("disk", 0, disk, sizeof disk);
	if (!disk[0])
	{
		FILE *f = fopen("rom.name", "rb");
		if (f)
		{
			size_t got = fread(disk, 1, sizeof disk - 1, f);
			disk[got] = '\0';
			fclose(f);
		}
	}
	if (!disk[0])
	{
		snprintf(g_loadError, sizeof g_loadError, "no disk was given");
		return 0;
	}

	retro_set_environment(environment);
	retro_set_video_refresh(video_refresh);
	retro_set_audio_sample(audio_sample);
	retro_set_audio_sample_batch(audio_batch);
	retro_set_input_poll(input_poll);
	retro_set_input_state(input_state);
	retro_init();

	struct retro_game_info info = { disk, NULL, 0, NULL };
	if (!retro_load_game(&info))
	{
		snprintf(g_loadError, sizeof g_loadError, "the emulator did not start with %s", disk);
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

ECL_EXPORT int GetButtonCount(void) { return kButtons; }
ECL_EXPORT const char *GetButtonName(int32_t i) { return i >= 0 && i < kButtons ? kButtonNames[i] : ""; }
ECL_EXPORT int IsButtonActive(int32_t i) { return i >= 0 && i < kButtons; }
ECL_EXPORT void SetButton(int32_t i, int32_t state)
{
	if (i >= 0 && i < kButtons) g_held[i] = state != 0;
}

ECL_EXPORT int GetAxisCount(void) { return 0; }
ECL_EXPORT const char *GetAxisName(int32_t i) { (void)i; return ""; }
ECL_EXPORT int IsAxisActive(int32_t i) { (void)i; return 0; }
ECL_EXPORT void SetAxis(int32_t i, int32_t value) { (void)i; (void)value; }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;
	g_inputRead = 0;
	g_audioFrames = 0;
	retro_run();
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

ECL_EXPORT int32_t GetSaveDataFileCount(void) { return 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { (void)i; return ""; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { (void)i; return 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { (void)i; return NULL; }
