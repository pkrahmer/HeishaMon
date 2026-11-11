/*
 * The energy balance: the house treated as a thermal battery. The model predicts the net power
 * into the house from the inputs, the trim corrects whatever the model gets wrong from the
 * indoor temperature's offset, and their sum is what the controller integrates. The ledger
 * integrates each term on its own, so that the owner can see where the energy went.
 */
#ifndef CTL_BALANCE_H
#define CTL_BALANCE_H

#include "ctl_config.h"
#include "ctl_signals.h"

/* Powers in W; a negative p_net_w is a deficit, a positive one a surplus. */
typedef struct {
    double p_transmission_w;
    double p_model_w;
    double p_trim_w;
    double p_net_w;
} ctl_balance;

typedef struct {
    double produced_wh;
    double solar_wh;
    double internal_wh;
    double transmission_wh;
    double trim_wh;
} ctl_ledger;

/* Fills balance with the four powers of the inputs, in the Python reference's operand order.
 * Refuses nothing: a NaN input gives NaN powers. */
void ctl_energy_balance(const ctl_inputs *inputs, const ctl_config *config, ctl_balance *balance);

/* Sets the five terms to 0. Refuses nothing. */
void ctl_ledger_init(ctl_ledger *ledger);

/* Returns the sum of the five terms, from produced to trim. Refuses nothing. */
double ctl_ledger_total_wh(const ctl_ledger *ledger);

/* Adds to each term its power times hours. Refuses nothing. */
void ctl_ledger_accumulate(ctl_ledger *ledger, const ctl_inputs *inputs,
                           const ctl_balance *balance, double hours);

#endif
