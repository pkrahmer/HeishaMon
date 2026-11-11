/*
 * The pump and solar adapters: they turn HeishaMon's outdoor temperature, produced heat, valve
 * position and defrost flag, and the PV system's DC power, into readings for the core. The
 * produced heat is masked to 0 W while the pump heats the hot-water tank or defrosts, because
 * that heat does not enter the rooms; a change of either flag gives the heat again under the new
 * mask, so that the core sees the switch when it happens rather than at the next power report.
 * Instants are seconds of the core's monotonic clock, handed in by the caller.
 */
#ifndef CTL_PUMP_H
#define CTL_PUMP_H

#include "../core/ctl_types.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    ctl_signal signal;
    double value;
    double t;
} ctl_reading;

typedef struct {
    bool has_heat;
    double heat_w;   /* the last accepted produced heat, before the mask */
    bool hot_water;  /* the valve is on the hot-water tank */
    bool defrost;
} ctl_pump;

/* Leaves the pump without a heat (heat_w 0.0), the valve off the hot-water tank and no defrost. */
void ctl_pump_init(ctl_pump *pump);

/* Returns true with a T_OUTDOOR reading of the payload's value at t in *reading. Refuses,
 * returning false and leaving *reading as it was, a payload the parser refuses, a value
 * CTL_BOUND_TEMPERATURE refuses and a t that is not finite. */
bool ctl_pump_outdoor(const char *payload, size_t length, double t, ctl_reading *reading);

/* Stores the payload's value as the last heat and returns true with a P_PRODUCED reading at t,
 * of 0.0 while the valve is on hot water or the pump defrosts and of the value otherwise.
 * Refuses, returning false and leaving the pump and *reading as they were, a payload the parser
 * refuses, a value CTL_BOUND_HEAT refuses and a t that is not finite. */
bool ctl_pump_heat(ctl_pump *pump, const char *payload, size_t length, double t,
                   ctl_reading *reading);

/* Sets hot_water to whether the payload's value is 1.0. Returns true with the stored heat as a
 * P_PRODUCED reading at t under the new mask when that switched the mask on or off and a heat is
 * stored; returns false, writing no reading, otherwise: a value that leaves hot_water as it was,
 * and a change while defrost still masks, change nothing the core sees. Refuses, returning false and leaving the pump and
 * *reading as they were, a payload the parser refuses, a value that is not exactly 0.0 or 1.0
 * and a t that is not finite. */
bool ctl_pump_valve(ctl_pump *pump, const char *payload, size_t length, double t,
                    ctl_reading *reading);

/* The same as ctl_pump_valve, for the defrost flag. */
bool ctl_pump_defrost(ctl_pump *pump, const char *payload, size_t length, double t,
                      ctl_reading *reading);

/* Returns true with a P_SOLAR reading of the PV power times gain at t in *reading. Refuses,
 * returning false and leaving *reading as it was, a payload the parser refuses, a PV power
 * CTL_BOUND_PV refuses, a gain that is not finite or below 0, and a t that is not finite. */
bool ctl_solar(const char *payload, size_t length, double gain, double t, ctl_reading *reading);

#endif
