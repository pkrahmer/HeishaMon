/*
 * The fixed-point text of a number. The loop writes its records on a board whose printf may
 * lack floating point, or pull in a large library for it, so numbers are written here with
 * integer arithmetic only, with a fixed count of decimals so that every record reads alike.
 */
#ifndef CTL_FORMAT_H
#define CTL_FORMAT_H

#include <stdbool.h>
#include <stddef.h>

#define CTL_FORMAT_DECIMALS_MAX 3
#define CTL_FORMAT_MAGNITUDE_LIMIT 1e12 /* the first magnitude refused */
/* Holds every text ctl_format_fixed accepts with its terminating zero: a sign, 13 digits (a value
 * just below the limit can round up to it), a point and 3 decimals, with room to spare. */
#define CTL_FORMAT_TEXT_MAX 24

/* Writes value with exactly decimals digits after the point (no point for 0), rounded half away
 * from zero after scaling by 10^decimals, with a '-' only before a result that is not zero, and
 * returns true. NaN is written "nan" and the infinities "inf" and "-inf". Refuses, returning
 * false and writing nothing, decimals outside 0 to CTL_FORMAT_DECIMALS_MAX, a finite magnitude of
 * CTL_FORMAT_MAGNITUDE_LIMIT or more, and a size that cannot hold the text and its terminating
 * zero. */
bool ctl_format_fixed(double value, int decimals, char *text, size_t size);

#endif
