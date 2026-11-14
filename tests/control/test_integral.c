#include "check.h"

#include "core/ctl_balance.h"
#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "core/ctl_relay.h"
#include "core/ctl_types.h"
#include "glue/ctl_dispatch.h"
#include "glue/ctl_settings.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP_S 60.0
#define OUTDOOR_C 5.0
#define TOLERANCE 1e-9
#define PATH_MAX_LENGTH 4096
#define LINE_MAX_LENGTH 1024
#define FIELDS_MAX 8

static void check_near(const char *file, int line, double actual, double expected)
{
    if (!(fabs(actual - expected) <= TOLERANCE)) {
        check_double(file, line, actual, expected);
    }
}

#define CHECK_NEAR(actual, expected) check_near(__FILE__, __LINE__, (actual), (expected))

/* The produced power that makes the model and the trim cancel at this indoor temperature, so
 * that the energy stays far from its bounds while the integral learns. */
static double balancing_produced_w(double indoor_c, const ctl_config *config)
{
    const double p_transmission = config->transmission_w_per_k * (OUTDOOR_C - indoor_c);
    const double p_trim = config->adjustment_w_per_k * (indoor_c - config->desired_indoor_c);
    return -(config->fallback_w[CTL_SIGNAL_P_INTERNAL] + p_transmission + p_trim);
}

static void observe_all(ctl_state *state, double indoor_c, double produced_w, double t,
                        const ctl_config *config)
{
    ctl_observe(state, CTL_SIGNAL_T_INDOOR, indoor_c, t, config);
    ctl_observe(state, CTL_SIGNAL_T_OUTDOOR, OUTDOOR_C, t, config);
    ctl_observe(state, CTL_SIGNAL_P_PRODUCED, produced_w, t, config);
}

/* Fresh readings at every step from start_s, so that each interval up to end_s integrates. */
static void run_steps(ctl_state *state, double indoor_c, double start_s, double end_s,
                      const ctl_config *config)
{
    const double produced_w = balancing_produced_w(indoor_c, config);
    double t;
    for (t = start_s; t <= end_s; t += STEP_S) {
        observe_all(state, indoor_c, produced_w, t, config);
    }
}

static void config_with_gain(ctl_config *config, double gain)
{
    ctl_config_defaults(config);
    config->integral_w_per_kh = gain;
}

/* Two hours at the offset with and without the integral; returns the integral at the end. */
static double integral_after_two_hours(double offset_k)
{
    const double indoor_c = 22.0 + offset_k;
    ctl_config with;
    ctl_config without;
    ctl_state learning;
    ctl_state fixed;
    ctl_decision learned;
    ctl_decision plain;

    config_with_gain(&with, 20.0);
    config_with_gain(&without, 0.0);
    ctl_state_init(&learning);
    ctl_state_init(&fixed);
    run_steps(&learning, indoor_c, 0.0, 7200.0, &with);
    run_steps(&fixed, indoor_c, 0.0, 7200.0, &without);
    ctl_evaluate(&learning, 7200.0, &with, &learned);
    ctl_evaluate(&fixed, 7200.0, &without, &plain);

    CHECK_TRUE(learned.has_inputs && plain.has_inputs);
    CHECK_DOUBLE(learned.balance.p_integral_w, learning.p_integral_w);
    CHECK_DOUBLE(plain.balance.p_integral_w, 0.0);
    CHECK_DOUBLE(learned.balance.p_net_w, plain.balance.p_net_w + learning.p_integral_w);
    CHECK_DOUBLE(learned.balance.p_model_w + learned.balance.p_trim_w +
                     learned.balance.p_integral_w,
                 learned.balance.p_net_w);
    CHECK_TRUE(!learned.clamped);
    return learning.p_integral_w;
}

static void test_learns_surplus_and_deficit(void)
{
    CHECK_NEAR(integral_after_two_hours(0.5), 20.0);
    CHECK_NEAR(integral_after_two_hours(-0.5), -20.0);
}

/* The ledger books the integral like every other term, and the total adds it. */
static void test_ledger(void)
{
    ctl_config config;
    ctl_state state;
    ctl_ledger ledger;

    config_with_gain(&config, 20.0);
    ctl_state_init(&state);
    run_steps(&state, 22.5, 0.0, 7200.0, &config);
    CHECK_TRUE(state.ledger.integral_wh > 0.0);
    CHECK_TRUE(state.ledger.integral_wh < 20.0 * 2.0);
    CHECK_DOUBLE(ctl_ledger_total_wh(&state.ledger),
                 state.ledger.produced_wh + state.ledger.solar_wh + state.ledger.internal_wh +
                     state.ledger.transmission_wh + state.ledger.trim_wh +
                     state.ledger.integral_wh);
    ctl_ledger_init(&ledger);
    CHECK_DOUBLE(ledger.integral_wh, 0.0);
}

static void test_limit(void)
{
    ctl_config config;
    ctl_state warm;
    ctl_state cold;

    config_with_gain(&config, 100.0);
    config.integral_limit_w = 30.0;
    ctl_state_init(&warm);
    ctl_state_init(&cold);
    run_steps(&warm, 23.0, 0.0, 3600.0, &config);
    run_steps(&cold, 21.0, 0.0, 3600.0, &config);
    CHECK_DOUBLE(warm.p_integral_w, 30.0);
    CHECK_DOUBLE(cold.p_integral_w, -30.0);

    config.integral_limit_w = 10.0;
    run_steps(&warm, 23.0, 3600.0 + STEP_S, 3600.0 + STEP_S, &config);
    CHECK_DOUBLE(warm.p_integral_w, 10.0);
}

/* With the gain at 0 the integral freezes, and only the reset clears it. */
static void test_freeze_and_reset(void)
{
    ctl_config config;
    ctl_state state;
    double frozen;

    config_with_gain(&config, 20.0);
    ctl_state_init(&state);
    run_steps(&state, 22.5, 0.0, 3600.0, &config);
    frozen = state.p_integral_w;
    CHECK_TRUE(frozen > 0.0);
    config.integral_w_per_kh = 0.0;
    run_steps(&state, 22.5, 3600.0 + STEP_S, 7200.0, &config);
    CHECK_DOUBLE(state.p_integral_w, frozen);

    ctl_reset_integral(&state, 7200.0 + STEP_S, &config);
    CHECK_DOUBLE(state.p_integral_w, 0.0);
    CHECK_DOUBLE(state.t_last, 7200.0 + STEP_S);
    ctl_reset_integral(&state, NAN, &config);
    CHECK_DOUBLE(state.p_integral_w, 0.0);
}

typedef enum { HOLD_STALE, HOLD_LONG_STEP, HOLD_OUTSIDE_TRIM, HOLD_CLAMPED } hold_case;

/* Leaves the state at the start of an interval of the case, with a learned integral. */
static void prepare_hold(ctl_state *state, hold_case which, double *t, const ctl_config *config)
{
    double dt_s;
    run_steps(state, 22.5, 0.0, 1200.0, config);
    *t = 1200.0;
    switch (which) {
    case HOLD_STALE:
        /* The readings of 1200 are fresh up to 1800, so the interval from 1860 starts stale. */
        for (*t = 1200.0 + STEP_S; *t <= 1860.0; *t += STEP_S) {
            CHECK_TRUE(ctl_advance(state, *t, config, &dt_s));
        }
        *t = 1860.0;
        break;
    case HOLD_LONG_STEP:
        break;
    case HOLD_OUTSIDE_TRIM:
        ctl_observe(state, CTL_SIGNAL_T_INDOOR, 27.0, *t, config);
        break;
    case HOLD_CLAMPED:
        ctl_observe(state, CTL_SIGNAL_P_PRODUCED, -1e6, *t, config);
        *t += STEP_S;
        ctl_observe(state, CTL_SIGNAL_P_PRODUCED, balancing_produced_w(22.5, config), *t,
                    config);
        CHECK_TRUE(state->clamped);
        break;
    }
}

static void test_holds(void)
{
    static const struct {
        hold_case which;
        double step_s;
        bool integrates;
    } rows[] = {
        {HOLD_STALE, STEP_S, false},
        {HOLD_LONG_STEP, 400.0, false},
        {HOLD_OUTSIDE_TRIM, STEP_S, true},
        {HOLD_CLAMPED, STEP_S, true},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_config config;
        ctl_state state;
        double t;
        double dt_s;
        double before;
        config_with_gain(&config, 20.0);
        ctl_state_init(&state);
        prepare_hold(&state, rows[i].which, &t, &config);
        before = state.p_integral_w;
        CHECK_TRUE(before > 0.0);
        CHECK_TRUE(ctl_advance(&state, t + rows[i].step_s, &config, &dt_s) == rows[i].integrates);
        CHECK_DOUBLE(dt_s, rows[i].step_s);
        CHECK_DOUBLE(state.p_integral_w, before);
    }
}

/* Without a trim limit every deviation is one the integral may learn from. */
static void test_learns_without_trim_limit(void)
{
    ctl_config config;
    ctl_state state;
    config_with_gain(&config, 20.0);
    config.trim_limit_enabled = false;
    ctl_state_init(&state);
    run_steps(&state, 22.5, 0.0, 3600.0, &config);
    CHECK_NEAR(state.p_integral_w, 10.0);
}

static void test_config_rules(void)
{
    static const struct {
        double gain;
        double limit;
        uint32_t problems;
    } rows[] = {
        {0.0, 300.0, 0u},
        {200.0, 300.0, 0u},
        {0.0, 1.0, 0u},
        {0.0, 2000.0, 0u},
        {-1.0, 300.0, CTL_CONFIG_BAD_INTEGRAL_GAIN},
        {201.0, 300.0, CTL_CONFIG_BAD_INTEGRAL_GAIN},
        {NAN, 300.0, CTL_CONFIG_BAD_INTEGRAL_GAIN},
        {INFINITY, 300.0, CTL_CONFIG_BAD_INTEGRAL_GAIN},
        {0.0, 0.0, CTL_CONFIG_BAD_INTEGRAL_LIMIT},
        {0.0, 2001.0, CTL_CONFIG_BAD_INTEGRAL_LIMIT},
        {0.0, NAN, CTL_CONFIG_BAD_INTEGRAL_LIMIT},
        {0.0, INFINITY, CTL_CONFIG_BAD_INTEGRAL_LIMIT},
    };
    ctl_config config;
    size_t i;
    ctl_config_defaults(&config);
    CHECK_DOUBLE(config.integral_w_per_kh, 0.0);
    CHECK_DOUBLE(config.integral_limit_w, 300.0);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_config_defaults(&config);
        config.integral_w_per_kh = rows[i].gain;
        config.integral_limit_w = rows[i].limit;
        CHECK_UINT(ctl_config_validate(&config), rows[i].problems);
    }
}

/* The reset is a command, not a setting: the dispatch names it on its own. */
static void test_reset_topic(void)
{
    ctl_settings settings;
    ctl_topic topic;
    char value[CTL_SETTINGS_VALUE_MAX];
    ctl_settings_defaults(&settings);
    topic = ctl_dispatch_topic(&settings, "panasonic_heat_pump",
                               "panasonic_heat_pump/control/set/integral_reset");
    CHECK_UINT((uint32_t)topic.kind, (uint32_t)CTL_TOPIC_INTEGRAL_RESET);
    CHECK_TEXT(topic.name, "");
    topic = ctl_dispatch_topic(&settings, "panasonic_heat_pump",
                               "panasonic_heat_pump/control/set/integral_w_per_kh");
    CHECK_UINT((uint32_t)topic.kind, (uint32_t)CTL_TOPIC_SETTING);
    CHECK_UINT((uint32_t)ctl_settings_set(&settings, "integral_reset", "1"),
               (uint32_t)CTL_SETTING_UNKNOWN);
    CHECK_TRUE(!ctl_settings_get(&settings, "integral_reset", value, sizeof value));
}

/* ---- The golden traces with the integral off but a different limit --------------------- */

typedef struct {
    ctl_config config;
    ctl_state state;
} twin;

static bool parse_signal(const char *text, ctl_signal *signal)
{
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        if (strcmp(text, ctl_signal_name((ctl_signal)index)) == 0) {
            *signal = (ctl_signal)index;
            return true;
        }
    }
    return false;
}

static uint8_t parse_demands(char *text)
{
    uint8_t demands = 0u;
    char *name = strtok(text, ",");
    int index;
    while (name != NULL) {
        for (index = 0; index <= (int)CTL_DEMAND_NONE; index++) {
            if (strcmp(name, ctl_demand_name((ctl_demand)index)) == 0) {
                demands |= (uint8_t)CTL_DEMAND_BIT(index);
            }
        }
        name = strtok(NULL, ",");
    }
    return demands;
}

static bool same_bits(double a, double b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

static bool same_ledger(const ctl_ledger *a, const ctl_ledger *b)
{
    return same_bits(a->produced_wh, b->produced_wh) && same_bits(a->solar_wh, b->solar_wh) &&
           same_bits(a->internal_wh, b->internal_wh) &&
           same_bits(a->transmission_wh, b->transmission_wh) &&
           same_bits(a->trim_wh, b->trim_wh) && same_bits(a->integral_wh, b->integral_wh);
}

static bool same_state(const ctl_state *a, const ctl_state *b)
{
    return a->phase == b->phase && a->demand == b->demand && a->allowed == b->allowed &&
           same_bits(a->energy_wh, b->energy_wh) && same_bits(a->t_last, b->t_last) &&
           same_bits(a->integrated_s, b->integrated_s) &&
           same_bits(a->skipped_s, b->skipped_s) && a->clamped == b->clamped &&
           same_bits(a->p_integral_w, b->p_integral_w) && same_ledger(&a->ledger, &b->ledger);
}

static bool same_decision(const ctl_decision *a, const ctl_decision *b)
{
    return a->demand == b->demand && a->reason == b->reason && a->phase == b->phase &&
           a->has_inputs == b->has_inputs && same_bits(a->energy_wh, b->energy_wh) &&
           same_bits(a->dt_s, b->dt_s) && same_bits(a->balance.p_net_w, b->balance.p_net_w) &&
           same_bits(a->balance.p_integral_w, b->balance.p_integral_w) &&
           a->unhealthy == b->unhealthy && a->substituted == b->substituted;
}

/* Makes the call of one trace line on the twin; returns false for a line that is no call. */
static bool make_call(twin *run, char **fields, int count, ctl_decision *decision)
{
    const double t = count > 1 ? strtod(fields[1], NULL) : 0.0;
    double dt_s;
    ctl_signal signal;
    if (strcmp(fields[0], "observe") == 0 && count >= 4 && parse_signal(fields[2], &signal)) {
        ctl_observe(&run->state, signal, strtod(fields[3], NULL), t, &run->config);
    } else if (strcmp(fields[0], "advance") == 0) {
        (void)ctl_advance(&run->state, t, &run->config, &dt_s);
    } else if (strcmp(fields[0], "allow") == 0 && count >= 3) {
        ctl_set_allowed(&run->state, parse_demands(fields[2]), t, &run->config);
    } else if (strcmp(fields[0], "evaluate") == 0) {
        ctl_evaluate(&run->state, t, &run->config, decision);
    } else {
        return false;
    }
    return true;
}

/* Splits the call's part of a line, before its '|', into fields; the copy is cut in place. */
static int call_fields(char *line, char **fields)
{
    int count = 0;
    char *bar = strchr(line, '|');
    char *field;
    if (bar != NULL) {
        *bar = '\0';
    }
    field = strtok(line, " \n");
    while (field != NULL && count < FIELDS_MAX) {
        fields[count++] = field;
        field = strtok(NULL, " \n");
    }
    return count;
}

/* Replays the calls of the trace on two controllers that differ only in integral_limit_w and
 * returns the number of calls after which they differ. */
static long replay_twins(const char *path, long *calls)
{
    static twin plain;
    static twin limited;
    char line[LINE_MAX_LENGTH];
    char copy[LINE_MAX_LENGTH];
    long differences = 0;
    FILE *file = fopen(path, "r");
    CHECK_TRUE(file != NULL);
    if (file == NULL) {
        return 1;
    }
    ctl_config_defaults(&plain.config);
    limited.config = plain.config;
    limited.config.integral_limit_w = 50.0;
    ctl_state_init(&plain.state);
    ctl_state_init(&limited.state);
    while (fgets(line, sizeof line, file) != NULL) {
        char *fields[FIELDS_MAX];
        ctl_decision first;
        ctl_decision second;
        int count;
        bool called;
        memcpy(copy, line, sizeof copy);
        count = call_fields(line, fields);
        if (count == 0 || !make_call(&plain, fields, count, &first)) {
            continue;
        }
        count = call_fields(copy, fields);
        called = make_call(&limited, fields, count, &second);
        (*calls)++;
        if (!called || !same_state(&plain.state, &limited.state) ||
            (strcmp(fields[0], "evaluate") == 0 && !same_decision(&first, &second))) {
            differences++;
        }
    }
    fclose(file);
    return differences;
}

static void test_traces_with_other_limit(void)
{
    static const char *const names[] = {
        "barbarastr_march", "cold_snap_undersized", "dhw_interruptions", "edge_cases",
        "flaky_transport", "indoor_sensor_outage", "masked_cooling", "model_mismatch",
        "shoulder_season", "steady_winter", "summer_day", "underreported_solar", "winter_day",
    };
    const char *folder = getenv("CTL_TRACES");
    size_t i;
    CHECK_TRUE(folder != NULL);
    if (folder == NULL) {
        return;
    }
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        char path[PATH_MAX_LENGTH];
        long calls = 0;
        snprintf(path, sizeof path, "%s/%s.trace", folder, names[i]);
        CHECK_UINT((uint32_t)replay_twins(path, &calls), 0u);
        CHECK_TRUE(calls > 0);
    }
}

int main(void)
{
    test_learns_surplus_and_deficit();
    test_ledger();
    test_limit();
    test_freeze_and_reset();
    test_holds();
    test_learns_without_trim_limit();
    test_config_rules();
    test_reset_topic();
    test_traces_with_other_limit();
    return check_result();
}
