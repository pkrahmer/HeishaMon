/*
 * The text writer: the loop's record line and the telemetry's values are built from the same
 * pieces (numbers in the fixed format, inputs that may be missing, the router's commands), and
 * one writer keeps them written alike. A text is written into a buffer of a known size; once a
 * piece does not fit, the writer stops and marks the text as not fitting, so that the caller can
 * refuse it whole rather than publish it cut.
 */
#ifndef CTL_TEXT_H
#define CTL_TEXT_H

#include "../route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *text;
    size_t size;
    size_t length;
    bool fits; /* false once a piece did not fit or a number could not be written */
} ctl_text;

/* Starts an empty text in buffer, kept terminated after every piece. A size of 0 starts a text
 * that fits nothing, and its buffer is never written. */
void ctl_text_start(ctl_text *writer, char *buffer, size_t size);

/* Appends piece. Refuses, by clearing fits, a piece that does not fit with the terminating zero;
 * the text keeps what fitted before. Writes nothing once fits is false. */
void ctl_text_put(ctl_text *writer, const char *piece);

/* Appends at most limit characters of piece, for a field of a fixed size that may lack its
 * terminating zero; refuses as ctl_text_put does. */
void ctl_text_put_bounded(ctl_text *writer, const char *piece, size_t limit);

/* Appends value as ctl_format_fixed writes it with decimals; refuses, by clearing fits, what
 * ctl_format_fixed refuses and what does not fit. */
void ctl_text_put_number(ctl_text *writer, double value, int decimals);

/* Appends value as ctl_text_put_number does when has_inputs is true, and "-" otherwise: without
 * inputs the core resolved nothing, and its zeros there are no measurement. Refuses as
 * ctl_text_put_number does. */
void ctl_text_put_input(ctl_text *writer, bool has_inputs, double value, int decimals);

/* Appends the commands as name:value joined by ",", or "-" when there is none; refuses as
 * ctl_text_put does. */
void ctl_text_put_commands(ctl_text *writer, const ctl_commands *commands);

#endif
