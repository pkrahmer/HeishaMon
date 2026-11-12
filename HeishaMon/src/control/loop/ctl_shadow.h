/*
 * The shadow protocol: the loop driven by request lines, so that the shadow sketch on a spare
 * board and its host twin run the whole control path live, fed line by line from the house, and
 * report every decision as a line. The board is wired to no pump and the loop assumes every
 * command landed, so the router's commands only ever reach the records. The sketch and the twin
 * move framed lines and read their clocks; everything a line means is here, pure like the loop.
 * The functions work on the text inside the frame of ctl_frame.h.
 */
#ifndef CTL_SHADOW_H
#define CTL_SHADOW_H

#include "ctl_loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTL_SHADOW_LINE_MAX 240 /* the longest request line, without frame and line end */

typedef struct {
    ctl_loop_config config;
    ctl_loop loop;
    bool started;
    uint32_t inputs;
    uint32_t records;
    bool has_record; /* a record waits to be taken by ctl_shadow_poll */
    ctl_loop_record record;
} ctl_shadow;

/* Sets the loop's defaults for 4 rooms with assume_landed true, not started, both counts 0. */
void ctl_shadow_init(ctl_shadow *shadow);

/* Answers one request line, received at now, with its reply text in reply: "ok", "bad", one of
 * "taken", "no_reading", "refused" or "bad" for an input, or the stats line. Refuses with
 * "error", changing nothing, a line longer than CTL_SHADOW_LINE_MAX, an unknown request or name,
 * a missing or surplus field, a field that does not parse, a value outside its range, a
 * configuration after the start, an input before it and a second start. A reply that does not fit
 * size is left empty. */
void ctl_shadow_handle(ctl_shadow *shadow, const char *line, double now, char *reply, size_t size);

/* Ticks a started loop at now. Returns true with "D " and the record's text in text when a record
 * was due, counting it. Returns false and writes nothing when no record is due, when the shadow
 * is not started, and when the record does not fit size: such a record waits and is written by
 * the next call that can hold it. */
bool ctl_shadow_poll(ctl_shadow *shadow, double now, char *text, size_t size);

#endif
