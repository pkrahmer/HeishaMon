/*
 * The store of the learned integral: the rules the glue follows to keep the integral term in a
 * file of its own across a restart. The term learns over about a day, and a restart would throw
 * that away; the flash wears with every write, so the file is written rarely. The glue does the
 * file access; what it decides, and the text it writes and reads, is here, so that the host can
 * test it.
 */
#ifndef CTL_INTEGRAL_STORE_H
#define CTL_INTEGRAL_STORE_H

#include "../loop/ctl_format.h"

#include <stdbool.h>
#include <stddef.h>

#define CTL_INTEGRAL_STORE_INTERVAL_S 21600.0 /* the shortest time between two writes: 6 h */
#define CTL_INTEGRAL_STORE_MOVE_W 10.0        /* the smallest move worth a write */
#define CTL_INTEGRAL_STORE_TEXT_MAX CTL_FORMAT_TEXT_MAX

/* Returns true when the file is to be written now: always after a reset, and otherwise only when
 * since_write_s is at least CTL_INTEGRAL_STORE_INTERVAL_S and the integral differs from the saved
 * value by at least CTL_INTEGRAL_STORE_MOVE_W. Returns false for a NaN anywhere but a reset. */
bool ctl_integral_store_due(double integral_w, double saved_w, double since_write_s, bool reset);

/* Parses the length bytes of the file's text, which need no terminating zero, as one bare number
 * with optional white space after it, and returns true with the value in *value_w. Refuses,
 * returning false and leaving *value_w as it was, an empty text, anything but a bare number and
 * a number that is not finite as a double. */
bool ctl_integral_store_parse(const char *text, size_t length, double *value_w);

/* Returns the integral's start value at boot: the parsed text clamped to -limit_w and limit_w,
 * or 0.0 for a text ctl_integral_store_parse refuses. Refuses nothing. */
double ctl_integral_store_start(const char *text, size_t length, double limit_w);

/* Writes the file's text of value_w, with three decimals, and returns true. Refuses, returning
 * false and writing nothing, what ctl_format_fixed refuses: a magnitude of
 * CTL_FORMAT_MAGNITUDE_LIMIT or more, NaN, the infinities and a size too small. */
bool ctl_integral_store_format(double value_w, char *text, size_t size);

#endif
