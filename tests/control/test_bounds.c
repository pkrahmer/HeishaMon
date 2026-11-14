#include "check.h"

#include "adapt/ctl_bounds.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static void test_limits(void)
{
    static const struct {
        ctl_bound bound;
        double low;
        double high;
    } ranges[] = {
        {CTL_BOUND_TEMPERATURE, -60.0, 60.0},
        {CTL_BOUND_HEAT, 0.0, 30000.0},
        {CTL_BOUND_PV, 0.0, 100000.0},
        {CTL_BOUND_WEIGHT, 0.0, 1000.0},
    };
    size_t i;
    for (i = 0; i < sizeof ranges / sizeof ranges[0]; i++) {
        ctl_bound bound = ranges[i].bound;
        CHECK_TRUE(ctl_bounds_accept(bound, ranges[i].low));
        CHECK_TRUE(ctl_bounds_accept(bound, ranges[i].high));
        CHECK_TRUE(!ctl_bounds_accept(bound, nextafter(ranges[i].low, -INFINITY)));
        CHECK_TRUE(!ctl_bounds_accept(bound, nextafter(ranges[i].high, INFINITY)));
        CHECK_TRUE(!ctl_bounds_accept(bound, NAN));
        CHECK_TRUE(!ctl_bounds_accept(bound, INFINITY));
        CHECK_TRUE(!ctl_bounds_accept(bound, -INFINITY));
    }
}

static void test_heat_by_a_milliwatt(void)
{
    CHECK_TRUE(!ctl_bounds_accept(CTL_BOUND_HEAT, -0.001));
    CHECK_TRUE(!ctl_bounds_accept(CTL_BOUND_HEAT, 30000.001));
}

static void test_unknown_bound(void)
{
    CHECK_TRUE(!ctl_bounds_accept((ctl_bound)4, 0.0));
}

int main(void)
{
    test_limits();
    test_heat_by_a_milliwatt();
    test_unknown_bound();
    return check_result();
}
