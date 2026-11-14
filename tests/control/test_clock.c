#include "check.h"

#include "loop/ctl_clock.h"

#include <stddef.h>
#include <stdint.h>

/* One call of a sequence on one clock: the counter handed in and the widened value expected. */
typedef struct {
    uint32_t now_ms;
    uint64_t widened;
} clock_row;

static void check_u64(const char *file, int line, uint64_t actual, uint64_t expected)
{
    check_uint(file, line, (uint32_t)(actual >> 32), (uint32_t)(expected >> 32));
    check_uint(file, line, (uint32_t)actual, (uint32_t)expected);
}

static void test_widening_sequence(void)
{
    static const clock_row rows[] = {
        {5u, UINT64_C(5)},
        {4294967295u, UINT64_C(4294967295)},
        {3u, UINT64_C(4294967299)},
        {3u, UINT64_C(4294967299)},
        {2u, UINT64_C(8589934594)},
    };
    ctl_clock clock;
    size_t i;
    ctl_clock_init(&clock);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_u64(__FILE__, __LINE__, ctl_clock_widen(&clock, rows[i].now_ms), rows[i].widened);
    }
}

static void test_first_call_starts_the_clock(void)
{
    ctl_clock clock;
    ctl_clock_init(&clock);
    CHECK_TRUE(!clock.started);
    check_u64(__FILE__, __LINE__, ctl_clock_widen(&clock, 1000u), UINT64_C(1000));
    CHECK_TRUE(clock.started);
    CHECK_UINT(clock.last_ms, 1000u);
}

int main(void)
{
    test_widening_sequence();
    test_first_call_starts_the_clock();
    return check_result();
}
