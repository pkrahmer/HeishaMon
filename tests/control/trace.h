/*
 * The golden trace replayer: reads a trace of the Python reference (docs/control/golden-traces.md),
 * makes each recorded call into the C core from a cold start, and compares every recorded value
 * with what the core holds after the call, exactly. A difference means the port decides
 * otherwise than the reference; there is no tolerance, because the core is meant to reproduce
 * the reference bit for bit.
 */
#ifndef TRACE_H
#define TRACE_H

#include <stdbool.h>
#include <stdio.h>

/* The reader's line buffer: a longer line is a format error. */
#define TRACE_LINE_MAX 1024

typedef struct {
    long calls;        /* call lines replayed */
    long mismatches;   /* values that differed, counted up to the limit */
    long format_line;  /* the line of the first format error, or 0 */
} trace_result;

/* Returns true only when the trace at path was read to its end line without a format error and
 * without a mismatch. Fills result in every case. Prints one line to standard output for each
 * value that differs, and stops the replay after mismatch_limit of them, a limit below 1 counting
 * as 1, or at the first format error. Refuses, returning false with format_line 0, a file it
 * cannot open, and says so in one printed line. */
bool trace_replay(const char *path, long mismatch_limit, trace_result *result);

/* Returns and fills what trace_replay does, and prints its lines to output instead, so that a
 * test can read what the replay reported. */
bool trace_replay_to(FILE *output, const char *path, long mismatch_limit, trace_result *result);

#endif
