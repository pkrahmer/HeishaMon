#include "check.h"
#include "core/ctl_config.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* One field of type double, changed by its offset so that one table covers them all. */
typedef struct {
    size_t offset;
    double value;
    uint32_t expected;
} field_row;

#define FIELD(name) offsetof(ctl_config, name)

static void set_field(ctl_config *config, size_t offset, double value)
{
    *(double *)((char *)config + offset) = value;
}

static uint32_t validate_with_field(size_t offset, double value)
{
    ctl_config config;
    ctl_config_defaults(&config);
    set_field(&config, offset, value);
    return ctl_config_validate(&config);
}

static double below(double limit)
{
    return nextafter(limit, -INFINITY);
}

static double above(double limit)
{
    return nextafter(limit, INFINITY);
}

static void test_defaults(void)
{
    ctl_config config;
    ctl_config_defaults(&config);
    CHECK_DOUBLE(config.desired_indoor_c, 22.0);
    CHECK_DOUBLE(config.transmission_w_per_k, 155.0);
    CHECK_DOUBLE(config.adjustment_w_per_k, 805.0);
    CHECK_DOUBLE(config.hysteresis_wh, 1500.0);
    CHECK_TRUE(config.trim_limit_enabled);
    CHECK_DOUBLE(config.trim_limit_k, 3.0);
    CHECK_DOUBLE(config.solar_gain_factor, 0.02);
    CHECK_DOUBLE(config.energy_clamp_factor, 2.0);
    CHECK_DOUBLE(config.max_step_s, 300.0);
    CHECK_DOUBLE(config.tick_interval_s, 60.0);
    CHECK_DOUBLE(config.integration_interval_s, 10.0);
    CHECK_DOUBLE(config.input_hold_s, 900.0);
    CHECK_DOUBLE(config.max_age_s[CTL_SIGNAL_T_INDOOR], 600.0);
    CHECK_DOUBLE(config.max_age_s[CTL_SIGNAL_T_OUTDOOR], 600.0);
    CHECK_DOUBLE(config.max_age_s[CTL_SIGNAL_P_PRODUCED], 600.0);
    CHECK_DOUBLE(config.max_age_s[CTL_SIGNAL_P_SOLAR], 600.0);
    CHECK_DOUBLE(config.max_age_s[CTL_SIGNAL_P_INTERNAL], INFINITY);
    CHECK_DOUBLE(config.fallback_w[CTL_SIGNAL_T_INDOOR], 0.0);
    CHECK_DOUBLE(config.fallback_w[CTL_SIGNAL_T_OUTDOOR], 0.0);
    CHECK_DOUBLE(config.fallback_w[CTL_SIGNAL_P_PRODUCED], 0.0);
    CHECK_DOUBLE(config.fallback_w[CTL_SIGNAL_P_SOLAR], 0.0);
    CHECK_DOUBLE(config.fallback_w[CTL_SIGNAL_P_INTERNAL], 550.0);
    CHECK_UINT(ctl_config_validate(&config), 0u);
}

/* Both sides of every limit that one field can cross alone, from the defaults. */
static void test_range_limits(void)
{
    const double small = 1e-9;
    const field_row rows[] = {
        {FIELD(desired_indoor_c), 5.0, 0u},
        {FIELD(desired_indoor_c), 30.0, 0u},
        {FIELD(desired_indoor_c), below(5.0), CTL_CONFIG_BAD_DESIRED_INDOOR},
        {FIELD(desired_indoor_c), above(30.0), CTL_CONFIG_BAD_DESIRED_INDOOR},
        {FIELD(transmission_w_per_k), small, 0u},
        {FIELD(transmission_w_per_k), 0.0, CTL_CONFIG_BAD_TRANSMISSION},
        {FIELD(adjustment_w_per_k), 0.0, 0u},
        {FIELD(adjustment_w_per_k), -small, CTL_CONFIG_BAD_ADJUSTMENT},
        {FIELD(hysteresis_wh), small, 0u},
        {FIELD(hysteresis_wh), 0.0, CTL_CONFIG_BAD_HYSTERESIS},
        {FIELD(trim_limit_k), small, 0u},
        {FIELD(trim_limit_k), 0.0, CTL_CONFIG_BAD_TRIM_LIMIT},
        {FIELD(solar_gain_factor), 0.0, 0u},
        {FIELD(solar_gain_factor), -small, CTL_CONFIG_BAD_SOLAR_GAIN},
        {FIELD(energy_clamp_factor), 1.0, 0u},
        {FIELD(energy_clamp_factor), below(1.0), CTL_CONFIG_BAD_ENERGY_CLAMP},
        {FIELD(tick_interval_s), 300.0 / 2.0, 0u},
        {FIELD(tick_interval_s), above(300.0 / 2.0), CTL_CONFIG_BAD_TICK_INTERVAL},
        {FIELD(integration_interval_s), small, 0u},
        {FIELD(integration_interval_s), 0.0, CTL_CONFIG_BAD_INTEGRATION_INTERVAL},
        {FIELD(integration_interval_s), 60.0, 0u},
        {FIELD(integration_interval_s), above(60.0), CTL_CONFIG_BAD_INTEGRATION_INTERVAL},
        {FIELD(input_hold_s), 0.0, 0u},
        {FIELD(input_hold_s), -small, CTL_CONFIG_BAD_INPUT_HOLD},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_UINT(validate_with_field(rows[i].offset, rows[i].value), rows[i].expected);
    }
}

/* max_step_s bounds tick_interval_s, which bounds integration_interval_s: a small positive
 * value of one is valid only with the fields it bounds brought under it, and a zero leaves
 * no valid value for the field it bounds, so that field's bit comes with it. */
typedef struct {
    double max_step_s;
    double tick_interval_s;
    double integration_interval_s;
    uint32_t expected;
} timing_row;

static void test_timing_limits(void)
{
    const double small = 1e-9;
    const timing_row rows[] = {
        {small, small / 2.0, small / 2.0, 0u},
        {0.0, 60.0, 10.0, CTL_CONFIG_BAD_MAX_STEP | CTL_CONFIG_BAD_TICK_INTERVAL},
        {300.0, small, small, 0u},
        {300.0, 0.0, 10.0, CTL_CONFIG_BAD_TICK_INTERVAL | CTL_CONFIG_BAD_INTEGRATION_INTERVAL},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_config config;
        ctl_config_defaults(&config);
        config.max_step_s = rows[i].max_step_s;
        config.tick_interval_s = rows[i].tick_interval_s;
        config.integration_interval_s = rows[i].integration_interval_s;
        CHECK_UINT(ctl_config_validate(&config), rows[i].expected);
    }
}

static void test_not_finite_fields(void)
{
    const struct {
        size_t offset;
        uint32_t bit;
    } fields[] = {
        {FIELD(desired_indoor_c), CTL_CONFIG_BAD_DESIRED_INDOOR},
        {FIELD(transmission_w_per_k), CTL_CONFIG_BAD_TRANSMISSION},
        {FIELD(adjustment_w_per_k), CTL_CONFIG_BAD_ADJUSTMENT},
        {FIELD(hysteresis_wh), CTL_CONFIG_BAD_HYSTERESIS},
        {FIELD(trim_limit_k), CTL_CONFIG_BAD_TRIM_LIMIT},
        {FIELD(solar_gain_factor), CTL_CONFIG_BAD_SOLAR_GAIN},
        {FIELD(energy_clamp_factor), CTL_CONFIG_BAD_ENERGY_CLAMP},
        {FIELD(max_step_s), CTL_CONFIG_BAD_MAX_STEP},
        {FIELD(tick_interval_s), CTL_CONFIG_BAD_TICK_INTERVAL},
        {FIELD(integration_interval_s), CTL_CONFIG_BAD_INTEGRATION_INTERVAL},
        {FIELD(input_hold_s), CTL_CONFIG_BAD_INPUT_HOLD},
    };
    size_t i;
    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        CHECK_UINT(validate_with_field(fields[i].offset, NAN), fields[i].bit);
        CHECK_UINT(validate_with_field(fields[i].offset, INFINITY), fields[i].bit);
    }
}

static uint32_t validate_with_max_age(ctl_signal signal, double value)
{
    ctl_config config;
    ctl_config_defaults(&config);
    config.max_age_s[signal] = value;
    return ctl_config_validate(&config);
}

static uint32_t validate_with_fallback(ctl_signal signal, double value)
{
    ctl_config config;
    ctl_config_defaults(&config);
    config.fallback_w[signal] = value;
    return ctl_config_validate(&config);
}

static void test_signal_arrays(void)
{
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        const ctl_signal signal = (ctl_signal)index;
        const uint32_t infinity_age =
            ctl_signal_is_required(signal) ? (uint32_t)CTL_CONFIG_BAD_MAX_AGE : 0u;
        CHECK_UINT(validate_with_max_age(signal, NAN), CTL_CONFIG_BAD_MAX_AGE);
        CHECK_UINT(validate_with_max_age(signal, INFINITY), infinity_age);
        CHECK_UINT(validate_with_max_age(signal, 0.0), CTL_CONFIG_BAD_MAX_AGE);
        CHECK_UINT(validate_with_max_age(signal, 1e-9), 0u);
        CHECK_UINT(validate_with_fallback(signal, NAN), CTL_CONFIG_BAD_FALLBACK);
        CHECK_UINT(validate_with_fallback(signal, INFINITY), CTL_CONFIG_BAD_FALLBACK);
        CHECK_UINT(validate_with_fallback(signal, -INFINITY), CTL_CONFIG_BAD_FALLBACK);
        CHECK_UINT(validate_with_fallback(signal, -1000.0), 0u);
    }
}

static void test_trim_limit_switch(void)
{
    const double values[] = {0.0, -1.0, NAN};
    size_t i;
    for (i = 0; i < sizeof values / sizeof values[0]; i++) {
        ctl_config config;
        ctl_config_defaults(&config);
        config.trim_limit_k = values[i];
        config.trim_limit_enabled = false;
        CHECK_UINT(ctl_config_validate(&config), 0u);
        config.trim_limit_enabled = true;
        CHECK_UINT(ctl_config_validate(&config), CTL_CONFIG_BAD_TRIM_LIMIT);
    }
}

static void test_problems_add_up(void)
{
    ctl_config config;
    ctl_config_defaults(&config);
    config.hysteresis_wh = 0.0;
    config.max_step_s = -1.0;
    config.fallback_w[CTL_SIGNAL_P_SOLAR] = NAN;
    CHECK_UINT(ctl_config_validate(&config),
               CTL_CONFIG_BAD_HYSTERESIS | CTL_CONFIG_BAD_MAX_STEP | CTL_CONFIG_BAD_FALLBACK |
                   CTL_CONFIG_BAD_TICK_INTERVAL);
}

static void test_energy_limit(void)
{
    ctl_config config;
    ctl_config_defaults(&config);
    CHECK_DOUBLE(ctl_config_energy_limit_wh(&config), 3000.0);
    config.energy_clamp_factor = 2.5;
    CHECK_DOUBLE(ctl_config_energy_limit_wh(&config), 3750.0);
}

int main(void)
{
    test_defaults();
    test_range_limits();
    test_timing_limits();
    test_not_finite_fields();
    test_signal_arrays();
    test_trim_limit_switch();
    test_problems_add_up();
    test_energy_limit();
    return check_result();
}
