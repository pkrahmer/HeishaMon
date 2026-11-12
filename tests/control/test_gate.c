#include "check.h"

#include "glue/ctl_gate.h"
#include "loop/ctl_loop.h"
#include "route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROOMS 4
#define DECISION_TICKS 20

#define CHECK_GATE(actual, expected) CHECK_UINT((uint32_t)(actual), (uint32_t)(expected))

typedef struct {
    bool enabled;
    bool listen_only;
    bool optional_pcb;
    ctl_gate gate;
} gate_row;

static void test_state(void)
{
    static const gate_row rows[] = {
        {false, false, false, CTL_GATE_DISABLED},
        {false, false, true, CTL_GATE_DISABLED},
        {false, true, false, CTL_GATE_DISABLED},
        {false, true, true, CTL_GATE_DISABLED},
        {true, true, false, CTL_GATE_LISTEN_ONLY},
        {true, true, true, CTL_GATE_LISTEN_ONLY},
        {true, false, false, CTL_GATE_NO_OPTIONAL_PCB},
        {true, false, true, CTL_GATE_OPEN},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_GATE(ctl_gate_state(rows[i].enabled, rows[i].listen_only, rows[i].optional_pcb),
                   rows[i].gate);
    }
}

static void test_name(void)
{
    CHECK_TEXT(ctl_gate_name(CTL_GATE_OPEN), "-");
    CHECK_TEXT(ctl_gate_name(CTL_GATE_DISABLED), "disabled");
    CHECK_TEXT(ctl_gate_name(CTL_GATE_LISTEN_ONLY), "listen_only");
    CHECK_TEXT(ctl_gate_name(CTL_GATE_NO_OPTIONAL_PCB), "no_optional_pcb");
    CHECK_TEXT(ctl_gate_name((ctl_gate)9), "invalid");
}

/* Steps the loop for the decision ticks with no confirmation, as a blocked board does, and
 * returns whether the router ever reported a fault. */
static bool faults_unconfirmed(bool assume_landed)
{
    ctl_loop_config config;
    ctl_loop loop;
    ctl_loop_record record;
    bool faulted = false;
    int decisions = 0;
    int step;

    ctl_loop_config_defaults(&config, ROOMS);
    config.assume_landed = assume_landed;
    ctl_loop_init(&loop, &config, 0.0);
    for (step = 1; decisions < DECISION_TICKS && step <= 10 * DECISION_TICKS; step++) {
        if (!ctl_loop_tick(&loop, &config, 10.0 * step, &record)) {
            continue;
        }
        decisions++;
        if (record.router_status == CTL_ROUTER_FAULT) {
            faulted = true;
        }
    }
    CHECK_UINT((uint32_t)decisions, (uint32_t)DECISION_TICKS);
    return faulted;
}

static void test_assumed_landing(void)
{
    CHECK_TRUE(!faults_unconfirmed(true));
    CHECK_TRUE(faults_unconfirmed(false));
}

int main(void)
{
    test_state();
    test_name();
    test_assumed_landing();
    return check_result();
}
