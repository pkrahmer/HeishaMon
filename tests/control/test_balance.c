#include "check.h"
#include "core/ctl_balance.h"
#include "core/ctl_config.h"
#include "core/ctl_signals.h"

#include <stddef.h>

static ctl_inputs winter_inputs(double t_indoor_c)
{
    ctl_inputs inputs;
    inputs.t_indoor_c = t_indoor_c;
    inputs.t_outdoor_c = 5.0;
    inputs.p_produced_w = 3000.0;
    inputs.p_solar_w = 100.0;
    inputs.p_internal_w = 550.0;
    inputs.substituted = 0u;
    return inputs;
}

static void test_balance(void)
{
    const ctl_inputs inputs = winter_inputs(21.0);
    ctl_config config;
    ctl_balance balance;

    ctl_config_defaults(&config);
    ctl_energy_balance(&inputs, &config, &balance);
    CHECK_DOUBLE(balance.p_transmission_w, -2480.0);
    CHECK_DOUBLE(balance.p_model_w, 1170.0);
    CHECK_DOUBLE(balance.p_trim_w, -805.0);
    CHECK_DOUBLE(balance.p_net_w, 365.0);
}

typedef struct {
    bool trim_limit_enabled;
    double t_indoor_c;
    double p_trim_w;
} trim_row;

static void test_trim_clamp(void)
{
    const trim_row rows[] = {
        {true, 19.0, -2415.0},  {true, 17.0, -2415.0},  {true, 25.0, 2415.0},
        {true, 26.0, 2415.0},   {false, 17.0, -4025.0}, {false, 26.0, 3220.0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const ctl_inputs inputs = winter_inputs(rows[i].t_indoor_c);
        ctl_config config;
        ctl_balance balance;

        ctl_config_defaults(&config);
        config.trim_limit_enabled = rows[i].trim_limit_enabled;
        ctl_energy_balance(&inputs, &config, &balance);
        CHECK_DOUBLE(balance.p_trim_w, rows[i].p_trim_w);
    }
}

static void check_ledger(const ctl_ledger *ledger, double factor)
{
    CHECK_DOUBLE(ledger->produced_wh, 1500.0 * factor);
    CHECK_DOUBLE(ledger->solar_wh, 50.0 * factor);
    CHECK_DOUBLE(ledger->internal_wh, 275.0 * factor);
    CHECK_DOUBLE(ledger->transmission_wh, -1240.0 * factor);
    CHECK_DOUBLE(ledger->trim_wh, -402.5 * factor);
    CHECK_DOUBLE(ctl_ledger_total_wh(ledger), 182.5 * factor);
}

static void test_ledger(void)
{
    const ctl_inputs inputs = winter_inputs(21.0);
    ctl_config config;
    ctl_balance balance;
    ctl_ledger ledger;

    ctl_config_defaults(&config);
    ctl_energy_balance(&inputs, &config, &balance);
    ctl_ledger_init(&ledger);
    CHECK_DOUBLE(ledger.produced_wh, 0.0);
    CHECK_DOUBLE(ledger.solar_wh, 0.0);
    CHECK_DOUBLE(ledger.internal_wh, 0.0);
    CHECK_DOUBLE(ledger.transmission_wh, 0.0);
    CHECK_DOUBLE(ledger.trim_wh, 0.0);
    CHECK_DOUBLE(ctl_ledger_total_wh(&ledger), 0.0);

    ctl_ledger_accumulate(&ledger, &inputs, &balance, 0.5);
    check_ledger(&ledger, 1.0);
    ctl_ledger_accumulate(&ledger, &inputs, &balance, 0.5);
    check_ledger(&ledger, 2.0);
}

int main(void)
{
    test_balance();
    test_trim_clamp();
    test_ledger();
    return check_result();
}
