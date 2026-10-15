/*
 * ipf-reader.c - IPF floppy images for PUAE, read without the SPS library.
 *
 * IPF ("Interchangeable Preservation Format") is the format the Software
 * Preservation Society keeps Amiga disks in, copy protection included. UAE
 * reads it through the SPS Decoder Library (capsimg), whose licence forbids
 * commercial use; the GPL, under which this core is distributed, does not
 * allow that restriction to be added, so the library cannot be built in. The
 * sandbox cannot load a library the user brings either. This file stands in
 * for PUAE's caps/caps.c (which only loads that library) and gives disk.c the
 * same five functions (sources/src/include/caps.h), on a reader of its own.
 *
 * The reader is MAME's: src/lib/formats/ipf_dsk.cpp, copyright Olivier
 * Galibert, under the BSD-3-Clause licence (its text is in
 * waterbox/licenses/MAME-ipf_dsk-BSD-3-Clause.txt), as it is at MAME commit
 * 0cacb1a7, ported from C++ to C. The reading of the file and the building
 * of a track's cells from it are MAME's, step for step, with MAME's names.
 * What is this file's own is the end of it: MAME hands a track to its floppy
 * drive as flux transitions over one revolution, while UAE wants the cells
 * as bits, and, on a track whose cells are not all the same length (the
 * copy protections that time a track), the length of each byte's cells, in
 * thousandths of the normal 2 microseconds, as the SPS library gives it.
 *
 * Weak bits (cells a copy protection reads differently on every revolution)
 * and unformatted tracks become random bits, drawn again on every revolution
 * from a generator whose state is in the core's memory: a savestate carries
 * it, and a movie replays the same.
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "zfile.h"
#include "caps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A cell is one 32-bit value, as in MAME: what it holds in the top bits, its
 * length in nanoseconds in the others. */
#define TIME_MASK 0x0fffffffu
#define MG_MASK   0xf0000000u
#define MG_0      0x10000000u /* no flux transition: a 0 bit */
#define MG_1      0x20000000u /* a flux transition: a 1 bit */
#define MG_N      0x30000000u /* no magnetisation: reads as random bits (MAME's MG_N) */

/* the most cells a track may have: what disk.c's track buffer holds at
 * double density, with room to spare (a normal track has about 100,000) */
#define MAX_CELLS (0x4000 * 16)
/* the cells of a track the image does not have: an unformatted track of the
 * normal length (12,668 bytes, as UAE writes one) */
#define NOISE_CELLS (12668 * 8)
/* MAME's limit on a track's index in the file */
#define MAX_TRACK_INDEX 1000
/* UAE's track numbers are cylinder * 2 + head */
#define MAX_UAE_TRACKS (168 * 2)

struct track_info {
	uint32_t cylinder, head, type;
	uint32_t sigtype, process, reserved[3];
	uint32_t size_bytes, size_cells;
	uint32_t index_bytes, index_cells;
	uint32_t datasize_cells, gapsize_cells;
	uint32_t block_count, weak_bits;

	uint32_t data_size_bits;

	bool info_set;

	const uint8_t *data;
	uint32_t data_size;
};

struct ipf_decode {
	uint8_t *file;   /* the core's own copy of the image: reading it changes it (scan_one_tag) */
	size_t size;

	struct track_info *tinfos;
	uint32_t tcount;

	uint32_t type, release, revision;
	uint32_t encoder_type, encoder_revision, origin;
	uint32_t min_cylinder, max_cylinder, min_head, max_head;
	uint32_t credit_day, credit_time;
	uint32_t platform[4], extra[5];
};

/* What a drive has: the image, and the cells of the last track built. */
struct ipf_drive {
	bool loaded;
	struct ipf_decode dec;
	int16_t track_of[MAX_UAE_TRACKS]; /* the tinfos index of each UAE track, -1 for none */

	int cached;          /* the UAE track whose cells are in cells[], -1 for none */
	uint32_t *cells;
	uint32_t ncells;
	uint32_t splice;     /* the write splice, in cells from the index */
	bool flakey;         /* it has cells that read differently each revolution */
	bool timed;          /* its cells are not all 2 microseconds long */
};

static struct ipf_drive drives[4];

/* The generator of weak bits (xorshift32). It is in the core's memory, so
 * the sandbox's savestate has it. */
static uint32_t weak_state = 0x2545f491u;

static uint32_t weak_bit(void)
{
	uint32_t x = weak_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	weak_state = x;
	return x >> 31;
}

static uint32_t get_u32be(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rb(const uint8_t **p, int count)
{
	uint32_t v = 0;
	for (int i = 0; i < count; i++)
		v = (v << 8) | *(*p)++;
	return v;
}

static uint32_t crc32r(const uint8_t *data, uint32_t size)
{
	/* Reversed crc32 */
	uint32_t crc = 0xffffffff;
	for (uint32_t i = 0; i != size; i++) {
		crc = crc ^ data[i];
		for (int j = 0; j < 8; j++)
			if (crc & 1)
				crc = (crc >> 1) ^ 0xedb88320;
			else
				crc = crc >> 1;
	}
	return ~crc;
}

/* ---- reading the file (MAME's parse_info .. scan_all_tags) ---------------- */

static bool parse_info(struct ipf_decode *d, const uint8_t *info)
{
	d->type = get_u32be(info + 12);
	if (d->type != 1)
		return false;
	d->encoder_type = get_u32be(info + 16); /* 1 for CAPS, 2 for SPS */
	if (d->encoder_type != 1 && d->encoder_type != 2)
		return false;
	d->encoder_revision = get_u32be(info + 20);
	if (d->encoder_revision != 1)
		return false; /* Only SPS_ENCODER revision 1 is defined/supported */
	d->release = get_u32be(info + 24);
	d->revision = get_u32be(info + 28);
	d->origin = get_u32be(info + 32); /* Original source reference */
	d->min_cylinder = get_u32be(info + 36);
	d->max_cylinder = get_u32be(info + 40);
	d->min_head = get_u32be(info + 44);
	d->max_head = get_u32be(info + 48);
	d->credit_day = get_u32be(info + 52);  /* year*1e4 + month*1e2 + day */
	d->credit_time = get_u32be(info + 56); /* hour*1e7 + min*1e5 + sec*1e3 + msec */
	for (int i = 0; i < 4; i++)
		d->platform[i] = get_u32be(info + 60 + 4 * i);
	for (int i = 0; i < 5; i++)
		d->extra[i] = get_u32be(info + 76 + 4 * i);
	return true;
}

static struct track_info *get_index(struct ipf_decode *d, uint32_t idx)
{
	/* MAME grows its table as indexes come; here it is made whole at once */
	if (idx > MAX_TRACK_INDEX)
		return NULL;
	if (idx >= d->tcount)
		d->tcount = idx + 1;
	return &d->tinfos[idx];
}

static bool parse_imge(struct ipf_decode *d, const uint8_t *imge)
{
	struct track_info *t = get_index(d, get_u32be(imge + 64));
	if (!t)
		return false;

	t->info_set = true;

	t->cylinder = get_u32be(imge + 12);
	if (t->cylinder < d->min_cylinder || t->cylinder > d->max_cylinder)
		return false;

	t->head = get_u32be(imge + 16);
	if (t->head < d->min_head || t->head > d->max_head)
		return false;

	t->type = get_u32be(imge + 20);
	t->sigtype = get_u32be(imge + 24); /* 1 for 2us cells, no other value valid */
	if (t->sigtype != 1)
		return false;
	t->size_bytes = get_u32be(imge + 28);
	t->index_bytes = get_u32be(imge + 32);
	t->index_cells = get_u32be(imge + 36);
	t->datasize_cells = get_u32be(imge + 40);
	t->gapsize_cells = get_u32be(imge + 44);
	t->size_cells = get_u32be(imge + 48);
	t->block_count = get_u32be(imge + 52);
	t->process = get_u32be(imge + 56); /* encoder process, always 0 */
	if (t->process != 0)
		return false;
	t->weak_bits = get_u32be(imge + 60);
	t->reserved[0] = get_u32be(imge + 68);
	t->reserved[1] = get_u32be(imge + 72);
	t->reserved[2] = get_u32be(imge + 76);

	return true;
}

static bool parse_data(struct ipf_decode *d, const uint8_t *data, uint32_t *pos, uint32_t max_extra_size)
{
	struct track_info *t = get_index(d, get_u32be(data + 24));
	if (!t)
		return false;

	t->data_size_bits = get_u32be(data + 16);
	t->data = data + 28;
	t->data_size = get_u32be(data + 12);
	if (t->data_size > max_extra_size)
		return false;
	if (crc32r(t->data, t->data_size) != get_u32be(data + 20))
		return false;
	*pos += t->data_size;
	return true;
}

static bool scan_one_tag(uint8_t *data, size_t size, uint32_t *pos, uint8_t **tag, uint32_t *tsize)
{
	if (size - *pos < 12)
		return false;
	*tag = &data[*pos];
	*tsize = get_u32be(*tag + 4);
	if (*tsize < 12) /* Every tag has at least a 12-byte header; a smaller */
		return false; /* size would stall scan_all_tags' loop forever */
	if (size - *pos < *tsize)
		return false;
	uint32_t crc = get_u32be(*tag + 8);
	(*tag)[8] = (*tag)[9] = (*tag)[10] = (*tag)[11] = 0;
	if (crc32r(*tag, *tsize) != crc)
		return false;
	*pos += *tsize;
	return true;
}

static bool scan_all_tags(struct ipf_decode *d, uint8_t *data, size_t size)
{
	uint32_t pos = 0;
	while (pos != size) {
		uint8_t *tag;
		uint32_t tsize;

		if (!scan_one_tag(data, size, &pos, &tag, &tsize))
			return false;

		switch (get_u32be(tag)) {
		case 0x43415053: /* CAPS */
			if (tsize != 12)
				return false;
			break;

		case 0x494e464f: /* INFO */
			if (tsize != 96)
				return false;
			if (!parse_info(d, tag))
				return false;
			break;

		case 0x494d4745: /* IMGE */
			if (tsize != 80)
				return false;
			if (!parse_imge(d, tag))
				return false;
			break;

		case 0x44415441: /* DATA */
			if (tsize != 28)
				return false;
			if (!parse_data(d, tag, &pos, size - pos))
				return false;
			break;

		default:
			/* Unknown tags (e.g. TRCK, CTEX, CTEI) carry no data needed
			   for decoding and are safe to skip. */
			break;
		}
	}
	return true;
}

/* ---- building a track's cells (MAME's track_write_raw .. generate_track) -- */

static void track_write_raw(uint32_t **tpos, const uint8_t *data, uint32_t cells, bool *context)
{
	for (uint32_t i = 0; i != cells; i++)
		*(*tpos)++ = data[i >> 3] & (0x80 >> (i & 7)) ? MG_1 : MG_0;
	if (cells)
		*context = (*tpos)[-1] == MG_1;
}

static void track_write_mfm(uint32_t **tpos, const uint8_t *data, uint32_t start_offset, uint32_t patlen, uint32_t cells, bool *context)
{
	patlen *= 2;
	for (uint32_t i = 0; i != cells; i++) {
		uint32_t pos = (i + start_offset) % patlen;
		bool bit = data[pos >> 4] & (0x80 >> ((pos >> 1) & 7));
		if (pos & 1) {
			*(*tpos)++ = bit ? MG_1 : MG_0;
			*context = bit;
		} else
			*(*tpos)++ = *context || bit ? MG_0 : MG_1;
	}
}

static void track_write_weak(uint32_t **tpos, uint32_t cells)
{
	for (uint32_t i = 0; i != cells; i++)
		*(*tpos)++ = MG_N;
}

static bool generate_block_data(const uint8_t *data, const uint8_t *dlimit, uint32_t *tpos, uint32_t *tlimit, bool dmb, bool raw, bool *context)
{
	for (;;) {
		if (data >= dlimit)
			return false;
		uint8_t val = *data++;
		if ((val >> 5) > dlimit - data)
			return false;
		uint32_t param = rb(&data, val >> 5);
		uint64_t tleft = tlimit - tpos;
		uint64_t bitcount = dmb ? (uint64_t)param : (uint64_t)param * 8;
		uint64_t bytecount = (bitcount + 7) / 8;
		switch (val & 0x1f) {
		case 0: /* End of description */
			return !tleft;

		case 1: /* Sync mark, unencoded cells */
			if (raw)
				return false;
			if (bitcount > tleft || bytecount > (uint64_t)(dlimit - data))
				return false;
			track_write_raw(&tpos, data, (uint32_t)bitcount, context);
			data += bytecount;
			break;

		case 2: /* MFM-decoded data bytes */
		case 3: /* MFM-decoded gap bytes */
			if (raw)
				return false;
			if (2 * bitcount > tleft || bytecount > (uint64_t)(dlimit - data))
				return false;
			track_write_mfm(&tpos, data, 0, (uint32_t)bitcount, (uint32_t)(2 * bitcount), context);
			data += bytecount;
			break;

		case 4: /* Raw cell bits, size in bytes regardless of the data size mode */
			if (!raw)
				return false;
			bitcount = (uint64_t)param * 8;
			bytecount = param;
			if (bitcount > tleft || bytecount > (uint64_t)(dlimit - data))
				return false;
			track_write_raw(&tpos, data, (uint32_t)bitcount, context);
			data += bytecount;
			break;

		case 5: /* Weak bytes */
			if (raw)
				return false;
			if (2 * bitcount > tleft)
				return false;
			track_write_weak(&tpos, (uint32_t)(2 * bitcount));
			*context = 0;
			break;

		default:
			return false;
		}
	}
}

static bool generate_block_gap_0(uint32_t gap_cells, uint8_t pattern, uint32_t *spos, uint32_t ipos, uint32_t **tpos, bool *context)
{
	*spos = ipos >= 16 && ipos + 16 <= gap_cells ? ipos : gap_cells >> 1;
	track_write_mfm(tpos, &pattern, 0, 8, *spos, context);
	uint32_t delta = 0;
	if (gap_cells & 1) {
		*(*tpos)++ = MG_0;
		delta++;
	}
	track_write_mfm(tpos, &pattern, *spos + delta - gap_cells, 8, gap_cells - *spos - delta, context);
	return true;
}

static bool gap_description_to_reserved_size(const uint8_t **data, const uint8_t *dlimit, uint32_t *res_size)
{
	*res_size = 0;
	for (;;) {
		if (*data >= dlimit)
			return false;
		uint8_t val = *(*data)++;
		if ((val >> 5) > dlimit - *data)
			return false;
		uint32_t param = rb(data, val >> 5);
		switch (val & 0x1f) {
		case 0:
			return true;
		case 1:
			*res_size += param * 2;
			break;
		case 2:
			*data += (param + 7) / 8;
			break;
		default:
			return false;
		}
	}
}

static bool generate_gap_from_description(const uint8_t **data, const uint8_t *dlimit, uint32_t *tpos, uint32_t size, bool pre, bool *context)
{
	const uint8_t *data1 = *data;
	uint32_t res_size;
	if (!gap_description_to_reserved_size(&data1, dlimit, &res_size))
		return false;

	if (res_size > size)
		return false;
	uint8_t pattern[16];
	memset(pattern, 0, sizeof(pattern));
	uint32_t pattern_size = 0;

	uint32_t pos = 0, block_size = 0;
	for (;;) {
		uint8_t val = *(*data)++;
		uint32_t param = rb(data, val >> 5);
		switch (val & 0x1f) {
		case 0:
			return size == pos;

		case 1:
			if (block_size)
				return false;
			block_size = param * 2;
			pattern_size = 0;
			break;

		case 2:
			pattern_size = param;
			if (pattern_size > sizeof(pattern) * 8)
				return false;

			memcpy(pattern, *data, (pattern_size + 7) / 8);
			*data += (pattern_size + 7) / 8;
			if (pre) {
				/* The last data sample of a forward gap stream is the loop point */
				if (*data < dlimit && !(**data & 0x1f))
					block_size = size - pos;
				else if (!block_size)
					block_size = pattern_size;
				if (pos + block_size > size)
					return false;
				track_write_mfm(&tpos, pattern, 0, pattern_size, block_size, context);
				pos += block_size;
			} else {
				if (pos == 0 && block_size && res_size != size)
					block_size = size - (res_size - block_size);
				if (!block_size)
					block_size = size - res_size;
				if (pos + block_size > size)
					return false;
				track_write_mfm(&tpos, pattern, -block_size, pattern_size, block_size, context);
				pos += block_size;
			}
			block_size = 0;
			break;
		}
	}
}

static bool generate_block_gap_1(uint32_t gap_cells, uint32_t *spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, uint32_t *tpos, bool *context)
{
	if (ipos >= 16 && ipos < gap_cells - 16)
		*spos = ipos;
	else
		*spos = 0;
	return generate_gap_from_description(&data, dlimit, tpos, gap_cells, true, context);
}

static bool generate_block_gap_2(uint32_t gap_cells, uint32_t *spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, uint32_t *tpos, bool *context)
{
	if (ipos >= 16 && ipos < gap_cells - 16)
		*spos = ipos;
	else
		*spos = gap_cells;
	return generate_gap_from_description(&data, dlimit, tpos, gap_cells, false, context);
}

static bool generate_block_gap_3(uint32_t gap_cells, uint32_t *spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, uint32_t *tpos, bool *context)
{
	uint32_t presize, postsize;
	const uint8_t *data1 = data;
	if (!gap_description_to_reserved_size(&data1, dlimit, &presize))
		return false;
	if (!gap_description_to_reserved_size(&data1, dlimit, &postsize))
		return false;

	uint32_t delta = gap_cells & 1;
	uint32_t usable = gap_cells - delta;

	if (presize + postsize > usable) {
		/* Trim both gap streams evenly like the reference decoder */
		uint32_t rem = presize + postsize - usable;
		uint32_t rem0 = rem >> 1, rem1 = rem - rem0;
		if (presize < rem0) {
			rem1 += rem0 - presize;
			rem0 = presize;
		}
		if (postsize < rem1) {
			rem0 += rem1 - postsize;
			rem1 = postsize;
		}
		presize -= rem0;
		postsize -= rem1;
	}

	if (ipos >= 16 && ipos < gap_cells - 16) {
		/* Keep the index splice within the region the descriptions can fill */
		*spos = ipos;
		if (*spos < presize)
			*spos = presize;
		if (*spos > usable - postsize)
			*spos = usable - postsize;
	} else
		*spos = presize + (usable - presize - postsize) / 2;

	if (!generate_gap_from_description(&data, dlimit, tpos, *spos, true, context))
		return false;
	if (delta)
		tpos[*spos] = MG_0;

	return generate_gap_from_description(&data, dlimit, tpos + *spos + delta, gap_cells - *spos - delta, false, context);
}

static bool generate_block_gap(uint32_t gap_type, uint32_t gap_cells, uint8_t pattern, uint32_t *spos, uint32_t ipos, const uint8_t *data, const uint8_t *dlimit, uint32_t *tpos, bool *context)
{
	switch (gap_type) {
	case 0:
		return generate_block_gap_0(gap_cells, pattern, spos, ipos, &tpos, context);
	case 1:
		return generate_block_gap_1(gap_cells, spos, ipos, data, dlimit, tpos, context);
	case 2:
		return generate_block_gap_2(gap_cells, spos, ipos, data, dlimit, tpos, context);
	case 3:
		return generate_block_gap_3(gap_cells, spos, ipos, data, dlimit, tpos, context);
	default:
		return false;
	}
}

static bool generate_block(const struct ipf_decode *d, const struct track_info *t, uint32_t idx, uint32_t ipos, uint32_t *track, uint32_t *pos, uint32_t *dpos, uint32_t *gpos, uint32_t *spos, bool *context)
{
	const uint8_t *data = t->data;
	const uint8_t *data_end = t->data + t->data_size;
	const uint8_t *thead = data + 32 * idx;
	uint32_t data_cells = get_u32be(thead);
	uint32_t gap_cells = get_u32be(thead + 4);

	if (gap_cells < 8)
		gap_cells = 0;
	/* +8  = gap description offset / datasize in bytes (when gap type = 0)
	 *       -- old CAPS_ENCODER (v1): "blocksize", unused, rounded duplicate of data_cells
	 * +12 =                      1 / gap size in bytes (when gap type = 0)
	 *       -- old CAPS_ENCODER (v1): "gapsize", unused, rounded duplicate of gap_cells
	 * +16 = block encoder type: 1 = MFM, 2 = raw cells (no encoding)
	 * +20 = flags: bits 0-1 = gap type, bit 2 = data size mode (DMB, bits vs bytes)
	 * +24 = type 0 gap pattern (8 bits) / speed mask for sector 0 track type 9
	 * +28 = data description offset */

	uint32_t encoder = get_u32be(thead + 16);
	if (encoder != 1 && encoder != 2)
		return false;
	bool raw = encoder == 2;

	uint32_t flags = d->encoder_type == 1 ? 0 : get_u32be(thead + 20);
	uint32_t gap_type = flags & 3;
	bool dmb = (flags & 4) != 0;

	*dpos = *pos;
	*gpos = *dpos + data_cells;
	*pos = *gpos + gap_cells;
	if (*pos > t->size_cells)
		return false;
	/* the offsets are checked here, where MAME reads past the track's data
	 * on a damaged file */
	if (get_u32be(thead + 28) >= t->data_size || (gap_type != 0 && get_u32be(thead + 8) >= t->data_size))
		return false;
	if (!generate_block_data(data + get_u32be(thead + 28), data_end, track + *dpos, track + *gpos, dmb, raw, context))
		return false;
	if (!generate_block_gap(gap_type, gap_cells, (uint8_t)get_u32be(thead + 24), spos, ipos > *gpos ? ipos - *gpos : 0, data + get_u32be(thead + 8), data_end, track + *gpos, context))
		return false;
	*spos += *gpos;

	return true;
}

static uint32_t block_compute_real_size(const struct track_info *t)
{
	uint32_t size = 0;
	const uint8_t *thead = t->data;
	for (unsigned int i = 0; i != t->block_count; i++) {
		uint32_t data_cells = get_u32be(thead);
		uint32_t gap_cells = get_u32be(thead + 4);
		if (gap_cells < 8)
			gap_cells = 0;

		size += data_cells + gap_cells;
		thead += 32;
	}
	return size;
}

static void timing_set(uint32_t *track, uint32_t start, uint32_t end, uint32_t time)
{
	for (uint32_t i = start; i != end; i++)
		track[i] = (track[i] & MG_MASK) | time;
}

static bool generate_timings(const struct track_info *t, uint32_t *track, const uint32_t *data_pos, const uint32_t *gap_pos)
{
	timing_set(track, 0, t->size_cells, 2000);

	switch (t->type) {
	case 2: break; /* type 1 (noise) never reaches here, see generate_track */

	case 3:
		if (t->block_count >= 5) {
			timing_set(track, gap_pos[3], data_pos[4], 1890);
			timing_set(track, data_pos[4], gap_pos[4], 1890);
		}
		if (t->block_count >= 6) {
			timing_set(track, gap_pos[4], data_pos[5], 1990);
			timing_set(track, data_pos[5], gap_pos[5], 1990);
		}
		if (t->block_count >= 7) {
			timing_set(track, gap_pos[5], data_pos[6], 2090);
			timing_set(track, data_pos[6], gap_pos[6], 2090);
		}
		break;

	case 4:
		timing_set(track, gap_pos[t->block_count - 1], t->size_cells, 1890);
		timing_set(track, data_pos[0], gap_pos[0], 1890);
		if (t->block_count >= 2) {
			timing_set(track, gap_pos[0], data_pos[1], 1990);
			timing_set(track, data_pos[1], gap_pos[1], 1990);
		}
		if (t->block_count >= 3) {
			timing_set(track, gap_pos[1], data_pos[2], 2090);
			timing_set(track, data_pos[2], gap_pos[2], 2090);
		}
		break;

	case 5:
		if (t->block_count >= 6)
			timing_set(track, data_pos[5], gap_pos[5], 2100);
		break;

	case 6:
		if (t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2200);
		if (t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 1800);
		break;

	case 7:
		if (t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2100);
		break;

	case 8:
		if (t->block_count >= 2)
			timing_set(track, data_pos[1], gap_pos[1], 2200);
		if (t->block_count >= 3)
			timing_set(track, data_pos[2], gap_pos[2], 2100);
		if (t->block_count >= 5)
			timing_set(track, data_pos[4], gap_pos[4], 1900);
		if (t->block_count >= 6)
			timing_set(track, data_pos[5], gap_pos[5], 1800);
		if (t->block_count >= 7)
			timing_set(track, data_pos[6], gap_pos[6], 1700);
		break;

	case 9: {
		/* Speed key, reused from block 0's gap pattern/value field */
		uint32_t mask = get_u32be(t->data + 24);
		for (uint32_t i = 1; i < t->block_count; i++)
			timing_set(track, data_pos[i], gap_pos[i], mask & (1 << (i - 1)) ? 1900 : 2100);
		break;
	}

	default:
		return false;
	}

	return true;
}

static void rotate(uint32_t *track, uint32_t offset, uint32_t size)
{
	/* the cell at offset becomes the first (MAME moves them in place; the
	   result is the same) */
	uint32_t *copy = (uint32_t *)malloc(size * sizeof(uint32_t));
	if (!copy)
		return;
	memcpy(copy, track, size * sizeof(uint32_t));
	for (uint32_t i = 0; i != size; i++) {
		uint32_t from = i + offset;
		if (from >= size)
			from -= size;
		track[i] = copy[from];
	}
	free(copy);
}

static void mark_track_splice(uint32_t *track, uint32_t offset, uint32_t size)
{
	for (int i = 0; i < 3; i++) {
		uint32_t pos = (offset + i) % size;
		uint32_t v = track[pos];
		if ((v & MG_MASK) == MG_0)
			v = (v & TIME_MASK) | MG_1;
		else if ((v & MG_MASK) == MG_1)
			v = (v & TIME_MASK) | MG_0;
		track[pos] = v;
	}
}

/* MAME's generate_track, which ends by handing the cells to MAME's drive;
 * this one leaves them in `out` (malloc'd, `*ncells` of them, the first at
 * the index) with the write splice's position. A noise track, and a track
 * with no cells, is false with *noise set: the caller makes it. */
static bool generate_track(const struct ipf_decode *d, struct track_info *t, uint32_t **out, uint32_t *ncells, uint32_t *splice, bool *noise)
{
	*out = NULL;
	*noise = false;
	if (!t->size_cells || t->type == 1) {
		/* Noise/unformatted track: the surface has no cell structure */
		*noise = true;
		*ncells = t->size_cells;
		return false;
	}

	if (t->data_size < 32 * t->block_count)
		return false;

	/* Annoyingly enough, too small gaps are ignored, changing the
	   total track size.  Artifact stemming from the byte-only support
	   of old times? */
	t->size_cells = block_compute_real_size(t);

	if (t->index_cells >= t->size_cells || t->size_cells > MAX_CELLS || !t->block_count)
		return false;

	uint32_t *track = (uint32_t *)calloc(t->size_cells, sizeof(uint32_t));
	uint32_t *data_pos = (uint32_t *)calloc(t->block_count + 1, sizeof(uint32_t));
	uint32_t *gap_pos = (uint32_t *)calloc(t->block_count, sizeof(uint32_t));
	uint32_t *splice_pos = (uint32_t *)calloc(t->block_count, sizeof(uint32_t));
	bool ok = track && data_pos && gap_pos && splice_pos;

	bool context = false;
	uint32_t pos = 0;
	for (uint32_t i = 0; ok && i != t->block_count; i++) {
		if (!generate_block(d, t, i, i == t->block_count - 1 ? t->size_cells - t->index_cells : 0xffffffff, track, &pos, &data_pos[i], &gap_pos[i], &splice_pos[i], &context))
			ok = false;
	}
	if (ok && (pos != t->size_cells || t->index_cells >= t->size_cells))
		ok = false;

	if (ok) {
		data_pos[t->block_count] = pos;

		mark_track_splice(track, splice_pos[t->block_count - 1], t->size_cells);

		if (!generate_timings(t, track, data_pos, gap_pos))
			ok = false;
	}

	if (ok) {
		if (t->index_cells)
			rotate(track, t->size_cells - t->index_cells, t->size_cells);
		*splice = (splice_pos[t->block_count - 1] + t->index_cells) % t->size_cells;
		*ncells = t->size_cells;
		*out = track;
		track = NULL;
	}

	free(track);
	free(data_pos);
	free(gap_pos);
	free(splice_pos);
	return ok;
}

/* ---- the drive: what UAE asks for ----------------------------------------- */

static void drop_drive(struct ipf_drive *dr)
{
	free(dr->dec.file);
	free(dr->dec.tinfos);
	free(dr->cells);
	memset(dr, 0, sizeof(*dr));
	dr->cached = -1;
}

/* Makes the cells of UAE track `track` the drive's current ones. A track the
 * image does not have, or describes as unformatted, is noise of its length. */
static bool build_track(struct ipf_drive *dr, int track)
{
	if (dr->cached == track && dr->cells)
		return true;

	free(dr->cells);
	dr->cells = NULL;
	dr->cached = -1;

	uint32_t *cells = NULL, ncells = 0, splice = 0;
	bool noise = true;
	int idx = track >= 0 && track < MAX_UAE_TRACKS ? dr->track_of[track] : -1;
	if (idx >= 0) {
		/* generate_track changes size_cells; work on a copy, so the image
		 * stays as read */
		struct track_info t = dr->dec.tinfos[idx];
		if (!generate_track(&dr->dec, &t, &cells, &ncells, &splice, &noise) && !noise)
			return false; /* caps_loadimage built every track: not reached */
	}

	if (noise) {
		if (!ncells || ncells > MAX_CELLS)
			ncells = NOISE_CELLS;
		cells = (uint32_t *)malloc(ncells * sizeof(uint32_t));
		if (!cells)
			return false;
		for (uint32_t i = 0; i != ncells; i++)
			cells[i] = MG_N | 2000;
		splice = 0;
	}

	dr->cells = cells;
	dr->ncells = ncells;
	dr->splice = noise ? (uint32_t)-1 : splice;
	dr->flakey = false;
	dr->timed = false;
	for (uint32_t i = 0; i != ncells; i++) {
		if ((cells[i] & MG_MASK) == MG_N)
			dr->flakey = true;
		if ((cells[i] & TIME_MASK) != 2000)
			dr->timed = true;
	}
	dr->cached = track;
	return true;
}

/* The drive's current cells as UAE's track buffer: 16 cells to a word, the
 * first in the top bit; weak cells drawn anew. */
static void put_cells(const struct ipf_drive *dr, uae_u16 *mfmbuf)
{
	uint32_t words = (dr->ncells + 15) / 16;
	for (uint32_t w = 0; w != words; w++) {
		uae_u16 v = 0;
		for (uint32_t b = 0; b != 16; b++) {
			uint32_t i = w * 16 + b;
			uint32_t bit = 0;
			if (i < dr->ncells) {
				uint32_t level = dr->cells[i] & MG_MASK;
				bit = level == MG_1 ? 1 : level == MG_N ? weak_bit() : 0;
			}
			v = (uae_u16)((v << 1) | bit);
		}
		mfmbuf[w] = v;
	}
}

/* Each byte's cells' length, in thousandths of 2 microseconds, as the SPS
 * library hands it to UAE on a timed track. */
static void put_timing(const struct ipf_drive *dr, uae_u16 *tracktiming)
{
	uint32_t bytes = (dr->ncells + 7) / 8;
	for (uint32_t b = 0; b != bytes; b++) {
		uint32_t sum = 0, n = 0;
		for (uint32_t i = b * 8; i != b * 8 + 8 && i < dr->ncells; i++, n++)
			sum += dr->cells[i] & TIME_MASK;
		tracktiming[b] = (uae_u16)((sum / n + 1) / 2);
	}
}

int caps_init(void)
{
	return 1;
}

void caps_unloadimage(int drv)
{
	if (drv < 0 || drv > 3 || !drives[drv].loaded)
		return;
	drop_drive(&drives[drv]);
}

int caps_loadimage(struct zfile *zf, int drv, int *num_tracks)
{
	if (drv < 0 || drv > 3)
		return 0;
	struct ipf_drive *dr = &drives[drv];
	caps_unloadimage(drv);
	drop_drive(dr);

	zfile_fseek(zf, 0, SEEK_END);
	uae_s64 len = zfile_ftell(zf);
	zfile_fseek(zf, 0, SEEK_SET);
	if (len < 12)
		return 0;
	dr->dec.file = (uint8_t *)malloc((size_t)len);
	dr->dec.tinfos = (struct track_info *)calloc(MAX_TRACK_INDEX + 1, sizeof(struct track_info));
	if (!dr->dec.file || !dr->dec.tinfos || zfile_fread(dr->dec.file, (size_t)len, 1, zf) != 1) {
		drop_drive(dr);
		return 0;
	}
	dr->dec.size = (size_t)len;
	dr->dec.tcount = 84 * 2 + 1; /* Usual max */

	bool ok = scan_all_tags(&dr->dec, dr->dec.file, dr->dec.size);

	/* MAME's generate_tracks: every track described is built once, so an
	 * image MAME would refuse is refused here, when it is put in the drive,
	 * and not when the game reaches the track */
	for (int i = 0; i != MAX_UAE_TRACKS; i++)
		dr->track_of[i] = -1;
	for (uint32_t i = 0; ok && i != dr->dec.tcount; i++) {
		struct track_info *t = &dr->dec.tinfos[i];
		if (t->info_set && t->data) {
			struct track_info copy = *t;
			uint32_t *cells, ncells, splice;
			bool noise;
			if (!generate_track(&dr->dec, &copy, &cells, &ncells, &splice, &noise) && !noise)
				ok = false;
			free(cells);
			uint32_t ut = t->cylinder * 2 + t->head;
			if (ok && ut < MAX_UAE_TRACKS)
				dr->track_of[ut] = (int16_t)i;
		} else if (t->info_set || t->data)
			ok = false;
	}
	if (!ok) {
		write_log("IPF: the image could not be read\n");
		drop_drive(dr);
		return 0;
	}

	dr->loaded = true;
	*num_tracks = (int)((dr->dec.max_cylinder - dr->dec.min_cylinder + 1) * (dr->dec.max_head - dr->dec.min_head + 1));
	write_log("IPF: encoder %u, release %u revision %u, cylinders %u-%u, heads %u-%u\n",
		dr->dec.encoder_type, dr->dec.release, dr->dec.revision,
		dr->dec.min_cylinder, dr->dec.max_cylinder, dr->dec.min_head, dr->dec.max_head);
	return 1;
}

int caps_loadtrack(uae_u16 *mfmbuf, uae_u16 *tracktiming, int drv, int track, int *tracklength, int *multirev, int *gapoffset, int *nextrev, bool sametrack)
{
	(void)nextrev;
	(void)sametrack;
	if (tracktiming)
		*tracktiming = 0;
	if (drv < 0 || drv > 3 || !drives[drv].loaded)
		return 0;
	struct ipf_drive *dr = &drives[drv];
	if (!build_track(dr, track))
		return 0;

	put_cells(dr, mfmbuf);
	*tracklength = (int)dr->ncells;
	*multirev = dr->flakey ? 1 : 0;
	*gapoffset = dr->splice == (uint32_t)-1 ? -1 : (int)dr->splice;
	if (dr->timed && tracktiming)
		put_timing(dr, tracktiming);
	return 1;
}

int caps_loadrevolution(uae_u16 *mfmbuf, uae_u16 *tracktiming, int drv, int track, int *tracklength, int *nextrev, bool track_access_done)
{
	(void)nextrev;
	(void)track_access_done;
	if (drv < 0 || drv > 3 || !drives[drv].loaded)
		return 0;
	struct ipf_drive *dr = &drives[drv];
	if (!build_track(dr, track))
		return 0;

	/* the same cells, the weak ones drawn again */
	put_cells(dr, mfmbuf);
	*tracklength = (int)dr->ncells;
	if (dr->timed && tracktiming)
		put_timing(dr, tracktiming);
	return 1;
}
