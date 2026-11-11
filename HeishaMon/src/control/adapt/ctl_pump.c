#include "ctl_pump.h"

#include "../core/ctl_types.h"
#include "ctl_bounds.h"
#include "ctl_payload.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

void ctl_pump_init(ctl_pump *pump)
{
    pump->has_heat = false;
    pump->heat_w = 0.0;
    pump->hot_water = false;
    pump->defrost = false;
}

static void write_reading(ctl_reading *reading, ctl_signal signal, double value, double t)
{
    reading->signal = signal;
    reading->value = value;
    reading->t = t;
}

static bool parse_bounded(const char *payload, size_t length, ctl_bound bound, double t,
                          double *value)
{
    ctl_payload parsed = {0.0, false, 0};
    if (!ctl_payload_parse(payload, length, &parsed)) {
        return false;
    }
    if (!ctl_bounds_accept(bound, parsed.value) || !isfinite(t)) {
        return false;
    }
    *value = parsed.value;
    return true;
}

/* HeishaMon publishes its states as whole numbers; anything but 0 or 1 is not a state of a
 * two-state flag, and guessing its meaning could unmask the heat of a hot-water run. */
static bool parse_flag(const char *payload, size_t length, double t, bool *flag)
{
    ctl_payload parsed = {0.0, false, 0};
    if (!ctl_payload_parse(payload, length, &parsed) || !isfinite(t)) {
        return false;
    }
    if (parsed.value != 0.0 && parsed.value != 1.0) {
        return false;
    }
    *flag = parsed.value == 1.0;
    return true;
}

static bool is_masked(const ctl_pump *pump)
{
    return pump->hot_water || pump->defrost;
}

static double masked_heat(const ctl_pump *pump)
{
    return is_masked(pump) ? 0.0 : pump->heat_w;
}

/* Only a change of the mask itself is news to the core: a flag that changes while the other
 * flag still masks leaves the reading at 0.0, so nothing is emitted. */
static bool set_mask_flag(ctl_pump *pump, bool *flag, bool value, double t, ctl_reading *reading)
{
    bool masked_before = is_masked(pump);
    *flag = value;
    if (!pump->has_heat || is_masked(pump) == masked_before) {
        return false;
    }
    write_reading(reading, CTL_SIGNAL_P_PRODUCED, masked_heat(pump), t);
    return true;
}

bool ctl_pump_outdoor(const char *payload, size_t length, double t, ctl_reading *reading)
{
    double value = 0.0;
    if (!parse_bounded(payload, length, CTL_BOUND_TEMPERATURE, t, &value)) {
        return false;
    }
    write_reading(reading, CTL_SIGNAL_T_OUTDOOR, value, t);
    return true;
}

bool ctl_pump_heat(ctl_pump *pump, const char *payload, size_t length, double t,
                   ctl_reading *reading)
{
    double value = 0.0;
    /* CTL_BOUND_HEAT starts at 0 W, so a negative heat is refused rather than clamped: a meter
     * that reports one is broken, and a clamped 0 W would look like a valid measurement. */
    if (!parse_bounded(payload, length, CTL_BOUND_HEAT, t, &value)) {
        return false;
    }
    pump->has_heat = true;
    pump->heat_w = value;
    write_reading(reading, CTL_SIGNAL_P_PRODUCED, masked_heat(pump), t);
    return true;
}

bool ctl_pump_valve(ctl_pump *pump, const char *payload, size_t length, double t,
                    ctl_reading *reading)
{
    bool hot_water = false;
    if (!parse_flag(payload, length, t, &hot_water)) {
        return false;
    }
    return set_mask_flag(pump, &pump->hot_water, hot_water, t, reading);
}

bool ctl_pump_defrost(ctl_pump *pump, const char *payload, size_t length, double t,
                      ctl_reading *reading)
{
    bool defrost = false;
    if (!parse_flag(payload, length, t, &defrost)) {
        return false;
    }
    return set_mask_flag(pump, &pump->defrost, defrost, t, reading);
}

bool ctl_solar(const char *payload, size_t length, double gain, double t, ctl_reading *reading)
{
    double pv_w = 0.0;
    double solar_w = 0.0;
    if (!isfinite(gain) || gain < 0.0) {
        return false;
    }
    /* CTL_BOUND_PV starts at 0 W: a negative PV power is refused, not clamped, as the heat is. */
    if (!parse_bounded(payload, length, CTL_BOUND_PV, t, &pv_w)) {
        return false;
    }
    /* The configuration bounds the gain only from below, so a huge gain can overflow. */
    solar_w = pv_w * gain;
    if (!isfinite(solar_w)) {
        return false;
    }
    write_reading(reading, CTL_SIGNAL_P_SOLAR, solar_w, t);
    return true;
}
