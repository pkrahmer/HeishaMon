#include "ctl_signals.h"

#include <math.h>

void ctl_signals_init(ctl_signal_table *table)
{
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        table->observed[index] = false;
        table->value[index] = 0.0;
        table->t[index] = 0.0;
    }
}

void ctl_signals_set(ctl_signal_table *table, ctl_signal signal, double value, double t)
{
    table->observed[signal] = true;
    table->value[signal] = value;
    table->t[signal] = t;
}

double ctl_signals_age(const ctl_signal_table *table, ctl_signal signal, double at)
{
    if (!table->observed[signal]) {
        return INFINITY;
    }
    return at - table->t[signal];
}

bool ctl_signals_is_fresh(const ctl_signal_table *table, ctl_signal signal, double at,
                          const ctl_config *config)
{
    if (!table->observed[signal]) {
        return false;
    }
    return (at - table->t[signal]) <= config->max_age_s[signal];
}

/* The bits of the signals of one kind, required or optional, that are not fresh. Written as
 * "not fresh" rather than "age > max_age" so that a NaN instant counts as stale. */
static uint8_t not_fresh(const ctl_signal_table *table, double at, const ctl_config *config,
                         bool required)
{
    uint8_t bits = 0u;
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        const ctl_signal signal = (ctl_signal)index;
        if (ctl_signal_is_required(signal) == required &&
            !ctl_signals_is_fresh(table, signal, at, config)) {
            bits = (uint8_t)(bits | CTL_SIGNAL_BIT(signal));
        }
    }
    return bits;
}

uint8_t ctl_signals_unhealthy(const ctl_signal_table *table, double at, const ctl_config *config)
{
    return not_fresh(table, at, config, true);
}

uint8_t ctl_signals_stale_optional(const ctl_signal_table *table, double at,
                                   const ctl_config *config)
{
    return not_fresh(table, at, config, false);
}

uint8_t ctl_signals_observed(const ctl_signal_table *table)
{
    uint8_t bits = 0u;
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        if (table->observed[index]) {
            bits = (uint8_t)(bits | CTL_SIGNAL_BIT(index));
        }
    }
    return bits;
}

static double optional_value(const ctl_signal_table *table, ctl_signal signal, uint8_t stale,
                             const ctl_config *config)
{
    if (stale & CTL_SIGNAL_BIT(signal)) {
        return config->fallback_w[signal];
    }
    return table->value[signal];
}

bool ctl_signals_resolve(const ctl_signal_table *table, double at, const ctl_config *config,
                         ctl_inputs *inputs)
{
    uint8_t stale;

    if (ctl_signals_unhealthy(table, at, config) != 0u) {
        return false;
    }
    stale = ctl_signals_stale_optional(table, at, config);
    inputs->t_indoor_c = table->value[CTL_SIGNAL_T_INDOOR];
    inputs->t_outdoor_c = table->value[CTL_SIGNAL_T_OUTDOOR];
    inputs->p_produced_w = table->value[CTL_SIGNAL_P_PRODUCED];
    inputs->p_solar_w = optional_value(table, CTL_SIGNAL_P_SOLAR, stale, config);
    inputs->p_internal_w = optional_value(table, CTL_SIGNAL_P_INTERNAL, stale, config);
    inputs->substituted = stale;
    return true;
}
