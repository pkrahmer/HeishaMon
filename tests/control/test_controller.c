#include "check.h"
#include "core/ctl_balance.h"
#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "core/ctl_relay.h"
#include "core/ctl_signals.h"
#include "core/ctl_types.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* With the defaults and the fallbacks, the three readings give this net power. */
static const double net_power_w = -2735.0;

static const uint8_t required_bits =
    (uint8_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR) | CTL_SIGNAL_BIT(CTL_SIGNAL_T_OUTDOOR) |
              CTL_SIGNAL_BIT(CTL_SIGNAL_P_PRODUCED));
static const uint8_t optional_bits =
    (uint8_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_P_SOLAR) | CTL_SIGNAL_BIT(CTL_SIGNAL_P_INTERNAL));

static void observe_three_readings(ctl_state *state, double t, const ctl_config *config)
{
    ctl_observe(state, CTL_SIGNAL_T_INDOOR, 21.0, t, config);
    ctl_observe(state, CTL_SIGNAL_T_OUTDOOR, 5.0, t, config);
    ctl_observe(state, CTL_SIGNAL_P_PRODUCED, 0.0, t, config);
}

/* A state with the three readings at 0.0 and the reference at 0.0. */
static void state_with_readings(ctl_state *state, const ctl_config *config)
{
    ctl_state_init(state);
    observe_three_readings(state, 0.0, config);
}

/* The state after the cold start: seeded at 300.0 on the readings from 0.0. */
static void seeded_state(ctl_state *state, const ctl_config *config)
{
    ctl_decision decision;
    state_with_readings(state, config);
    ctl_evaluate(state, 300.0, config, &decision);
}

static double one_step_energy(double start_wh)
{
    return start_wh + net_power_w * (300.0 / 3600.0);
}

static void check_same_ledger(const ctl_ledger *actual, const ctl_ledger *expected)
{
    CHECK_DOUBLE(actual->produced_wh, expected->produced_wh);
    CHECK_DOUBLE(actual->solar_wh, expected->solar_wh);
    CHECK_DOUBLE(actual->internal_wh, expected->internal_wh);
    CHECK_DOUBLE(actual->transmission_wh, expected->transmission_wh);
    CHECK_DOUBLE(actual->trim_wh, expected->trim_wh);
}

static void check_same_state(const ctl_state *actual, const ctl_state *expected)
{
    int signal;
    CHECK_TEXT(ctl_phase_name(actual->phase), ctl_phase_name(expected->phase));
    CHECK_DOUBLE(actual->energy_wh, expected->energy_wh);
    CHECK_TEXT(ctl_demand_name(actual->demand), ctl_demand_name(expected->demand));
    CHECK_UINT(actual->allowed, expected->allowed);
    CHECK_TRUE(actual->has_t_last == expected->has_t_last);
    CHECK_DOUBLE(actual->t_last, expected->t_last);
    CHECK_TRUE(actual->has_t_healthy == expected->has_t_healthy);
    CHECK_DOUBLE(actual->t_healthy, expected->t_healthy);
    for (signal = 0; signal < CTL_SIGNAL_COUNT; signal++) {
        CHECK_TRUE(actual->signals.observed[signal] == expected->signals.observed[signal]);
        CHECK_DOUBLE(actual->signals.value[signal], expected->signals.value[signal]);
        CHECK_DOUBLE(actual->signals.t[signal], expected->signals.t[signal]);
    }
    CHECK_DOUBLE(actual->integrated_s, expected->integrated_s);
    CHECK_DOUBLE(actual->skipped_s, expected->skipped_s);
    check_same_ledger(&actual->ledger, &expected->ledger);
    CHECK_TRUE(actual->clamped == expected->clamped);
}

static void check_one_step_integrated(const ctl_state *state)
{
    const double h = 300.0 / 3600.0;
    CHECK_DOUBLE(state->energy_wh, one_step_energy(0.0));
    CHECK_DOUBLE(state->integrated_s, 300.0);
    CHECK_DOUBLE(state->skipped_s, 0.0);
    CHECK_TRUE(!state->clamped);
    CHECK_DOUBLE(state->ledger.produced_wh, 0.0);
    CHECK_DOUBLE(state->ledger.solar_wh, 0.0);
    CHECK_DOUBLE(state->ledger.internal_wh, 550.0 * h);
    CHECK_DOUBLE(state->ledger.transmission_wh, -2480.0 * h);
    CHECK_DOUBLE(state->ledger.trim_wh, -805.0 * h);
}

static void test_state_init(void)
{
    ctl_state state;
    ctl_signal_table empty;
    ctl_ledger zero;
    int signal;

    ctl_signals_init(&empty);
    ctl_ledger_init(&zero);
    ctl_state_init(&state);
    CHECK_TEXT(ctl_phase_name(state.phase), "UNINITIALIZED");
    CHECK_DOUBLE(state.energy_wh, 0.0);
    CHECK_TEXT(ctl_demand_name(state.demand), "NO_DEMAND");
    CHECK_UINT(state.allowed, CTL_DEMAND_ALL);
    CHECK_TRUE(!state.has_t_last);
    CHECK_TRUE(!state.has_t_healthy);
    for (signal = 0; signal < CTL_SIGNAL_COUNT; signal++) {
        CHECK_TRUE(state.signals.observed[signal] == empty.observed[signal]);
        CHECK_DOUBLE(state.signals.value[signal], empty.value[signal]);
        CHECK_DOUBLE(state.signals.t[signal], empty.t[signal]);
    }
    CHECK_DOUBLE(state.integrated_s, 0.0);
    CHECK_DOUBLE(state.skipped_s, 0.0);
    check_same_ledger(&state.ledger, &zero);
    CHECK_TRUE(!state.clamped);
}

typedef struct {
    double t;
    bool has_t_last;
    double t_last;
} advance_row;

/* The rows run in order on one state. */
static void test_advance_without_integration(void)
{
    const advance_row rows[] = {
        {NAN, false, 0.0},      {INFINITY, false, 0.0}, {-INFINITY, false, 0.0},
        {10.0, true, 10.0},     {10.0, true, 10.0},     {5.0, true, 10.0},
        {NAN, true, 10.0},      {INFINITY, true, 10.0},
    };
    ctl_config config;
    ctl_state state;
    size_t i;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        double dt_s = NAN;
        CHECK_TRUE(!ctl_advance(&state, rows[i].t, &config, &dt_s));
        CHECK_DOUBLE(dt_s, 0.0);
        CHECK_TRUE(state.has_t_last == rows[i].has_t_last);
        if (rows[i].has_t_last) {
            CHECK_DOUBLE(state.t_last, rows[i].t_last);
        }
        CHECK_DOUBLE(state.integrated_s, 0.0);
        CHECK_DOUBLE(state.skipped_s, 0.0);
    }
}

static void test_advance_skips(void)
{
    ctl_config config;
    ctl_state state;
    double dt_s = NAN;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    state.has_t_last = true;
    state.t_last = 0.0;
    CHECK_TRUE(!ctl_advance(&state, 100.0, &config, &dt_s));
    CHECK_DOUBLE(dt_s, 100.0);
    CHECK_DOUBLE(state.t_last, 100.0);
    CHECK_DOUBLE(state.skipped_s, 100.0);
    CHECK_DOUBLE(state.integrated_s, 0.0);
    CHECK_DOUBLE(state.energy_wh, 0.0);

    state_with_readings(&state, &config);
    CHECK_TRUE(!ctl_advance(&state, nextafter(300.0, INFINITY), &config, &dt_s));
    CHECK_DOUBLE(dt_s, nextafter(300.0, INFINITY));
    CHECK_DOUBLE(state.skipped_s, nextafter(300.0, INFINITY));
    CHECK_DOUBLE(state.energy_wh, 0.0);

    state_with_readings(&state, &config);
    CHECK_TRUE(ctl_advance(&state, 300.0, &config, &dt_s));
    CHECK_DOUBLE(dt_s, 300.0);
}

static void test_advance_integrates(void)
{
    ctl_config config;
    ctl_state state;
    double dt_s = NAN;

    ctl_config_defaults(&config);
    state_with_readings(&state, &config);
    CHECK_TRUE(state.has_t_last);
    CHECK_DOUBLE(state.t_last, 0.0);
    CHECK_TRUE(ctl_advance(&state, 300.0, &config, &dt_s));
    CHECK_DOUBLE(dt_s, 300.0);
    CHECK_DOUBLE(state.t_last, 300.0);
    check_one_step_integrated(&state);
}

static void test_advance_clamps(void)
{
    ctl_config config;
    ctl_state state;
    double dt_s = NAN;

    ctl_config_defaults(&config);
    state_with_readings(&state, &config);
    state.energy_wh = -2990.0;
    CHECK_TRUE(ctl_advance(&state, 300.0, &config, &dt_s));
    CHECK_DOUBLE(state.energy_wh, -3000.0);
    CHECK_TRUE(state.clamped);

    ctl_observe(&state, CTL_SIGNAL_P_PRODUCED, 5000.0, 300.0, &config);
    CHECK_TRUE(ctl_advance(&state, 600.0, &config, &dt_s));
    CHECK_DOUBLE(state.energy_wh, -3000.0 + 2265.0 * (300.0 / 3600.0));
    CHECK_TRUE(!state.clamped);
}

typedef struct {
    double value;
    double t;
} reading_row;

static void test_observe_refuses(void)
{
    const reading_row rows[] = {
        {NAN, 150.0}, {INFINITY, 150.0}, {22.0, NAN}, {22.0, INFINITY}, {22.0, 50.0},
    };
    ctl_config config;
    ctl_state arranged;
    size_t i;

    ctl_config_defaults(&config);
    ctl_state_init(&arranged);
    ctl_observe(&arranged, CTL_SIGNAL_T_INDOOR, 21.0, 100.0, &config);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_state state = arranged;
        ctl_observe(&state, CTL_SIGNAL_T_INDOOR, rows[i].value, rows[i].t, &config);
        check_same_state(&state, &arranged);
    }

    ctl_observe(&arranged, CTL_SIGNAL_T_INDOOR, 23.0, 100.0, &config);
    CHECK_DOUBLE(arranged.signals.value[CTL_SIGNAL_T_INDOOR], 23.0);
    CHECK_DOUBLE(arranged.signals.t[CTL_SIGNAL_T_INDOOR], 100.0);
}

static void test_observe_closes_interval_with_old_value(void)
{
    ctl_config config;
    ctl_state state;

    ctl_config_defaults(&config);
    state_with_readings(&state, &config);
    ctl_observe(&state, CTL_SIGNAL_T_INDOOR, 22.0, 300.0, &config);
    CHECK_DOUBLE(state.energy_wh, one_step_energy(0.0));
    CHECK_TRUE(state.signals.observed[CTL_SIGNAL_T_INDOOR]);
    CHECK_DOUBLE(state.signals.value[CTL_SIGNAL_T_INDOOR], 22.0);
    CHECK_DOUBLE(state.signals.t[CTL_SIGNAL_T_INDOOR], 300.0);
}

static void test_observe_before_reference(void)
{
    ctl_config config;
    ctl_state state;
    ctl_state before;

    ctl_config_defaults(&config);
    state_with_readings(&state, &config);
    state.t_last = 300.0;
    before = state;
    ctl_observe(&state, CTL_SIGNAL_T_OUTDOOR, 4.0, 200.0, &config);
    CHECK_DOUBLE(state.signals.value[CTL_SIGNAL_T_OUTDOOR], 4.0);
    CHECK_DOUBLE(state.signals.t[CTL_SIGNAL_T_OUTDOOR], 200.0);
    CHECK_DOUBLE(state.t_last, before.t_last);
    CHECK_DOUBLE(state.energy_wh, before.energy_wh);
    CHECK_DOUBLE(state.integrated_s, before.integrated_s);
    CHECK_DOUBLE(state.skipped_s, before.skipped_s);
}

static void test_set_allowed(void)
{
    const uint8_t heating = CTL_DEMAND_BIT(CTL_DEMAND_HEATING);
    const uint8_t cooling = CTL_DEMAND_BIT(CTL_DEMAND_COOLING);
    const uint8_t none = CTL_DEMAND_BIT(CTL_DEMAND_NONE);
    ctl_config config;
    ctl_state state;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    ctl_set_allowed(&state, heating, 0.0, &config);
    CHECK_UINT(state.allowed, (uint8_t)(heating | none));
    CHECK_TRUE(!state.clamped);

    ctl_state_init(&state);
    state.energy_wh = -2000.0;
    ctl_set_allowed(&state, cooling, 0.0, &config);
    CHECK_UINT(state.allowed, (uint8_t)(cooling | none));
    CHECK_DOUBLE(state.energy_wh, -1500.0);
    CHECK_TRUE(!state.clamped);
    ctl_set_allowed(&state, CTL_DEMAND_ALL, 0.0, &config);
    CHECK_UINT(state.allowed, CTL_DEMAND_ALL);
    CHECK_DOUBLE(state.energy_wh, -1500.0);
    CHECK_TRUE(!state.clamped);

    state_with_readings(&state, &config);
    ctl_set_allowed(&state, CTL_DEMAND_ALL, 300.0, &config);
    CHECK_DOUBLE(state.t_last, 300.0);
    check_one_step_integrated(&state);
}

static void test_set_allowed_unchanged_mask_keeps_energy(void)
{
    const uint8_t cooling_and_none =
        (uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_COOLING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE));
    ctl_config config;
    ctl_state state;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    state.allowed = cooling_and_none;
    state.energy_wh = -2000.0;
    ctl_set_allowed(&state, CTL_DEMAND_BIT(CTL_DEMAND_COOLING), 0.0, &config);
    CHECK_UINT(state.allowed, cooling_and_none);
    CHECK_DOUBLE(state.energy_wh, -2000.0);
}

/* Integrating against the new bounds would clamp and set the flag; the old bounds do not. */
static void test_set_allowed_advances_before_the_new_mask(void)
{
    ctl_config config;
    ctl_state state;

    ctl_config_defaults(&config);
    state_with_readings(&state, &config);
    state.energy_wh = -1400.0;
    ctl_set_allowed(&state, CTL_DEMAND_BIT(CTL_DEMAND_COOLING), 300.0, &config);
    CHECK_DOUBLE(state.t_last, 300.0);
    CHECK_DOUBLE(state.integrated_s, 300.0);
    CHECK_DOUBLE(state.energy_wh, -1500.0);
    CHECK_TRUE(!state.clamped);
}

static void test_set_allowed_keeps_clamped(void)
{
    ctl_config config;
    ctl_state state;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    state.energy_wh = -2000.0;
    state.clamped = true;
    ctl_set_allowed(&state, CTL_DEMAND_BIT(CTL_DEMAND_COOLING), 0.0, &config);
    CHECK_DOUBLE(state.energy_wh, -1500.0);
    CHECK_TRUE(state.clamped);
}

static void test_evaluate_invalid_time(void)
{
    const double times[] = {NAN, INFINITY};
    ctl_config config;
    ctl_state arranged;
    size_t i;

    ctl_config_defaults(&config);
    state_with_readings(&arranged, &config);
    arranged.phase = CTL_PHASE_RUNNING;
    arranged.demand = CTL_DEMAND_HEATING;
    arranged.energy_wh = -100.0;
    for (i = 0; i < sizeof times / sizeof times[0]; i++) {
        ctl_state state = arranged;
        ctl_decision decision;
        ctl_evaluate(&state, times[i], &config, &decision);
        check_same_state(&state, &arranged);
        CHECK_DOUBLE(decision.t, times[i]);
        CHECK_TEXT(ctl_reason_name(decision.reason), "INVALID_TIME");
        CHECK_TEXT(ctl_demand_name(decision.demand), "HEATING");
        CHECK_TEXT(ctl_phase_name(decision.phase), "RUNNING");
        CHECK_DOUBLE(decision.energy_wh, -100.0);
        CHECK_DOUBLE(decision.hysteresis_wh, 1500.0);
        CHECK_DOUBLE(decision.dt_s, 0.0);
        CHECK_DOUBLE(decision.integrated_s, arranged.integrated_s);
        CHECK_DOUBLE(decision.skipped_s, arranged.skipped_s);
        CHECK_TRUE(!decision.has_inputs);
        check_same_ledger(&decision.ledger, &arranged.ledger);
        CHECK_TRUE(decision.clamped == arranged.clamped);
        CHECK_UINT(decision.unhealthy, 0u);
        CHECK_UINT(decision.substituted, 0u);
    }
}

static void test_evaluate_cold_start(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    ctl_evaluate(&state, 0.0, &config, &decision);
    CHECK_TEXT(ctl_demand_name(decision.demand), "NO_DEMAND");
    CHECK_TEXT(ctl_reason_name(decision.reason), "WAITING_FOR_INPUTS");
    CHECK_TEXT(ctl_phase_name(decision.phase), "UNINITIALIZED");
    CHECK_TEXT(ctl_phase_name(state.phase), "UNINITIALIZED");
    CHECK_TRUE(!decision.has_inputs);
    CHECK_UINT(decision.unhealthy, required_bits);

    observe_three_readings(&state, 0.0, &config);
    ctl_evaluate(&state, 300.0, &config, &decision);
    CHECK_DOUBLE(decision.t, 300.0);
    CHECK_TEXT(ctl_demand_name(decision.demand), "NO_DEMAND");
    CHECK_TEXT(ctl_reason_name(decision.reason), "SEEDED");
    CHECK_TEXT(ctl_phase_name(decision.phase), "RUNNING");
    CHECK_TEXT(ctl_phase_name(state.phase), "RUNNING");
    CHECK_DOUBLE(decision.dt_s, 300.0);
    CHECK_DOUBLE(decision.energy_wh, one_step_energy(0.0));
    CHECK_DOUBLE(decision.integrated_s, 300.0);
    CHECK_TRUE(decision.has_inputs);
    CHECK_DOUBLE(decision.inputs.t_indoor_c, 21.0);
    CHECK_DOUBLE(decision.balance.p_net_w, net_power_w);
    CHECK_UINT(decision.unhealthy, 0u);
    CHECK_UINT(decision.substituted, optional_bits);
    CHECK_TRUE(state.has_t_healthy);
    CHECK_DOUBLE(state.t_healthy, 300.0);
}

static void test_evaluate_normal(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    seeded_state(&state, &config);
    state.energy_wh = -1500.0;
    ctl_evaluate(&state, 300.0, &config, &decision);
    CHECK_TEXT(ctl_demand_name(decision.demand), "HEATING");
    CHECK_TEXT(ctl_reason_name(decision.reason), "NORMAL");
    CHECK_TEXT(ctl_phase_name(decision.phase), "RUNNING");
    CHECK_DOUBLE(decision.dt_s, 0.0);
    CHECK_TEXT(ctl_demand_name(state.demand), "HEATING");

    state.energy_wh = 0.0;
    ctl_evaluate(&state, 300.0, &config, &decision);
    CHECK_TEXT(ctl_demand_name(decision.demand), "NO_DEMAND");
    CHECK_TEXT(ctl_reason_name(decision.reason), "NORMAL");
    CHECK_TEXT(ctl_demand_name(state.demand), "NO_DEMAND");
}

static void test_evaluate_masked(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    seeded_state(&state, &config);
    state.allowed =
        (uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_COOLING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE));
    state.energy_wh = -1500.0;
    ctl_evaluate(&state, 300.0, &config, &decision);
    CHECK_TEXT(ctl_demand_name(decision.demand), "NO_DEMAND");
    CHECK_TEXT(ctl_reason_name(decision.reason), "MASKED");
    CHECK_TEXT(ctl_demand_name(state.demand), "NO_DEMAND");
}

static void check_held(const ctl_state *state, const ctl_decision *decision)
{
    CHECK_TEXT(ctl_demand_name(decision->demand), "HEATING");
    CHECK_TEXT(ctl_reason_name(decision->reason), "HELD_STALE_INPUT");
    CHECK_TEXT(ctl_phase_name(decision->phase), "HOLD");
    CHECK_TEXT(ctl_phase_name(state->phase), "HOLD");
    CHECK_TEXT(ctl_demand_name(state->demand), "HEATING");
}

static void check_faulted(const ctl_state *state, const ctl_decision *decision)
{
    CHECK_TEXT(ctl_demand_name(decision->demand), "NO_DEMAND");
    CHECK_TEXT(ctl_reason_name(decision->reason), "FAULT_TIMEOUT");
    CHECK_TEXT(ctl_phase_name(decision->phase), "FAULT");
    CHECK_TEXT(ctl_phase_name(state->phase), "FAULT");
    CHECK_TEXT(ctl_demand_name(state->demand), "NO_DEMAND");
}

static void test_evaluate_hold_fault_recovery(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    seeded_state(&state, &config);
    state.demand = CTL_DEMAND_HEATING;
    state.energy_wh = -500.0;
    CHECK_DOUBLE(state.t_healthy, 300.0);

    ctl_evaluate(&state, 700.0, &config, &decision);
    check_held(&state, &decision);
    CHECK_TRUE(!decision.has_inputs);
    CHECK_UINT(decision.unhealthy, required_bits);
    CHECK_UINT(decision.substituted, 0u);
    CHECK_DOUBLE(decision.energy_wh, -500.0);
    CHECK_DOUBLE(state.energy_wh, -500.0);
    CHECK_DOUBLE(decision.skipped_s, 400.0);
    CHECK_DOUBLE(state.t_healthy, 300.0);

    ctl_evaluate(&state, 1200.0, &config, &decision);
    check_held(&state, &decision);

    ctl_evaluate(&state, nextafter(1200.0, INFINITY), &config, &decision);
    check_faulted(&state, &decision);

    ctl_evaluate(&state, 1300.0, &config, &decision);
    check_faulted(&state, &decision);

    observe_three_readings(&state, 1400.0, &config);
    ctl_evaluate(&state, 1400.0, &config, &decision);
    CHECK_TEXT(ctl_demand_name(decision.demand), "NO_DEMAND");
    CHECK_TEXT(ctl_reason_name(decision.reason), "FAULT_RECOVERED");
    CHECK_TEXT(ctl_phase_name(decision.phase), "RUNNING");
    CHECK_DOUBLE(decision.energy_wh, 0.0);
    CHECK_DOUBLE(state.energy_wh, 0.0);
    CHECK_TEXT(ctl_demand_name(state.demand), "NO_DEMAND");
    CHECK_DOUBLE(state.t_healthy, 1400.0);
}

/* A fault is left only by fresh inputs, never by the hold's grace period. */
static void test_evaluate_fault_does_not_hold(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    seeded_state(&state, &config);
    state.phase = CTL_PHASE_FAULT;
    ctl_evaluate(&state, 700.0, &config, &decision);
    CHECK_DOUBLE(state.t_healthy, 300.0);
    check_faulted(&state, &decision);
}

static void test_evaluate_without_t_healthy_faults(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    ctl_state_init(&state);
    state.phase = CTL_PHASE_RUNNING;
    state.demand = CTL_DEMAND_HEATING;
    ctl_evaluate(&state, 100.0, &config, &decision);
    check_faulted(&state, &decision);
}

static void test_evaluate_fault_recovery_keeps_clamped(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;

    ctl_config_defaults(&config);
    seeded_state(&state, &config);
    ctl_evaluate(&state, 1300.0, &config, &decision);
    check_faulted(&state, &decision);
    state.clamped = true;
    observe_three_readings(&state, 1400.0, &config);
    ctl_evaluate(&state, 1400.0, &config, &decision);
    CHECK_TEXT(ctl_reason_name(decision.reason), "FAULT_RECOVERED");
    CHECK_TRUE(state.clamped);
    CHECK_TRUE(decision.clamped);
}

int main(void)
{
    test_state_init();
    test_advance_without_integration();
    test_advance_skips();
    test_advance_integrates();
    test_advance_clamps();
    test_observe_refuses();
    test_observe_closes_interval_with_old_value();
    test_observe_before_reference();
    test_set_allowed();
    test_set_allowed_unchanged_mask_keeps_energy();
    test_set_allowed_advances_before_the_new_mask();
    test_set_allowed_keeps_clamped();
    test_evaluate_invalid_time();
    test_evaluate_cold_start();
    test_evaluate_normal();
    test_evaluate_masked();
    test_evaluate_hold_fault_recovery();
    test_evaluate_fault_does_not_hold();
    test_evaluate_without_t_healthy_faults();
    test_evaluate_fault_recovery_keeps_clamped();
    return check_result();
}
