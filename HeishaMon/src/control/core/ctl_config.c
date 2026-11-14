#include "ctl_config.h"

#include <math.h>

void ctl_config_defaults(ctl_config *config)
{
    int index;

    config->desired_indoor_c = 22.0;
    config->transmission_w_per_k = 155.0;
    config->adjustment_w_per_k = 805.0;
    config->hysteresis_wh = 1500.0;
    config->trim_limit_enabled = true;
    config->trim_limit_k = 3.0;
    /* Off, so that the core decides as the Python reference does until the owner opts in. */
    config->integral_w_per_kh = 0.0;
    config->integral_limit_w = 300.0;
    config->solar_gain_factor = 0.02;
    config->energy_clamp_factor = 2.0;
    config->max_step_s = 300.0;
    config->tick_interval_s = 60.0;
    config->integration_interval_s = 10.0;
    config->input_hold_s = 900.0;

    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        config->max_age_s[index] = 600.0;
        config->fallback_w[index] = 0.0;
    }
    /* Internal gains are usually a static estimate, not a measurement that can go stale;
     * 550 W is what the legacy system adds unconditionally. */
    config->max_age_s[CTL_SIGNAL_P_INTERNAL] = INFINITY;
    config->fallback_w[CTL_SIGNAL_P_INTERNAL] = 550.0;
}

/* Written as "finite and in range" so that a NaN, which fails every comparison, is refused. */
static bool is_positive(double value)
{
    return isfinite(value) && value > 0.0;
}

static bool is_at_least(double value, double minimum)
{
    return isfinite(value) && value >= minimum;
}

static bool is_max_age_valid(ctl_signal signal, double age)
{
    if (isnan(age) || age <= 0.0) {
        return false;
    }
    /* A required signal that never goes stale could never be detected as dead. */
    return isfinite(age) || !ctl_signal_is_required(signal);
}

static bool are_max_ages_valid(const ctl_config *config)
{
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        if (!is_max_age_valid((ctl_signal)index, config->max_age_s[index])) {
            return false;
        }
    }
    return true;
}

static bool are_fallbacks_valid(const ctl_config *config)
{
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        if (!isfinite(config->fallback_w[index])) {
            return false;
        }
    }
    return true;
}

/* The bound is compared the way the Python reference compares it, so that a bound that is
 * itself NaN refuses nothing here: only the bound's own field is reported. */
static bool is_tick_interval_valid(const ctl_config *config)
{
    return is_positive(config->tick_interval_s) &&
           !(config->tick_interval_s > config->max_step_s / 2.0);
}

static bool is_integration_interval_valid(const ctl_config *config)
{
    return is_positive(config->integration_interval_s) &&
           !(config->integration_interval_s > config->tick_interval_s);
}

static bool is_trim_limit_valid(const ctl_config *config)
{
    return !config->trim_limit_enabled || is_positive(config->trim_limit_k);
}

static bool is_integral_gain_valid(double gain)
{
    return isfinite(gain) && gain >= 0.0 && gain <= 200.0;
}

static bool is_integral_limit_valid(double limit)
{
    return isfinite(limit) && limit > 0.0 && limit <= 2000.0;
}

static uint32_t problem_if(bool valid, ctl_config_problem problem)
{
    return valid ? 0u : (uint32_t)problem;
}

uint32_t ctl_config_validate(const ctl_config *config)
{
    const double indoor = config->desired_indoor_c;
    uint32_t problems = 0u;

    problems |= problem_if(isfinite(indoor) && indoor >= 5.0 && indoor <= 30.0,
                           CTL_CONFIG_BAD_DESIRED_INDOOR);
    problems |= problem_if(is_positive(config->transmission_w_per_k), CTL_CONFIG_BAD_TRANSMISSION);
    problems |= problem_if(is_at_least(config->adjustment_w_per_k, 0.0), CTL_CONFIG_BAD_ADJUSTMENT);
    problems |= problem_if(is_positive(config->hysteresis_wh), CTL_CONFIG_BAD_HYSTERESIS);
    problems |= problem_if(is_trim_limit_valid(config), CTL_CONFIG_BAD_TRIM_LIMIT);
    problems |=
        problem_if(is_integral_gain_valid(config->integral_w_per_kh), CTL_CONFIG_BAD_INTEGRAL_GAIN);
    problems |= problem_if(is_integral_limit_valid(config->integral_limit_w),
                           CTL_CONFIG_BAD_INTEGRAL_LIMIT);
    problems |= problem_if(is_at_least(config->solar_gain_factor, 0.0), CTL_CONFIG_BAD_SOLAR_GAIN);
    problems |=
        problem_if(is_at_least(config->energy_clamp_factor, 1.0), CTL_CONFIG_BAD_ENERGY_CLAMP);
    problems |= problem_if(is_positive(config->max_step_s), CTL_CONFIG_BAD_MAX_STEP);
    problems |= problem_if(is_tick_interval_valid(config), CTL_CONFIG_BAD_TICK_INTERVAL);
    problems |=
        problem_if(is_integration_interval_valid(config), CTL_CONFIG_BAD_INTEGRATION_INTERVAL);
    problems |= problem_if(is_at_least(config->input_hold_s, 0.0), CTL_CONFIG_BAD_INPUT_HOLD);
    problems |= problem_if(are_max_ages_valid(config), CTL_CONFIG_BAD_MAX_AGE);
    problems |= problem_if(are_fallbacks_valid(config), CTL_CONFIG_BAD_FALLBACK);
    return problems;
}

double ctl_config_energy_limit_wh(const ctl_config *config)
{
    return config->energy_clamp_factor * config->hysteresis_wh;
}
