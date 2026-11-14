/*
 * The plausibility bounds of the adapters: the range a value from outside must lie in before it
 * reaches the core. A sensor that reports 85 degC or a meter that reports a negative heat is
 * broken, and the core should see such a value as missing rather than act on it.
 */
#ifndef CTL_BOUNDS_H
#define CTL_BOUNDS_H

#include <stdbool.h>

typedef enum {
    CTL_BOUND_TEMPERATURE, /* -60.0 to 60.0 degC, both included */
    CTL_BOUND_HEAT,        /* 0.0 to 30000.0 W, both included */
    CTL_BOUND_PV,          /* 0.0 to 100000.0 W, both included */
    CTL_BOUND_WEIGHT       /* 0.0 to 1000.0, both included */
} ctl_bound;

/* Returns true when value is finite and within the bound's range, both ends included. Refuses,
 * returning false, NaN, both infinities, a value outside the range, and a bound that is no
 * member of ctl_bound. */
bool ctl_bounds_accept(ctl_bound bound, double value);

#endif
