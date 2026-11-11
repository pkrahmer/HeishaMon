#include "check.h"
#include "core/ctl_config.h"
#include "core/ctl_signals.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define REQUIRED_BITS                                                                      \
    ((uint32_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR) | CTL_SIGNAL_BIT(CTL_SIGNAL_T_OUTDOOR) | \
                CTL_SIGNAL_BIT(CTL_SIGNAL_P_PRODUCED)))
#define OPTIONAL_BITS \
    ((uint32_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_P_SOLAR) | CTL_SIGNAL_BIT(CTL_SIGNAL_P_INTERNAL)))

static void set_required(ctl_signal_table *table, double t)
{
    ctl_signals_set(table, CTL_SIGNAL_T_INDOOR, 21.0, t);
    ctl_signals_set(table, CTL_SIGNAL_T_OUTDOOR, 5.0, t);
    ctl_signals_set(table, CTL_SIGNAL_P_PRODUCED, 3000.0, t);
}

static void test_empty_table(void)
{
    ctl_signal_table table;
    ctl_config config;
    int index;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    CHECK_UINT(ctl_signals_observed(&table), 0u);
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        const ctl_signal signal = (ctl_signal)index;
        CHECK_DOUBLE(ctl_signals_age(&table, signal, 100.0), INFINITY);
        CHECK_TRUE(!ctl_signals_is_fresh(&table, signal, 100.0, &config));
    }
}

static void test_set_and_age(void)
{
    ctl_signal_table table;
    ctl_config config;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    ctl_signals_set(&table, CTL_SIGNAL_T_INDOOR, 21.5, 100.0);
    CHECK_DOUBLE(ctl_signals_age(&table, CTL_SIGNAL_T_INDOOR, 160.0), 60.0);
    CHECK_DOUBLE(ctl_signals_age(&table, CTL_SIGNAL_T_INDOOR, 40.0), -60.0);
    CHECK_UINT(ctl_signals_observed(&table), CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR));
    CHECK_DOUBLE(table.value[CTL_SIGNAL_T_INDOOR], 21.5);
    CHECK_DOUBLE(table.t[CTL_SIGNAL_T_INDOOR], 100.0);

    /* An older instant still replaces the reading: the newest call wins. */
    ctl_signals_set(&table, CTL_SIGNAL_T_INDOOR, 20.0, 50.0);
    CHECK_DOUBLE(table.value[CTL_SIGNAL_T_INDOOR], 20.0);
    CHECK_DOUBLE(table.t[CTL_SIGNAL_T_INDOOR], 50.0);
    CHECK_DOUBLE(ctl_signals_age(&table, CTL_SIGNAL_T_INDOOR, 160.0), 110.0);
    CHECK_UINT(ctl_signals_observed(&table), CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR));
}

typedef struct {
    ctl_signal signal;
    double at;
    bool fresh;
} fresh_row;

static void test_freshness_at_limit(void)
{
    const fresh_row rows[] = {
        {CTL_SIGNAL_T_INDOOR, 700.0, true},
        {CTL_SIGNAL_T_INDOOR, 700.001, false},
        {CTL_SIGNAL_T_INDOOR, 40.0, true},
        {CTL_SIGNAL_T_INDOOR, NAN, false},
        {CTL_SIGNAL_P_INTERNAL, 1e12, true},
    };
    ctl_signal_table table;
    ctl_config config;
    size_t i;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    ctl_signals_set(&table, CTL_SIGNAL_T_INDOOR, 21.0, 100.0);
    ctl_signals_set(&table, CTL_SIGNAL_P_INTERNAL, 400.0, 100.0);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_TRUE(ctl_signals_is_fresh(&table, rows[i].signal, rows[i].at, &config) ==
                   rows[i].fresh);
    }
}

static void test_unhealthy_and_stale_optional(void)
{
    ctl_signal_table table;
    ctl_config config;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    ctl_signals_set(&table, CTL_SIGNAL_T_INDOOR, 21.0, 100.0);
    ctl_signals_set(&table, CTL_SIGNAL_P_SOLAR, 120.0, 100.0);

    CHECK_UINT(ctl_signals_unhealthy(&table, 200.0, &config),
               (uint32_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_T_OUTDOOR) |
                          CTL_SIGNAL_BIT(CTL_SIGNAL_P_PRODUCED)));
    CHECK_UINT(ctl_signals_stale_optional(&table, 200.0, &config),
               CTL_SIGNAL_BIT(CTL_SIGNAL_P_INTERNAL));
    CHECK_UINT(ctl_signals_unhealthy(&table, 800.0, &config), REQUIRED_BITS);
    CHECK_UINT(ctl_signals_stale_optional(&table, 800.0, &config), OPTIONAL_BITS);
    /* Observed counts readings, fresh or not. */
    CHECK_UINT(ctl_signals_observed(&table),
               (uint32_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR) | CTL_SIGNAL_BIT(CTL_SIGNAL_P_SOLAR)));
}

static void check_inputs(const ctl_inputs *inputs, double solar, double internal,
                         uint32_t substituted)
{
    CHECK_DOUBLE(inputs->t_indoor_c, 21.0);
    CHECK_DOUBLE(inputs->t_outdoor_c, 5.0);
    CHECK_DOUBLE(inputs->p_produced_w, 3000.0);
    CHECK_DOUBLE(inputs->p_solar_w, solar);
    CHECK_DOUBLE(inputs->p_internal_w, internal);
    CHECK_UINT(inputs->substituted, substituted);
}

static void test_resolve_with_fallbacks(void)
{
    ctl_signal_table table;
    ctl_config config;
    ctl_inputs inputs;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    set_required(&table, 100.0);
    CHECK_TRUE(ctl_signals_resolve(&table, 200.0, &config, &inputs));
    check_inputs(&inputs, 0.0, 550.0, OPTIONAL_BITS);
}

static void test_resolve_with_every_signal(void)
{
    ctl_signal_table table;
    ctl_config config;
    ctl_inputs inputs;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    set_required(&table, 100.0);
    ctl_signals_set(&table, CTL_SIGNAL_P_SOLAR, 120.0, 100.0);
    ctl_signals_set(&table, CTL_SIGNAL_P_INTERNAL, 400.0, 100.0);
    CHECK_TRUE(ctl_signals_resolve(&table, 200.0, &config, &inputs));
    check_inputs(&inputs, 120.0, 400.0, 0u);
}

static void test_resolve_with_stale_solar(void)
{
    ctl_signal_table table;
    ctl_config config;
    ctl_inputs inputs;

    ctl_config_defaults(&config);
    ctl_signals_init(&table);
    ctl_signals_set(&table, CTL_SIGNAL_P_SOLAR, 120.0, 100.0);
    set_required(&table, 750.0);
    CHECK_TRUE(ctl_signals_resolve(&table, 800.0, &config, &inputs));
    check_inputs(&inputs, 0.0, 550.0, OPTIONAL_BITS);
}

static void fill_with_marker(ctl_inputs *inputs)
{
    inputs->t_indoor_c = -777.0;
    inputs->t_outdoor_c = -777.0;
    inputs->p_produced_w = -777.0;
    inputs->p_solar_w = -777.0;
    inputs->p_internal_w = -777.0;
    inputs->substituted = 0xA5u;
}

static void check_marker(const ctl_inputs *inputs)
{
    CHECK_DOUBLE(inputs->t_indoor_c, -777.0);
    CHECK_DOUBLE(inputs->t_outdoor_c, -777.0);
    CHECK_DOUBLE(inputs->p_produced_w, -777.0);
    CHECK_DOUBLE(inputs->p_solar_w, -777.0);
    CHECK_DOUBLE(inputs->p_internal_w, -777.0);
    CHECK_UINT(inputs->substituted, 0xA5u);
}

static void check_refused(const ctl_signal_table *table, double at)
{
    ctl_config config;
    ctl_inputs inputs;

    ctl_config_defaults(&config);
    fill_with_marker(&inputs);
    CHECK_TRUE(!ctl_signals_resolve(table, at, &config, &inputs));
    check_marker(&inputs);
}

static void test_resolve_refuses(void)
{
    ctl_signal_table missing;
    ctl_signal_table stale;
    ctl_signal_table complete;

    ctl_signals_init(&missing);
    ctl_signals_set(&missing, CTL_SIGNAL_T_INDOOR, 21.0, 100.0);
    ctl_signals_set(&missing, CTL_SIGNAL_T_OUTDOOR, 5.0, 100.0);
    check_refused(&missing, 200.0);

    ctl_signals_init(&stale);
    set_required(&stale, 100.0);
    ctl_signals_set(&stale, CTL_SIGNAL_T_OUTDOOR, 5.0, -500.0);
    check_refused(&stale, 200.0);

    ctl_signals_init(&complete);
    set_required(&complete, 100.0);
    check_refused(&complete, NAN);
}

int main(void)
{
    test_empty_table();
    test_set_and_age();
    test_freshness_at_limit();
    test_unhealthy_and_stale_optional();
    test_resolve_with_fallbacks();
    test_resolve_with_every_signal();
    test_resolve_with_stale_solar();
    test_resolve_refuses();
    return check_result();
}
