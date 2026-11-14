/*
 * The tri-state relay on the accumulated energy deviation, and its anti-windup clamp. Demand
 * engages at minus or plus the hysteresis and releases at 0, so a change of direction always
 * passes through no demand and costs twice the hysteresis of energy travel. The clamp bounds
 * the accumulator, tighter on a direction the supervisor has masked, so that unmasking gives a
 * bounded response instead of releasing hours of stored windup.
 */
#ifndef CTL_RELAY_H
#define CTL_RELAY_H

#include <stdint.h>

#include "ctl_config.h"
#include "ctl_types.h"

/* A set of demands is a bit mask; this is the bit of one demand. */
#define CTL_DEMAND_BIT(demand) ((uint8_t)(1u << (demand)))
#define CTL_DEMAND_ALL                                                                   \
    ((uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_COOLING) | \
               CTL_DEMAND_BIT(CTL_DEMAND_NONE)))

/* Returns the demand after at most one transition from previous, which must be the demand last
 * emitted. A previous that is no member is treated as CTL_DEMAND_NONE. A NaN energy keeps
 * heating and cooling and gives no demand from no demand. Refuses nothing. */
ctl_demand ctl_relay(double energy_wh, ctl_demand previous, double hysteresis_wh);

/* Sets low and high to the bounds of the energy accumulator: the energy limit in an allowed
 * direction, the hysteresis in a masked one. The bit of CTL_DEMAND_NONE plays no part.
 * Refuses nothing. */
void ctl_clamp_bounds(uint8_t allowed, const ctl_config *config, double *low, double *high);

/* Returns value bounded to low and high; a NaN value is returned as it is. Refuses nothing. */
double ctl_clamp(double value, double low, double high);

#endif
