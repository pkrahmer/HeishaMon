/*
 * The clock of the loop: the board's millisecond counter is 32 bits wide and wraps every 49.7
 * days, but the core needs elapsed time that never jumps back. Called often enough, at least once
 * per wrap, the clock widens the counter to 64 bits by counting the wraps it sees.
 */
#ifndef CTL_CLOCK_H
#define CTL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool started;
    uint32_t last_ms; /* the counter at the last call */
    uint64_t high;    /* 2^32 times the wraps seen so far */
} ctl_clock;

/* Leaves the clock not started, so that its first call takes the counter as it is. */
void ctl_clock_init(ctl_clock *clock);

/* Returns the widened counter: high plus now_ms, after adding 2^32 to high when now_ms lies
 * below the counter of the previous call. Refuses nothing; two wraps between calls count as one,
 * which is why the caller must call at least once per wrap. */
uint64_t ctl_clock_widen(ctl_clock *clock, uint32_t now_ms);

#endif
