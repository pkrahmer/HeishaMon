#include "ctl_relay.h"

ctl_demand ctl_relay(double energy_wh, ctl_demand previous, double hysteresis_wh)
{
    if (previous == CTL_DEMAND_HEATING) {
        return energy_wh >= 0.0 ? CTL_DEMAND_NONE : CTL_DEMAND_HEATING;
    }
    if (previous == CTL_DEMAND_COOLING) {
        return energy_wh <= 0.0 ? CTL_DEMAND_NONE : CTL_DEMAND_COOLING;
    }
    if (energy_wh <= -hysteresis_wh) {
        return CTL_DEMAND_HEATING;
    }
    if (energy_wh >= hysteresis_wh) {
        return CTL_DEMAND_COOLING;
    }
    return CTL_DEMAND_NONE;
}

void ctl_clamp_bounds(uint8_t allowed, const ctl_config *config, double *low, double *high)
{
    const double limit = ctl_config_energy_limit_wh(config);
    const bool heating = (allowed & CTL_DEMAND_BIT(CTL_DEMAND_HEATING)) != 0u;
    const bool cooling = (allowed & CTL_DEMAND_BIT(CTL_DEMAND_COOLING)) != 0u;

    /* A masked direction clamps at exactly its engage threshold. */
    *low = heating ? -limit : -config->hysteresis_wh;
    *high = cooling ? limit : config->hysteresis_wh;
}

double ctl_clamp(double value, double low, double high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}
