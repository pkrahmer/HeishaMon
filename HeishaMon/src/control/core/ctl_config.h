/*
 * The configuration every module of the control core takes as an argument: its defaults,
 * which are the legacy system's field-tuned values, and its validation, which the board runs
 * before it applies a configuration so that a bad one is refused rather than acted on.
 */
#ifndef CTL_CONFIG_H
#define CTL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "ctl_types.h"

typedef struct {
    double desired_indoor_c;
    double transmission_w_per_k;
    double adjustment_w_per_k;
    double hysteresis_wh;
    bool trim_limit_enabled;
    double trim_limit_k;
    double integral_w_per_kh; /* 0 freezes the integral term */
    double integral_limit_w;
    double solar_gain_factor;
    double energy_clamp_factor;
    double max_step_s;
    double tick_interval_s;
    double integration_interval_s;
    double input_hold_s;
    double max_age_s[CTL_SIGNAL_COUNT];
    double fallback_w[CTL_SIGNAL_COUNT];
} ctl_config;

/* One bit per field, so that one call reports every bad field at once. */
typedef enum {
    CTL_CONFIG_BAD_DESIRED_INDOOR = 1u << 0,
    CTL_CONFIG_BAD_TRANSMISSION = 1u << 1,
    CTL_CONFIG_BAD_ADJUSTMENT = 1u << 2,
    CTL_CONFIG_BAD_HYSTERESIS = 1u << 3,
    CTL_CONFIG_BAD_TRIM_LIMIT = 1u << 4,
    CTL_CONFIG_BAD_SOLAR_GAIN = 1u << 5,
    CTL_CONFIG_BAD_ENERGY_CLAMP = 1u << 6,
    CTL_CONFIG_BAD_MAX_STEP = 1u << 7,
    CTL_CONFIG_BAD_TICK_INTERVAL = 1u << 8,
    CTL_CONFIG_BAD_INTEGRATION_INTERVAL = 1u << 9,
    CTL_CONFIG_BAD_INPUT_HOLD = 1u << 10,
    CTL_CONFIG_BAD_MAX_AGE = 1u << 11,
    CTL_CONFIG_BAD_FALLBACK = 1u << 12,
    CTL_CONFIG_BAD_INTEGRAL_GAIN = 1u << 13,
    CTL_CONFIG_BAD_INTEGRAL_LIMIT = 1u << 14
} ctl_config_problem;

/* Sets every field to its default. The result is valid. Refuses nothing. */
void ctl_config_defaults(ctl_config *config);

/* Returns 0 when the configuration is valid, and otherwise the ctl_config_problem bits of
 * every field that breaks its rule. A NaN or infinite field is refused, except INFINITY in
 * max_age_s of an optional signal, which means that the signal never goes stale. */
uint32_t ctl_config_validate(const ctl_config *config);

/* Returns the anti-windup bound on the energy accumulator, in Wh. Refuses nothing: on a
 * configuration that is not valid the result means nothing. */
double ctl_config_energy_limit_wh(const ctl_config *config);

#endif
