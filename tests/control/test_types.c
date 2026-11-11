#include "check.h"
#include "core/ctl_types.h"

#include <math.h>
#include <stdio.h>

/* The helper is checked first, by itself: every later line of this program trusts it.
 * Its counts are kept in locals and checked only after the reset, because a check of the
 * helper must not count against the helper's own demonstration of failures. */
static void test_check_helper(void)
{
    int failing_count;
    int failing_result;
    int passing_count;
    int passing_result;
    int equal_infinities;
    int two_nans;
    int signed_zeros;
    int next_double;
    int nan_against_zero;
    int before;

    printf("test_types: the next four lines are expected failures of the check helper\n");
    check_reset();
    CHECK_TRUE(1 == 2);
    CHECK_DOUBLE(1.0, 2.0);
    CHECK_UINT(1u, 2u);
    CHECK_TEXT("one", "two");
    failing_count = check_failures();
    failing_result = check_result();

    check_reset();
    CHECK_TRUE(1 == 1);
    CHECK_DOUBLE(1.0, 1.0);
    CHECK_UINT(2u, 2u);
    CHECK_TEXT("two", "two");
    passing_count = check_failures();
    passing_result = check_result();

    CHECK_DOUBLE(INFINITY, INFINITY);
    equal_infinities = check_failures();
    CHECK_DOUBLE(NAN, NAN);
    two_nans = check_failures();
    CHECK_DOUBLE(-0.0, 0.0);
    signed_zeros = check_failures();

    printf("test_types: the next two lines are expected failures of the check helper\n");
    before = check_failures();
    CHECK_DOUBLE(1.0, nextafter(1.0, 2.0));
    next_double = check_failures() - before;
    before = check_failures();
    CHECK_DOUBLE(NAN, 0.0);
    nan_against_zero = check_failures() - before;

    check_reset();
    CHECK_TRUE(failing_count == 4);
    CHECK_TRUE(failing_result == 1);
    CHECK_TRUE(passing_count == 0);
    CHECK_TRUE(passing_result == 0);
    CHECK_TRUE(equal_infinities == 0);
    CHECK_TRUE(two_nans == 0);
    CHECK_TRUE(signed_zeros == 0);
    CHECK_TRUE(next_double == 1);
    CHECK_TRUE(nan_against_zero == 1);
}

static void test_demand_names(void)
{
    CHECK_TEXT(ctl_demand_name(CTL_DEMAND_HEATING), "HEATING");
    CHECK_TEXT(ctl_demand_name(CTL_DEMAND_COOLING), "COOLING");
    CHECK_TEXT(ctl_demand_name(CTL_DEMAND_NONE), "NO_DEMAND");
    CHECK_TEXT(ctl_demand_name((ctl_demand)(CTL_DEMAND_NONE + 1)), "INVALID");
    CHECK_TEXT(ctl_demand_name((ctl_demand)-1), "INVALID");
}

static void test_phase_names(void)
{
    CHECK_TEXT(ctl_phase_name(CTL_PHASE_UNINITIALIZED), "UNINITIALIZED");
    CHECK_TEXT(ctl_phase_name(CTL_PHASE_RUNNING), "RUNNING");
    CHECK_TEXT(ctl_phase_name(CTL_PHASE_HOLD), "HOLD");
    CHECK_TEXT(ctl_phase_name(CTL_PHASE_FAULT), "FAULT");
    CHECK_TEXT(ctl_phase_name((ctl_phase)(CTL_PHASE_FAULT + 1)), "INVALID");
    CHECK_TEXT(ctl_phase_name((ctl_phase)-1), "INVALID");
}

static void test_reason_names(void)
{
    CHECK_TEXT(ctl_reason_name(CTL_REASON_NORMAL), "NORMAL");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_SEEDED), "SEEDED");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_WAITING_FOR_INPUTS), "WAITING_FOR_INPUTS");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_MASKED), "MASKED");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_HELD_STALE_INPUT), "HELD_STALE_INPUT");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_FAULT_TIMEOUT), "FAULT_TIMEOUT");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_FAULT_RECOVERED), "FAULT_RECOVERED");
    CHECK_TEXT(ctl_reason_name(CTL_REASON_INVALID_TIME), "INVALID_TIME");
    CHECK_TEXT(ctl_reason_name((ctl_reason)(CTL_REASON_INVALID_TIME + 1)), "INVALID");
    CHECK_TEXT(ctl_reason_name((ctl_reason)-1), "INVALID");
}

static void test_signal_names(void)
{
    CHECK_TEXT(ctl_signal_name(CTL_SIGNAL_T_INDOOR), "T_INDOOR");
    CHECK_TEXT(ctl_signal_name(CTL_SIGNAL_T_OUTDOOR), "T_OUTDOOR");
    CHECK_TEXT(ctl_signal_name(CTL_SIGNAL_P_PRODUCED), "P_PRODUCED");
    CHECK_TEXT(ctl_signal_name(CTL_SIGNAL_P_SOLAR), "P_SOLAR");
    CHECK_TEXT(ctl_signal_name(CTL_SIGNAL_P_INTERNAL), "P_INTERNAL");
    CHECK_TEXT(ctl_signal_name(CTL_SIGNAL_COUNT), "INVALID");
    CHECK_TEXT(ctl_signal_name((ctl_signal)-1), "INVALID");
}

static void test_required_signals(void)
{
    CHECK_TRUE(ctl_signal_is_required(CTL_SIGNAL_T_INDOOR));
    CHECK_TRUE(ctl_signal_is_required(CTL_SIGNAL_T_OUTDOOR));
    CHECK_TRUE(ctl_signal_is_required(CTL_SIGNAL_P_PRODUCED));
    CHECK_TRUE(!ctl_signal_is_required(CTL_SIGNAL_P_SOLAR));
    CHECK_TRUE(!ctl_signal_is_required(CTL_SIGNAL_P_INTERNAL));
    CHECK_TRUE(!ctl_signal_is_required(CTL_SIGNAL_COUNT));
    CHECK_TRUE(!ctl_signal_is_required((ctl_signal)-1));
}

int main(void)
{
    test_check_helper();
    test_demand_names();
    test_phase_names();
    test_reason_names();
    test_signal_names();
    test_required_signals();
    return check_result();
}
