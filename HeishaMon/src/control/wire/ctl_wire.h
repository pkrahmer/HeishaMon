/*
 * The line protocol of the device test: one request line in, one reply line out, each a call on
 * the control core or a change of the configuration it runs with. Doubles cross the line as the
 * 16 hexadecimal digits of their IEEE-754 bit pattern, because the test proves that a board
 * computes every bit as the host does, and decimal text would round on the way. The protocol is
 * plain C without input or output, so that the board's sketch and the host's twin run the same
 * code and only move lines.
 */
#ifndef CTL_WIRE_H
#define CTL_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/ctl_config.h"
#include "../core/ctl_controller.h"

#define CTL_WIRE_LINE_MAX 128   /* the longest request line, without its line end */
#define CTL_WIRE_REPLY_SIZE 256 /* the buffer a reply needs, with its terminating zero */

/* The caller owns the session; ctl_wire_handle changes it in place. */
typedef struct {
    ctl_config config;
    ctl_state state;
    bool begun;
    uint32_t calls; /* O, A, M and E requests answered since the last B */
} ctl_wire_session;

/* Sets the session as the request R does: the default configuration, a cold state, not begun,
 * no calls. Refuses nothing. */
void ctl_wire_init(ctl_wire_session *session);

/* Answers one request line, which holds no line end; a trailing carriage return is ignored.
 * Writes the reply without a line end into reply, cut to reply_size - 1 characters; a reply
 * needs CTL_WIRE_REPLY_SIZE. Refuses with the reply "error", changing nothing, a line longer
 * than CTL_WIRE_LINE_MAX, an unknown request, a missing, surplus or unparsable field, and a
 * call on the core before a successful B. Writes nothing when reply_size is 0. */
void ctl_wire_handle(ctl_wire_session *session, const char *line, char *reply, size_t reply_size);

/* Writes the 16 lowercase hexadecimal digits of the value's bit pattern, most significant
 * first, and a terminating zero: text needs 17 characters. Refuses nothing: NaN and the
 * infinities have their patterns too. */
void ctl_wire_format_double(double value, char *text);

/* Returns true and sets *value to the double whose bit pattern the text spells. Refuses,
 * returning false and leaving *value untouched, anything but exactly 16 lowercase hexadecimal
 * digits followed by the terminating zero. */
bool ctl_wire_parse_double(const char *text, double *value);

#endif
