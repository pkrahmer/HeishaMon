#include "check.h"

#include "core/ctl_types.h"
#include "route/ctl_router.h"

#include <stdbool.h>
#include <stdint.h>

static ctl_router_config defaults(void)
{
    ctl_router_config config;
    ctl_router_config_defaults(&config);
    return config;
}

/* A router that sent 2 at 0.0, never saw it land and gave up after its repeats at 120.0, 240.0
 * and 360.0, so that it faults at 480.0 with its last send at 360.0. */
static void faulted_heating(ctl_router *router, const ctl_router_config *config)
{
    ctl_commands commands;
    double now;
    ctl_router_init(router);
    for (now = 0.0; now <= 480.0; now += 120.0) {
        ctl_router_step(router, config, CTL_DEMAND_HEATING, false, 0, now, &commands);
    }
}

static void test_faulted_router_sends_once_after_pacing(void)
{
    const ctl_router_config config = defaults();
    ctl_router router;
    ctl_commands commands;

    faulted_heating(&router, &config);
    CHECK_UINT((uint32_t)router.status, (uint32_t)CTL_ROUTER_FAULT);
    CHECK_DOUBLE(router.sent_at, 360.0);

    ctl_router_reopen(&router);
    CHECK_UINT((uint32_t)router.status, (uint32_t)CTL_ROUTER_IDLE);
    CHECK_UINT((uint32_t)router.retries, 0u);
    CHECK_UINT((uint32_t)router.has_sent, 1u);

    /* Within the pacing interval after the last send: nothing. */
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 0, 659.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 0u);

    /* At the interval: the wanted state, once. */
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 0, 660.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 1u);
    CHECK_TEXT(commands.command[0].name, "SetExternalThermostat1State");
    CHECK_TEXT(commands.command[0].value, "2");
    CHECK_UINT((uint32_t)router.status, (uint32_t)CTL_ROUTER_WAITING);

    /* It lands: nothing more. */
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 2, 661.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 0u);
    CHECK_UINT((uint32_t)router.status, (uint32_t)CTL_ROUTER_IDLE);
}

static void test_long_after_last_send_sends_at_once(void)
{
    const ctl_router_config config = defaults();
    ctl_router router;
    ctl_commands commands;

    faulted_heating(&router, &config);
    ctl_router_reopen(&router);
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 0, 5000.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 1u);
    CHECK_TEXT(commands.command[0].value, "2");
}

static void test_landed_state_is_sent_again_once(void)
{
    const ctl_router_config config = defaults();
    ctl_router router;
    ctl_commands commands;

    ctl_router_init(&router);
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, true, 0, 0.0, &commands);
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, true, 0, 10.0, &commands);
    CHECK_UINT((uint32_t)router.status, (uint32_t)CTL_ROUTER_IDLE);

    ctl_router_reopen(&router);
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, true, 0, 299.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 0u);
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, true, 0, 300.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 1u);
    CHECK_TEXT(commands.command[0].value, "0");
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, true, 0, 900.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 0u);
}

static void test_never_sent_router_sends_at_once(void)
{
    const ctl_router_config config = defaults();
    ctl_router router;
    ctl_commands commands;

    ctl_router_init(&router);
    ctl_router_reopen(&router);
    CHECK_UINT((uint32_t)router.has_sent, 0u);
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 0, 5.0, &commands);
    CHECK_UINT((uint32_t)commands.count, 1u);
    CHECK_TEXT(commands.command[0].value, "2");
}

int main(void)
{
    test_faulted_router_sends_once_after_pacing();
    test_long_after_last_send_sends_at_once();
    test_landed_state_is_sent_again_once();
    test_never_sent_router_sends_at_once();
    return check_result();
}
