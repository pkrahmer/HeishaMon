#include "check.h"

#include "core/ctl_types.h"
#include "route/ctl_router.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THERMOSTAT "SetExternalThermostat1State"
#define HEAT_COOL "SetHeatCoolMode"

/* The commands a step should give: up to two, in the order they are written; a NULL name ends
 * the list early. */
typedef struct {
    const char *name[CTL_ROUTER_COMMANDS_MAX];
    const char *value[CTL_ROUTER_COMMANDS_MAX];
} expected_commands;

static const expected_commands NOTHING = {{NULL, NULL}, {NULL, NULL}};

static size_t expected_count(const expected_commands *expected)
{
    size_t count = 0;
    while (count < CTL_ROUTER_COMMANDS_MAX && expected->name[count] != NULL) {
        count++;
    }
    return count;
}

static void check_commands(const char *file, int line, const ctl_commands *actual,
                           const expected_commands *expected)
{
    const size_t count = expected_count(expected);
    size_t i;
    check_uint(file, line, (uint32_t)actual->count, (uint32_t)count);
    for (i = 0; i < count && i < actual->count; i++) {
        check_text(file, line, actual->command[i].name, expected->name[i]);
        check_text(file, line, actual->command[i].value, expected->value[i]);
    }
}

#define CHECK_COMMANDS(actual, expected) check_commands(__FILE__, __LINE__, (actual), (expected))

/* Compares every field, so that a change no check names is caught too. */
static void check_router_equal(const char *file, int line, const ctl_router *actual,
                               const ctl_router *expected)
{
    check_uint(file, line, (uint32_t)actual->wanted_state, (uint32_t)expected->wanted_state);
    check_uint(file, line, (uint32_t)actual->sent_state, (uint32_t)expected->sent_state);
    check_true(file, line, actual->has_sent == expected->has_sent, "has_sent");
    check_double(file, line, actual->sent_at, expected->sent_at);
    check_double(file, line, actual->changed_at, expected->changed_at);
    check_uint(file, line, (uint32_t)actual->retries, (uint32_t)expected->retries);
    check_uint(file, line, (uint32_t)actual->status, (uint32_t)expected->status);
}

/* A step with the state known and the pump reporting `reported`. */
static void step_reported(ctl_router *router, const ctl_router_config *config, ctl_demand demand,
                          int reported, double now, ctl_commands *commands)
{
    ctl_router_step(router, config, demand, true, reported, now, commands);
}

/* A fresh router that has sent 0 at 0.0 and seen it land at 10.0. */
static void landed_zero(ctl_router *router, const ctl_router_config *config)
{
    ctl_commands commands;
    ctl_router_init(router);
    step_reported(router, config, CTL_DEMAND_NONE, 0, 0.0, &commands);
    step_reported(router, config, CTL_DEMAND_NONE, 0, 10.0, &commands);
}

typedef struct {
    ctl_router_config config;
    bool valid;
} config_row;

static ctl_router_config config_with(double min_change, double retry, int retries, int heat)
{
    ctl_router_config config;
    config.min_change_interval_s = min_change;
    config.retry_interval_s = retry;
    config.max_retries = retries;
    config.heat_cool_heat_value = heat;
    return config;
}

static void test_config(void)
{
    ctl_router_config defaults;
    config_row rows[14];
    size_t count = 0;
    size_t i;

    ctl_router_config_defaults(&defaults);
    CHECK_DOUBLE(defaults.min_change_interval_s, 300.0);
    CHECK_DOUBLE(defaults.retry_interval_s, 120.0);
    CHECK_UINT((uint32_t)defaults.max_retries, 3u);
    CHECK_TRUE(defaults.heat_cool_heat_value == -1);

    rows[count].config = defaults; rows[count++].valid = true;
    rows[count].config = config_with(-1.0, 120.0, 3, -1); rows[count++].valid = false;
    rows[count].config = config_with(300.0, -1.0, 3, -1); rows[count++].valid = false;
    rows[count].config = config_with(NAN, 120.0, 3, -1); rows[count++].valid = false;
    rows[count].config = config_with(300.0, NAN, 3, -1); rows[count++].valid = false;
    rows[count].config = config_with(INFINITY, 120.0, 3, -1); rows[count++].valid = false;
    rows[count].config = config_with(300.0, 120.0, -1, -1); rows[count++].valid = false;
    rows[count].config = config_with(300.0, 120.0, 11, -1); rows[count++].valid = false;
    rows[count].config = config_with(300.0, 120.0, 3, 2); rows[count++].valid = false;
    rows[count].config = config_with(0.0, 0.0, 3, -1); rows[count++].valid = true;
    rows[count].config = config_with(300.0, 120.0, 0, -1); rows[count++].valid = true;
    rows[count].config = config_with(300.0, 120.0, 10, -1); rows[count++].valid = true;
    rows[count].config = config_with(300.0, 120.0, 3, 0); rows[count++].valid = true;
    rows[count].config = config_with(300.0, 120.0, 3, 1); rows[count++].valid = true;

    for (i = 0; i < count; i++) {
        const bool valid = ctl_router_config_valid(&rows[i].config);
        check_true(__FILE__, __LINE__, valid == rows[i].valid, "valid == rows[i].valid");
    }
}

typedef struct {
    ctl_demand demand;
    int state;
} state_row;

static void test_state_for(void)
{
    const state_row rows[] = {
        {CTL_DEMAND_HEATING, 2},
        {CTL_DEMAND_COOLING, 1},
        {CTL_DEMAND_NONE, 0},
        {(ctl_demand)7, 0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_UINT((uint32_t)ctl_router_state_for(rows[i].demand), (uint32_t)rows[i].state);
    }
}

static void test_init(void)
{
    ctl_router router;
    ctl_router_init(&router);
    CHECK_UINT((uint32_t)router.wanted_state, 0u);
    CHECK_TRUE(router.sent_state == -1);
    CHECK_TRUE(!router.has_sent);
    CHECK_UINT((uint32_t)router.retries, 0u);
    CHECK_TRUE(router.status == CTL_ROUTER_IDLE);
}

static void test_first_step_and_landing(void)
{
    const expected_commands zero = {{THERMOSTAT, NULL}, {"0", NULL}};
    ctl_router_config config;
    ctl_router router;
    ctl_commands commands;

    ctl_router_config_defaults(&config);
    ctl_router_init(&router);
    ctl_router_step(&router, &config, CTL_DEMAND_NONE, false, 0, 0.0, &commands);
    CHECK_COMMANDS(&commands, &zero);
    CHECK_TRUE(router.status == CTL_ROUTER_WAITING);

    step_reported(&router, &config, CTL_DEMAND_NONE, 0, 10.0, &commands);
    CHECK_COMMANDS(&commands, &NOTHING);
    CHECK_TRUE(router.status == CTL_ROUTER_IDLE);

    step_reported(&router, &config, CTL_DEMAND_NONE, 0, 20.0, &commands);
    CHECK_COMMANDS(&commands, &NOTHING);
    CHECK_TRUE(router.status == CTL_ROUTER_IDLE);
}

typedef struct {
    ctl_demand first;
    ctl_demand second; /* stepped at 800.0 when it is not the first */
    int heat_value;
    expected_commands commands; /* of the last step */
} heat_cool_row;

static void test_heat_cool(void)
{
    const heat_cool_row rows[] = {
        {CTL_DEMAND_HEATING, CTL_DEMAND_HEATING, -1, {{THERMOSTAT, NULL}, {"2", NULL}}},
        {CTL_DEMAND_HEATING, CTL_DEMAND_HEATING, 1, {{THERMOSTAT, HEAT_COOL}, {"2", "1"}}},
        {CTL_DEMAND_HEATING, CTL_DEMAND_HEATING, 0, {{THERMOSTAT, HEAT_COOL}, {"2", "0"}}},
        {CTL_DEMAND_COOLING, CTL_DEMAND_COOLING, 1, {{THERMOSTAT, HEAT_COOL}, {"1", "0"}}},
        {CTL_DEMAND_COOLING, CTL_DEMAND_COOLING, 0, {{THERMOSTAT, HEAT_COOL}, {"1", "1"}}},
        {CTL_DEMAND_HEATING, CTL_DEMAND_NONE, 1, {{THERMOSTAT, NULL}, {"0", NULL}}},
        {CTL_DEMAND_HEATING, CTL_DEMAND_NONE, 0, {{THERMOSTAT, NULL}, {"0", NULL}}},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_router_config config;
        ctl_router router;
        ctl_commands commands;
        ctl_router_config_defaults(&config);
        config.heat_cool_heat_value = rows[i].heat_value;
        ctl_router_init(&router);
        step_reported(&router, &config, CTL_DEMAND_NONE, 0, 0.0, &commands);
        step_reported(&router, &config, rows[i].first, 0, 400.0, &commands);
        if (rows[i].second != rows[i].first) {
            step_reported(&router, &config, rows[i].second, 0, 800.0, &commands);
        }
        CHECK_COMMANDS(&commands, &rows[i].commands);
        CHECK_TRUE(router.status == CTL_ROUTER_WAITING);
    }
}

typedef struct {
    ctl_demand demand;
    double now;
    expected_commands commands;
    ctl_router_status status;
} pacing_step;

typedef struct {
    size_t count;
    pacing_step step[2];
} pacing_row;

static void test_pacing(void)
{
    const pacing_row rows[] = {
        {1, {{CTL_DEMAND_HEATING, 299.0, {{NULL, NULL}, {NULL, NULL}}, CTL_ROUTER_IDLE}}},
        {1, {{CTL_DEMAND_HEATING, 300.0, {{THERMOSTAT, NULL}, {"2", NULL}}, CTL_ROUTER_WAITING}}},
        {2,
         {{CTL_DEMAND_HEATING, 299.0, {{NULL, NULL}, {NULL, NULL}}, CTL_ROUTER_IDLE},
          {CTL_DEMAND_NONE, 350.0, {{NULL, NULL}, {NULL, NULL}}, CTL_ROUTER_IDLE}}},
    };
    size_t i;
    size_t j;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_router_config config;
        ctl_router router;
        ctl_router_config_defaults(&config);
        landed_zero(&router, &config);
        for (j = 0; j < rows[i].count; j++) {
            const pacing_step *step = &rows[i].step[j];
            ctl_commands commands;
            step_reported(&router, &config, step->demand, 0, step->now, &commands);
            CHECK_COMMANDS(&commands, &step->commands);
            CHECK_TRUE(router.status == step->status);
        }
    }
}

typedef struct {
    double now;
    bool sends;
    int retries;
    ctl_router_status status;
} repeat_step;

static const repeat_step REPEATS[] = {
    {119.0, false, 0, CTL_ROUTER_WAITING},
    {119.0, false, 0, CTL_ROUTER_WAITING},
    {120.0, true, 1, CTL_ROUTER_WAITING},
    {239.0, false, 1, CTL_ROUTER_WAITING},
    {240.0, true, 2, CTL_ROUTER_WAITING},
    {360.0, true, 3, CTL_ROUTER_WAITING},
    {480.0, false, 3, CTL_ROUTER_FAULT},
    {600.0, false, 3, CTL_ROUTER_FAULT},
};

/* Sends 2 at 0.0 from a fresh router and walks the repeats to the fault, with the pump never
 * confirming: it reports 0 when the state is known, nothing when it is not. */
static void run_to_fault(ctl_router *router, const ctl_router_config *config, bool state_known)
{
    const expected_commands two = {{THERMOSTAT, NULL}, {"2", NULL}};
    ctl_commands commands;
    size_t i;

    ctl_router_init(router);
    ctl_router_step(router, config, CTL_DEMAND_HEATING, state_known, 0, 0.0, &commands);
    CHECK_COMMANDS(&commands, &two);
    for (i = 0; i < sizeof REPEATS / sizeof REPEATS[0]; i++) {
        ctl_router_step(router, config, CTL_DEMAND_HEATING, state_known, 0, REPEATS[i].now,
                        &commands);
        CHECK_COMMANDS(&commands, REPEATS[i].sends ? &two : &NOTHING);
        CHECK_UINT((uint32_t)router->retries, (uint32_t)REPEATS[i].retries);
        CHECK_TRUE(router->status == REPEATS[i].status);
        CHECK_UINT((uint32_t)router->sent_state, 2u);
        CHECK_DOUBLE(router->changed_at, 0.0);
    }
}

static void test_repeats(void)
{
    ctl_router_config config;
    ctl_router router;
    ctl_router_config_defaults(&config);
    run_to_fault(&router, &config, true);
    CHECK_DOUBLE(router.sent_at, 360.0);
}

static void test_repeats_unknown_confirmation(void)
{
    ctl_router_config config;
    ctl_router router;
    ctl_router_config_defaults(&config);
    run_to_fault(&router, &config, false);
}

typedef struct {
    ctl_demand demand;
    int reported;
    expected_commands commands;
    ctl_router_status status;
} fault_row;

static void test_leaving_fault(void)
{
    const fault_row rows[] = {
        {CTL_DEMAND_HEATING, 2, {{NULL, NULL}, {NULL, NULL}}, CTL_ROUTER_IDLE},
        {CTL_DEMAND_NONE, 0, {{THERMOSTAT, NULL}, {"0", NULL}}, CTL_ROUTER_WAITING},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_router_config config;
        ctl_router router;
        ctl_commands commands;
        ctl_router_config_defaults(&config);
        run_to_fault(&router, &config, true);
        step_reported(&router, &config, rows[i].demand, rows[i].reported, 610.0, &commands);
        CHECK_COMMANDS(&commands, &rows[i].commands);
        CHECK_TRUE(router.status == rows[i].status);
        CHECK_UINT((uint32_t)router.retries, 0u);
    }
}

/* A state that lands while the demand already wants another one leaves the status alone: the
 * router is still waiting to send the change. */
static void test_landing_while_change_pending(void)
{
    ctl_router_config config;
    ctl_router router;
    ctl_commands commands;
    ctl_router_config_defaults(&config);
    ctl_router_init(&router);
    step_reported(&router, &config, CTL_DEMAND_HEATING, 0, 0.0, &commands);
    step_reported(&router, &config, CTL_DEMAND_HEATING, 0, 120.0, &commands);
    CHECK_UINT((uint32_t)router.retries, 1u);
    step_reported(&router, &config, CTL_DEMAND_NONE, 2, 150.0, &commands);
    CHECK_COMMANDS(&commands, &NOTHING);
    CHECK_TRUE(router.status == CTL_ROUTER_WAITING);
    CHECK_UINT((uint32_t)router.retries, 1u);
}

static void test_no_retries_faults_at_once(void)
{
    ctl_router_config config;
    ctl_router router;
    ctl_commands commands;
    ctl_router_config_defaults(&config);
    config.max_retries = 0;
    ctl_router_init(&router);
    step_reported(&router, &config, CTL_DEMAND_HEATING, 0, 0.0, &commands);
    step_reported(&router, &config, CTL_DEMAND_HEATING, 0, 120.0, &commands);
    CHECK_COMMANDS(&commands, &NOTHING);
    CHECK_TRUE(router.status == CTL_ROUTER_FAULT);
}

static void test_not_finite_now(void)
{
    const double instants[] = {NAN, INFINITY, -INFINITY};
    ctl_router_config config;
    size_t i;
    ctl_router_config_defaults(&config);
    for (i = 0; i < sizeof instants / sizeof instants[0]; i++) {
        ctl_router fresh;
        ctl_router waiting;
        ctl_router before;
        ctl_commands commands;

        ctl_router_init(&fresh);
        before = fresh;
        ctl_router_step(&fresh, &config, CTL_DEMAND_HEATING, true, 0, instants[i], &commands);
        CHECK_COMMANDS(&commands, &NOTHING);
        check_router_equal(__FILE__, __LINE__, &fresh, &before);

        ctl_router_init(&waiting);
        step_reported(&waiting, &config, CTL_DEMAND_HEATING, 0, 0.0, &commands);
        before = waiting;
        ctl_router_step(&waiting, &config, CTL_DEMAND_COOLING, true, 2, instants[i], &commands);
        CHECK_COMMANDS(&commands, &NOTHING);
        check_router_equal(__FILE__, __LINE__, &waiting, &before);
    }
}

static void test_invalid_config(void)
{
    ctl_router_config config;
    ctl_router router;
    ctl_router before;
    ctl_commands commands;
    ctl_router_config_defaults(&config);
    config.heat_cool_heat_value = 5;
    ctl_router_init(&router);
    before = router;
    step_reported(&router, &config, CTL_DEMAND_HEATING, 0, 0.0, &commands);
    CHECK_COMMANDS(&commands, &NOTHING);
    check_router_equal(__FILE__, __LINE__, &router, &before);
}

int main(void)
{
    test_config();
    test_state_for();
    test_init();
    test_first_step_and_landing();
    test_heat_cool();
    test_pacing();
    test_repeats();
    test_repeats_unknown_confirmation();
    test_leaving_fault();
    test_landing_while_change_pending();
    test_no_retries_faults_at_once();
    test_not_finite_now();
    test_invalid_config();
    return check_result();
}
