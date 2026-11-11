#include "ctl_controller.h"

#include <math.h>

#include "ctl_relay.h"

static const double seconds_per_hour = 3600.0;

void ctl_state_init(ctl_state *state)
{
    state->phase = CTL_PHASE_UNINITIALIZED;
    state->energy_wh = 0.0;
    state->demand = CTL_DEMAND_NONE;
    state->allowed = CTL_DEMAND_ALL;
    /* The values behind the two flags are read by no function, but a defined struct compares. */
    state->has_t_last = false;
    state->t_last = 0.0;
    state->has_t_healthy = false;
    state->t_healthy = 0.0;
    ctl_signals_init(&state->signals);
    state->integrated_s = 0.0;
    state->skipped_s = 0.0;
    ctl_ledger_init(&state->ledger);
    state->clamped = false;
}

static double clamp_to_allowed(double energy_wh, uint8_t allowed, const ctl_config *config)
{
    double low;
    double high;
    ctl_clamp_bounds(allowed, config, &low, &high);
    return ctl_clamp(energy_wh, low, high);
}

static void integrate(ctl_state *state, double t, double dt, const ctl_inputs *inputs,
                      const ctl_config *config)
{
    ctl_balance balance;
    double hours;
    double raw;
    double energy;

    ctl_energy_balance(inputs, config, &balance);
    hours = dt / seconds_per_hour;
    raw = state->energy_wh + balance.p_net_w * hours;
    energy = clamp_to_allowed(raw, state->allowed, config);
    state->t_last = t;
    state->energy_wh = energy;
    state->integrated_s = state->integrated_s + dt;
    ctl_ledger_accumulate(&state->ledger, inputs, &balance, hours);
    state->clamped = energy != raw;
}

bool ctl_advance(ctl_state *state, double t, const ctl_config *config, double *dt_s)
{
    ctl_inputs inputs;
    double dt;

    *dt_s = 0.0;
    if (!isfinite(t)) {
        return false;
    }
    if (!state->has_t_last) {
        state->has_t_last = true;
        state->t_last = t;
        return false;
    }
    dt = t - state->t_last;
    /* A duplicate instant or a clock that went backwards: no time travel. */
    if (dt <= 0.0) {
        return false;
    }
    *dt_s = dt;
    /* Zero-order hold: the inputs at the interval's start held throughout it. Time nobody
     * observed adds no energy, but the reference still moves past it. */
    if (!ctl_signals_resolve(&state->signals, state->t_last, config, &inputs) ||
        dt > config->max_step_s) {
        state->t_last = t;
        state->skipped_s = state->skipped_s + dt;
        return false;
    }
    integrate(state, t, dt, &inputs, config);
    return true;
}

void ctl_observe(ctl_state *state, ctl_signal signal, double value, double t,
                 const ctl_config *config)
{
    double dt_s;

    if (!isfinite(value) || !isfinite(t)) {
        return;
    }
    if (state->signals.observed[signal] && t < state->signals.t[signal]) {
        return;
    }
    /* Close the open interval with the old value before the new one takes effect. */
    (void)ctl_advance(state, t, config, &dt_s);
    ctl_signals_set(&state->signals, signal, value, t);
}

void ctl_set_allowed(ctl_state *state, uint8_t allowed, double t, const ctl_config *config)
{
    const uint8_t new_allowed = (uint8_t)(allowed | CTL_DEMAND_BIT(CTL_DEMAND_NONE));
    double dt_s;

    /* Advance even when the mask stays, so that re-asserting the mask on a tick takes the same
     * path as changing it. */
    (void)ctl_advance(state, t, config, &dt_s);
    if (new_allowed == state->allowed) {
        return;
    }
    state->allowed = new_allowed;
    state->energy_wh = clamp_to_allowed(state->energy_wh, new_allowed, config);
}

/* Fills the fields every decision reads from the state, after the state has moved. */
static void describe_state(const ctl_state *state, double t, const ctl_config *config,
                           ctl_decision *decision)
{
    decision->t = t;
    decision->phase = state->phase;
    decision->energy_wh = state->energy_wh;
    decision->hysteresis_wh = config->hysteresis_wh;
    decision->integrated_s = state->integrated_s;
    decision->skipped_s = state->skipped_s;
    decision->ledger = state->ledger;
    decision->clamped = state->clamped;
}

static void clear_inputs(ctl_decision *decision)
{
    static const ctl_inputs no_inputs;
    static const ctl_balance no_balance;
    decision->has_inputs = false;
    decision->inputs = no_inputs;
    decision->balance = no_balance;
}

static void refuse_invalid_time(const ctl_state *state, double t, const ctl_config *config,
                                ctl_decision *decision)
{
    describe_state(state, t, config, decision);
    decision->demand = state->demand;
    decision->reason = CTL_REASON_INVALID_TIME;
    decision->dt_s = 0.0;
    clear_inputs(decision);
    decision->unhealthy = 0u;
    decision->substituted = 0u;
}

static void mark_healthy(ctl_state *state, double t)
{
    state->has_t_healthy = true;
    state->t_healthy = t;
}

static double seconds_since_healthy(const ctl_state *state, double t)
{
    return state->has_t_healthy ? t - state->t_healthy : INFINITY;
}

/* Required inputs are unusable: coast on the last demand, then fail off. */
static ctl_reason decide_without_inputs(ctl_state *state, double t, const ctl_config *config)
{
    if (state->phase != CTL_PHASE_FAULT &&
        seconds_since_healthy(state, t) <= config->input_hold_s) {
        state->phase = CTL_PHASE_HOLD;
        return CTL_REASON_HELD_STALE_INPUT;
    }
    state->phase = CTL_PHASE_FAULT;
    state->demand = CTL_DEMAND_NONE;
    return CTL_REASON_FAULT_TIMEOUT;
}

/* The relay on the accumulator, then the supervisor's mask. The masked result is stored, so
 * that the relay's previous demand is always the one emitted. */
static ctl_reason decide_normal(ctl_state *state, double t, const ctl_config *config)
{
    ctl_demand demand = ctl_relay(state->energy_wh, state->demand, config->hysteresis_wh);
    ctl_reason reason = CTL_REASON_NORMAL;
    if ((state->allowed & CTL_DEMAND_BIT(demand)) == 0u) {
        demand = CTL_DEMAND_NONE;
        reason = CTL_REASON_MASKED;
    }
    state->phase = CTL_PHASE_RUNNING;
    state->demand = demand;
    mark_healthy(state, t);
    return reason;
}

/* Moves the state's phase and demand and returns the reason. Every reason but
 * CTL_REASON_WAITING_FOR_INPUTS emits the state's demand afterwards. */
static ctl_reason decide(ctl_state *state, double t, bool resolved, const ctl_config *config)
{
    if (state->phase == CTL_PHASE_UNINITIALIZED) {
        if (!resolved) {
            return CTL_REASON_WAITING_FOR_INPUTS;
        }
        state->phase = CTL_PHASE_RUNNING;
        mark_healthy(state, t);
        return CTL_REASON_SEEDED;
    }
    /* After a long blind period the accumulated charge is not trustworthy, so start again
     * from the neutral prior. */
    if (state->phase == CTL_PHASE_FAULT && resolved) {
        state->phase = CTL_PHASE_RUNNING;
        state->energy_wh = 0.0;
        state->demand = CTL_DEMAND_NONE;
        mark_healthy(state, t);
        return CTL_REASON_FAULT_RECOVERED;
    }
    if (!resolved) {
        return decide_without_inputs(state, t, config);
    }
    return decide_normal(state, t, config);
}

void ctl_evaluate(ctl_state *state, double t, const ctl_config *config, ctl_decision *decision)
{
    ctl_inputs inputs;
    double dt_s;
    bool resolved;

    if (!isfinite(t)) {
        /* Letting a NaN through would set t_healthy to NaN and void the grace period of
         * the next stale evaluate. */
        refuse_invalid_time(state, t, config, decision);
        return;
    }
    (void)ctl_advance(state, t, config, &dt_s);
    resolved = ctl_signals_resolve(&state->signals, t, config, &inputs);
    clear_inputs(decision);
    decision->substituted = 0u;
    if (resolved) {
        decision->has_inputs = true;
        decision->inputs = inputs;
        ctl_energy_balance(&inputs, config, &decision->balance);
        decision->substituted = inputs.substituted;
    }
    decision->unhealthy = ctl_signals_unhealthy(&state->signals, t, config);
    decision->reason = decide(state, t, resolved, config);
    /* Before the seeding the state's demand is not yet a decision. */
    decision->demand = decision->reason == CTL_REASON_WAITING_FOR_INPUTS ? CTL_DEMAND_NONE
                                                                         : state->demand;
    decision->dt_s = dt_s;
    describe_state(state, t, config, decision);
}
