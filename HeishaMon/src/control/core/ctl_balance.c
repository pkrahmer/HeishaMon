#include "ctl_balance.h"

/* The deviation is clamped, not the power, so the limit stays readable in kelvin and does not
 * move when adjustment_w_per_k is retuned. */
static double trim_deviation(double t_indoor_c, const ctl_config *config)
{
    const double deviation = t_indoor_c - config->desired_indoor_c;
    const double limit = config->trim_limit_k;

    if (!config->trim_limit_enabled) {
        return deviation;
    }
    if (deviation < -limit) {
        return -limit;
    }
    if (deviation > limit) {
        return limit;
    }
    return deviation;
}

void ctl_energy_balance(const ctl_inputs *inputs, const ctl_config *config, ctl_balance *balance)
{
    const double p_transmission =
        config->transmission_w_per_k * (inputs->t_outdoor_c - inputs->t_indoor_c);
    const double p_model =
        inputs->p_produced_w + inputs->p_solar_w + inputs->p_internal_w + p_transmission;
    const double p_trim = config->adjustment_w_per_k * trim_deviation(inputs->t_indoor_c, config);

    balance->p_transmission_w = p_transmission;
    balance->p_model_w = p_model;
    balance->p_trim_w = p_trim;
    balance->p_net_w = p_model + p_trim;
}

void ctl_ledger_init(ctl_ledger *ledger)
{
    ledger->produced_wh = 0.0;
    ledger->solar_wh = 0.0;
    ledger->internal_wh = 0.0;
    ledger->transmission_wh = 0.0;
    ledger->trim_wh = 0.0;
}

double ctl_ledger_total_wh(const ctl_ledger *ledger)
{
    return ledger->produced_wh + ledger->solar_wh + ledger->internal_wh +
           ledger->transmission_wh + ledger->trim_wh;
}

void ctl_ledger_accumulate(ctl_ledger *ledger, const ctl_inputs *inputs,
                           const ctl_balance *balance, double hours)
{
    ledger->produced_wh += inputs->p_produced_w * hours;
    ledger->solar_wh += inputs->p_solar_w * hours;
    ledger->internal_wh += inputs->p_internal_w * hours;
    ledger->transmission_wh += balance->p_transmission_w * hours;
    ledger->trim_wh += balance->p_trim_w * hours;
}
