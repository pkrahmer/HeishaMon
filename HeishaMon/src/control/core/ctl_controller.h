/*
 * The controller: the state the control loop remembers and the four functions that move it.
 * Time handling is the algorithm here, so it is concentrated in ctl_advance, the only function
 * that moves the integration reference and changes the energy integral; the other three call it
 * first; it is also where the integral term learns. A reading closes the open interval with the old held values before it takes effect,
 * because a reading describes the world from its own instant onward and never before it.
 */
#ifndef CTL_CONTROLLER_H
#define CTL_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "ctl_balance.h"
#include "ctl_config.h"
#include "ctl_signals.h"
#include "ctl_types.h"

/* The caller owns the state; the four functions change it in place. */
typedef struct {
    ctl_phase phase;
    double energy_wh;
    ctl_demand demand;
    uint8_t allowed;      /* CTL_DEMAND_BIT of each permitted demand */
    bool has_t_last;
    double t_last;        /* the integration reference */
    bool has_t_healthy;
    double t_healthy;     /* the last instant every required signal resolved */
    ctl_signal_table signals;
    double integrated_s;
    double skipped_s;
    ctl_ledger ledger;
    bool clamped;
    double p_integral_w;  /* the integral term, in W; a positive one is a surplus */
} ctl_state;

/* Everything needed to explain one decision after the fact. */
typedef struct {
    double t;
    ctl_demand demand;
    ctl_reason reason;
    ctl_phase phase;
    double energy_wh;
    double hysteresis_wh;
    double dt_s;
    double integrated_s;
    double skipped_s;
    bool has_inputs;      /* false: inputs and balance hold nothing */
    ctl_inputs inputs;
    ctl_balance balance;
    ctl_ledger ledger;
    bool clamped;
    uint8_t unhealthy;    /* CTL_SIGNAL_BIT of each required signal that is not fresh */
    uint8_t substituted;  /* CTL_SIGNAL_BIT of each optional signal that fell back */
} ctl_decision;

/* Sets the state of a cold start: UNINITIALIZED, no energy, no demand, every direction
 * permitted, no time reference, no signal and no integral. Refuses nothing. */
void ctl_state_init(ctl_state *state);

/* Moves the integration reference to t and returns true when it integrated the interval it
 * closed; *dt_s gets that interval's length, or 0.0 when it closed none. An interval whose
 * start has no resolved signals, or that is longer than max_step_s, adds no energy: it is
 * counted in skipped_s and the reference still moves. Every interval it closes clamps the
 * integral to integral_limit_w; an integrated interval also teaches it when, at its start, the
 * indoor error lay within the trim limit and the energy was not at a bound. Refuses, changing
 * nothing and returning false with 0.0, a NaN or infinite t and a t at or before the reference. */
bool ctl_advance(ctl_state *state, double t, const ctl_config *config, double *dt_s);

/* Advances to t and then holds the reading. Refuses, changing nothing, a NaN or infinite value
 * or t, and a reading older than the one held for the same signal. A reading before the
 * reference is held without integration and without moving the reference. */
void ctl_observe(ctl_state *state, ctl_signal signal, double value, double t,
                 const ctl_config *config);

/* Advances to t, always, and then permits the demands of allowed and NONE; a changed mask
 * clamps the energy to its new bounds at once, so that a masked direction cannot bank windup.
 * Refuses nothing. */
void ctl_set_allowed(ctl_state *state, uint8_t allowed, double t, const ctl_config *config);

/* Advances to t, as far as ctl_advance takes it, and then sets the integral to 0. Refuses
 * nothing. */
void ctl_reset_integral(ctl_state *state, double t, const ctl_config *config);

/* Advances to t, decides the demand and fills the decision; the only function that changes the
 * demand or the phase. Refuses a NaN or infinite t: the state stays as it is and the decision
 * carries the reason CTL_REASON_INVALID_TIME with the state's demand. */
void ctl_evaluate(ctl_state *state, double t, const ctl_config *config, ctl_decision *decision);

#endif
