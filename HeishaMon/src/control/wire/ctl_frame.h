/*
 * The frame around every line of the device test: the text, a space, '*' and the XOR of the
 * text's bytes as two lowercase hexadecimal digits, as in "ok *04". A serial line at high speed
 * can drop or repeat a character; the frame lets each side tell a broken line from a reply, so
 * that the replay restarts a trace instead of comparing garbage or crashing on it. The frame is
 * plain C without input or output, shared by the board's sketch and the host's twin.
 */
#ifndef CTL_FRAME_H
#define CTL_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTL_FRAME_SUFFIX 4 /* " *" and two digits */

/* Returns the XOR of the first length bytes of text, 0 for none. Refuses nothing. */
uint8_t ctl_frame_checksum(const char *text, size_t length);

/* Appends the frame to the zero-terminated line in place and returns true. Refuses, returning
 * false and leaving line as it was, when size cannot hold the framed line and its zero. */
bool ctl_frame_add(char *line, size_t size);

/* Cuts the frame off a framed line in place and returns true; a trailing carriage return is
 * ignored first. Refuses, returning false and leaving line as it was, a line without the frame,
 * with a wrong checksum, with uppercase or missing digits, or with text after the digits. */
bool ctl_frame_strip(char *line);

#endif
