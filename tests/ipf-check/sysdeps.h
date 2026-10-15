/* tests/ipf-check: stands in for PUAE's sysdeps.h - the types and the log
 * call waterbox/ipf-reader.c uses, as PUAE declares them. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned short uae_u16;
typedef int64_t uae_s64;
void write_log(const char *format, ...);
