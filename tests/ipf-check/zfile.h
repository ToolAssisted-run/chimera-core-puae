/* tests/ipf-check: stands in for PUAE's zfile.h - the three calls
 * waterbox/ipf-reader.c reads a disk with, as PUAE declares them. */
#pragma once
struct zfile;
extern uae_s64 zfile_fseek(struct zfile *z, uae_s64 offset, int mode);
extern uae_s64 zfile_ftell(struct zfile *z);
extern size_t zfile_fread(void *b, size_t l1, size_t l2, struct zfile *z);
