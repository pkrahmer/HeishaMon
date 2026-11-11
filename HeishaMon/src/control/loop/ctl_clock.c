#include "ctl_clock.h"

#include <stdbool.h>
#include <stdint.h>

#define WRAP ((uint64_t)1 << 32)

void ctl_clock_init(ctl_clock *clock)
{
    clock->started = false;
    clock->last_ms = 0u;
    clock->high = 0u;
}

uint64_t ctl_clock_widen(ctl_clock *clock, uint32_t now_ms)
{
    if (clock->started && now_ms < clock->last_ms) {
        clock->high += WRAP;
    }
    clock->started = true;
    clock->last_ms = now_ms;
    return clock->high + now_ms;
}
