/*
 * The energy balance: the house treated as a thermal battery. The model predicts the net power
 * into the house from the inputs, the trim corrects whatever the model gets wrong from the
 * indoor temperature's offset, the integral term removes the offset the trim alone leaves, and
 * their sum is what the controller integrates. The ledger
 * integrates each term on its own, so that the owner can see where the energy went.
 */
#ifndef CTL_BALANCE_H
#define CTL_BALANCE_H

#include <stdbool.h>

#include "ctl_config.h"
#include "ctl_signals.h"

/* Powers in W; a negative p_net_w is a deficit, a positive one a surplus. */
typedef struct {
    double p_transmission_w;
    double p_model_w;
    double p_trim_w;
    double p_integral_w;
    double p_net_w;
} ctl_balance;

typedef struct {
    double produced_wh;
    double solar_wh;
    double internal_wh;
    double transmission_wh;
    double trim_wh;
    double integral_wh;
} ctl_ledger;

/* Fills balance with the powers of the inputs, in the Python reference's operand order, without
 * an integral term: p_integral_w is 0. Refuses nothing: a NaN input gives NaN powers. */
void ctl_energy_balance(const ctl_inputs *inputs, const ctl_config *config, ctl_balance *balance);

/* Sets p_integral_w and adds it to p_net_w last. An integral of exactly 0 leaves p_net_w as it
 * was, -0.0 included, so that a controller with the term off replays the golden traces bit for
 * bit. Refuses nothing. */
void ctl_balance_add_integral(ctl_balance *balance, double p_integral_w);

/* Returns the indoor error the trim and the integral act on: indoor minus desired, clamped to
 * the trim limit when it is enabled. Refuses nothing. */
double ctl_trim_deviation(double t_indoor_c, const ctl_config *config);

/* Returns true when the indoor error lies within the trim limit, its bounds included, or the
 * limit is off; false for a NaN error. Refuses nothing. */
bool ctl_trim_within_limit(double t_indoor_c, const ctl_config *config);

/* Sets the six terms to 0. Refuses nothing. */
void ctl_ledger_init(ctl_ledger *ledger);

/* Returns the sum of the six terms, from produced to integral. Refuses nothing. */
double ctl_ledger_total_wh(const ctl_ledger *ledger);

/* Adds to each term its power times hours. Refuses nothing. */
void ctl_ledger_accumulate(ctl_ledger *ledger, const ctl_inputs *inputs,
                           const ctl_balance *balance, double hours);

#endif
