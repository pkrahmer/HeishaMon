#include "check.h"

#include "core/ctl_types.h"
#include "glue/ctl_dispatch.h"
#include "route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMART_GRID "SetSmartGridMode"

typedef struct {
    int line;
    int heat_value;
    ctl_demand demand;
    const char *value;
    int reported; /* the Smart Grid mode the pump shows for value */
} smart_grid_row;

static const smart_grid_row ROWS[] = {
    {__LINE__, -1, CTL_DEMAND_HEATING, "0", 0}, {__LINE__, -1, CTL_DEMAND_COOLING, "0", 0},
    {__LINE__, -1, CTL_DEMAND_NONE, "1", 1},    {__LINE__, 1, CTL_DEMAND_HEATING, "0", 0},
    {__LINE__, 1, CTL_DEMAND_COOLING, "0", 0},  {__LINE__, 1, CTL_DEMAND_NONE, "1", 1},
};

static ctl_router_config smart_grid_config(int heat_value)
{
    ctl_router_config config;
    ctl_router_config_defaults(&config);
    config.smart_grid = true;
    config.heat_cool_heat_value = heat_value;
    return config;
}

/* Exactly one command, SetSmartGridMode with the value, or none when value is NULL. */
static void check_one(int line, const ctl_commands *commands, const char *value)
{
    if (value == NULL) {
        check_uint(__FILE__, line, (uint32_t)commands->count, 0u);
        return;
    }
    check_uint(__FILE__, line, (uint32_t)commands->count, 1u);
    if (commands->count == 1u) {
        check_text(__FILE__, line, commands->command[0].name, SMART_GRID);
        check_text(__FILE__, line, commands->command[0].value, value);
    }
}

static void test_config(void)
{
    ctl_router_config config;
    ctl_router_config_defaults(&config);
    CHECK_TRUE(!config.smart_grid);
    config.smart_grid = true;
    CHECK_TRUE(ctl_router_config_valid(&config));
}

/* The first send of each demand, and the same router seeing it land. */
static void test_send_and_land(void)
{
    size_t i;
    for (i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        const smart_grid_row *row = &ROWS[i];
        const ctl_router_config config = smart_grid_config(row->heat_value);
        ctl_router router;
        ctl_commands commands;
        ctl_router_init(&router);
        ctl_router_step(&router, &config, row->demand, false, 0, 0.0, &commands);
        check_one(row->line, &commands, row->value);
        check_uint(__FILE__, row->line, (uint32_t)router.status, (uint32_t)CTL_ROUTER_WAITING);
        ctl_router_step(&router, &config, row->demand, true, row->reported, 10.0, &commands);
        check_one(row->line, &commands, NULL);
        check_uint(__FILE__, row->line, (uint32_t)router.status, (uint32_t)CTL_ROUTER_IDLE);
        ctl_router_step(&router, &config, row->demand, true, row->reported, 1000.0, &commands);
        check_one(row->line, &commands, NULL);
    }
}

/* A report other than the value sent: a repeat every retry interval, then the fault. */
static void test_retries_to_fault(void)
{
    size_t i;
    for (i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        const smart_grid_row *row = &ROWS[i];
        const ctl_router_config config = smart_grid_config(row->heat_value);
        const int other = row->reported == 0 ? 1 : 0;
        ctl_router router;
        ctl_commands commands;
        int repeat;
        ctl_router_init(&router);
        ctl_router_step(&router, &config, row->demand, true, other, 0.0, &commands);
        check_one(row->line, &commands, row->value);
        for (repeat = 1; repeat <= config.max_retries; repeat++) {
            const double now = config.retry_interval_s * repeat;
            ctl_router_step(&router, &config, row->demand, true, other, now - 1.0, &commands);
            check_one(row->line, &commands, NULL);
            ctl_router_step(&router, &config, row->demand, true, other, now, &commands);
            check_one(row->line, &commands, row->value);
            check_uint(__FILE__, row->line, (uint32_t)router.retries, (uint32_t)repeat);
            check_uint(__FILE__, row->line, (uint32_t)router.status,
                       (uint32_t)CTL_ROUTER_WAITING);
        }
        ctl_router_step(&router, &config, row->demand, true, other,
                        config.retry_interval_s * (config.max_retries + 1), &commands);
        check_one(row->line, &commands, NULL);
        check_uint(__FILE__, row->line, (uint32_t)router.status, (uint32_t)CTL_ROUTER_FAULT);
    }
}

/* Heating and cooling are the same Smart Grid mode, so a change between them is no new state and
 * writes nothing; a change to no demand is one, paced as a thermostat change is. */
static void test_changes(void)
{
    const ctl_router_config config = smart_grid_config(1);
    ctl_router router;
    ctl_commands commands;
    ctl_router_init(&router);
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 0, 0.0, &commands);
    check_one(__LINE__, &commands, "0");
    ctl_router_step(&router, &config, CTL_DEMAND_COOLING, true, 0, 400.0, &commands);
    check_one(__LINE__, &commands, NULL);
    CHECK_UINT((uint32_t)router.status, (uint32_t)CTL_ROUTER_IDLE);
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, true, 0, 500.0, &commands);
    check_one(__LINE__, &commands, "1");
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 1, 600.0, &commands);
    check_one(__LINE__, &commands, NULL);
    ctl_router_step(&router, &config, CTL_DEMAND_HEATING, true, 1, 800.0, &commands);
    check_one(__LINE__, &commands, "0");
}

static void test_dispatch(void)
{
    CHECK_UINT((uint32_t)ctl_dispatch_smart_grid_mode(0x40), 0u);
    CHECK_UINT((uint32_t)ctl_dispatch_smart_grid_mode(0x50), 1u);
    CHECK_UINT((uint32_t)ctl_dispatch_smart_grid_mode(0x60), 2u);
    CHECK_UINT((uint32_t)ctl_dispatch_smart_grid_mode(0x70), 3u);
    CHECK_UINT((uint32_t)ctl_dispatch_smart_grid_mode(0x4C), 0u);
    /* The thermostat bits of the same byte stay apart. */
    CHECK_UINT((uint32_t)ctl_dispatch_thermostat_state(0x4C), 3u);
}

int main(void)
{
    test_config();
    test_send_and_land();
    test_retries_to_fault();
    test_changes();
    test_dispatch();
    return check_result();
}
