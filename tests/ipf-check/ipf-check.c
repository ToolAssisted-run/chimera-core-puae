/*
 * ipf-check - drives the core's IPF reader (waterbox/ipf-reader.c) the way
 * PUAE's floppy drive does, on an image tests/make-ipf.py wrote, and checks
 * what it hands over against what that script says the tracks hold.
 *
 *   ipf-check <image.ipf> <image.expect>
 *
 * For every track of the image, read on two revolutions:
 *   - every cell that is not weak is the cell the script wrote, on both;
 *   - weak cells come out different on the second revolution (at least a
 *     quarter of them), the track is said to need reading again every
 *     revolution, and a track with no weak cells is not;
 *   - the write splice is where the reader puts it (the middle of the gap);
 *   - a track of density type 2 has no cell lengths; one of type 3
 *     (Copylock) has 1000 for every byte but blocks 4, 5 and 6: 945, 995
 *     and 1045 (thousandths of 2 microseconds).
 * The tracks the image does not have (cylinders 80 to 83) read as noise:
 * different on every revolution.
 *
 * It prints one line with what it found and a digest of everything the
 * reader handed over, so two runs can be compared, and exits 0 only when
 * every check held.
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "zfile.h"
#include "caps.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- the three file calls, over a file read whole --------------------- */
struct zfile {
	unsigned char *data;
	uae_s64 size, pos;
};

uae_s64 zfile_fseek(struct zfile *z, uae_s64 offset, int mode)
{
	z->pos = mode == SEEK_SET ? offset : mode == SEEK_END ? z->size + offset : z->pos + offset;
	return 0;
}

uae_s64 zfile_ftell(struct zfile *z)
{
	return z->pos;
}

size_t zfile_fread(void *b, size_t l1, size_t l2, struct zfile *z)
{
	size_t want = l1 * l2;
	if (z->pos + (uae_s64)want > z->size)
		return 0;
	memcpy(b, z->data + z->pos, want);
	z->pos += want;
	return l2;
}

void write_log(const char *format, ...)
{
	(void)format;
}

static unsigned char *read_file(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *d = malloc(*size + 1);
	if (fread(d, 1, *size, f) != (size_t)*size) {
		fclose(f);
		free(d);
		return NULL;
	}
	fclose(f);
	d[*size] = 0;
	return d;
}

static uint64_t digest = 1469598103934665603ull;
static void hash(const void *p, size_t n)
{
	const unsigned char *b = p;
	for (size_t i = 0; i < n; i++)
		digest = (digest ^ b[i]) * 1099511628211ull;
}

static int cell(const uae_u16 *buf, int i)
{
	return (buf[i >> 4] >> (15 - (i & 15))) & 1;
}

#define WORDS (0x4000 * 2)

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: ipf-check <image.ipf> <image.expect>\n");
		return 2;
	}
	long isize, esize;
	struct zfile zf;
	zf.data = read_file(argv[1], &isize);
	char *expect = (char *)read_file(argv[2], &esize);
	if (!zf.data || !expect) {
		fprintf(stderr, "cannot read %s or %s\n", argv[1], argv[2]);
		return 2;
	}
	zf.size = isize;
	zf.pos = 0;

	int tracks = 0;
	if (!caps_loadimage(&zf, 0, &tracks)) {
		printf("FAIL the reader refused the image\n");
		return 1;
	}

	static uae_u16 rev1[WORDS], rev2[WORDS], time1[WORDS * 2], time2[WORDS * 2];
	int failures = 0, checked = 0, weak_tracks = 0, timed_tracks = 0;
	long weak_cells = 0, weak_changed = 0;
	const char *e = expect;
	for (int t = 0; t < 168; t++) {
		int len = 0, multirev = 0, gap = 0, nextrev = 0;
		memset(rev1, 0, sizeof rev1);
		memset(rev2, 0, sizeof rev2);
		memset(time1, 0, sizeof time1);
		memset(time2, 0, sizeof time2);
		if (!caps_loadtrack(rev1, time1, 0, t, &len, &multirev, &gap, &nextrev, false)) {
			printf("FAIL track %d: not read\n", t);
			return 1;
		}
		int len2 = 0;
		caps_loadrevolution(rev2, time2, 0, t, &len2, &nextrev, false);
		hash(&len, sizeof len);
		hash(&multirev, sizeof multirev);
		hash(&gap, sizeof gap);
		hash(rev1, (len + 15) / 16 * 2);
		hash(rev2, (len + 15) / 16 * 2);
		if (time1[0])
			hash(time1, (len + 7) / 8 * 2);

		if (t >= 160) {
			/* not in the image: noise, different every revolution */
			int changed = 0;
			for (int i = 0; i < len; i++)
				changed += cell(rev1, i) != cell(rev2, i);
			if (!multirev || changed < len / 4 || len2 != len) {
				printf("FAIL track %d (not in the image): multirev %d, %d of %d cells changed\n", t, multirev, changed, len);
				failures++;
			}
			continue;
		}

		int et, en, ed;
		if (sscanf(e, "%d %d %d", &et, &en, &ed) != 3 || et != t) {
			printf("FAIL the expect file has no track %d\n", t);
			return 1;
		}
		e = strchr(e, '\n') + 1;
		const char *cells = e;
		e += en + 1;
		checked++;

		if (len != en || len2 != len) {
			printf("FAIL track %d: %d cells (and %d on the second revolution), %d expected\n", t, len, len2, en);
			failures++;
			continue;
		}
		int wrong = 0, weak = 0, changed = 0;
		for (int i = 0; i < len; i++) {
			if (cells[i] == 'w') {
				weak++;
				changed += cell(rev1, i) != cell(rev2, i);
			} else if (cell(rev1, i) != cells[i] - '0' || cell(rev2, i) != cells[i] - '0')
				wrong++;
		}
		weak_cells += weak;
		weak_changed += changed;
		if (weak)
			weak_tracks++;
		if (wrong) {
			printf("FAIL track %d: %d cells are not the ones written\n", t, wrong);
			failures++;
		}
		if ((weak > 0) != (multirev != 0) || (weak && changed < weak / 4)) {
			printf("FAIL track %d: %d weak cells, %d of them changed, multirev %d\n", t, weak, changed, multirev);
			failures++;
		}
		/* the splice: in the middle of the gap after the last of 11 sectors */
		if (gap != 11 * 8704 + (len - 11 * 8704) / 2) {
			printf("FAIL track %d: the write splice is at %d\n", t, gap);
			failures++;
		}
		if (ed == 2 && time1[0]) {
			printf("FAIL track %d: cell lengths on a track of density type 2\n", t);
			failures++;
		}
		if (ed == 3) {
			timed_tracks++;
			int bad = 0;
			for (int b = 0; b < (len + 7) / 8; b++) {
				int want = b >= 4 * 1088 && b < 5 * 1088 ? 945 : b >= 5 * 1088 && b < 6 * 1088 ? 995 : b >= 6 * 1088 && b < 7 * 1088 ? 1045 : 1000;
				bad += time1[b] != want || time2[b] != want;
			}
			if (bad) {
				printf("FAIL track %d: %d bytes have another cell length than Copylock's\n", t, bad);
				failures++;
			}
		}
	}
	caps_unloadimage(0);

	printf("%s %d tracks as written, %d with weak cells (%ld of %ld changed on the next revolution), %d with Copylock's cell lengths, 8 not in the image as noise; digest %016llx\n",
		failures ? "FAIL" : "PASS", checked, weak_tracks, weak_changed, weak_cells, timed_tracks, (unsigned long long)digest);
	return failures ? 1 : 0;
}
