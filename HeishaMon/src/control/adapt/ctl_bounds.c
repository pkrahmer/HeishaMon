#include "ctl_bounds.h"

#include <math.h>
#include <stdbool.h>

typedef struct {
    double low;
    double high;
} bound_range;

/* A bound outside the enum has no range, so a cast integer cannot read past the table. */
static bool bound_range_of(ctl_bound bound, bound_range *range)
{
    switch (bound) {
    case CTL_BOUND_TEMPERATURE:
        range->low = -60.0;
        range->high = 60.0;
        return true;
    case CTL_BOUND_HEAT:
        range->low = 0.0;
        range->high = 30000.0;
        return true;
    case CTL_BOUND_PV:
        range->low = 0.0;
        range->high = 100000.0;
        return true;
    case CTL_BOUND_WEIGHT:
        range->low = 0.0;
        range->high = 1000.0;
        return true;
    }
    return false;
}

bool ctl_bounds_accept(ctl_bound bound, double value)
{
    bound_range range;
    if (!bound_range_of(bound, &range)) {
        return false;
    }
    return isfinite(value) && value >= range.low && value <= range.high;
}
