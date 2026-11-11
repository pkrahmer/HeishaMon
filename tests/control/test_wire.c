#include "check.h"

#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "core/ctl_relay.h"
#include "core/ctl_types.h"
#include "wire/ctl_wire.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEXT_MAX 300

static uint64_t bits_of(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void request(ctl_wire_session *session, const char *line, char *reply)
{
    ctl_wire_handle(session, line, reply, CTL_WIRE_REPLY_SIZE);
}

/* The hexadecimal text of a double, as the protocol spells it; one of two buffers in turn, so
 * that two can appear in one formatted request. */
static const char *hex(double value)
{
    static char texts[2][17];
    static int next;
    char *text = texts[next];
    next = 1 - next;
    ctl_wire_format_double(value, text);
    return text;
}

static void begun_session(ctl_wire_session *session)
{
    char reply[CTL_WIRE_REPLY_SIZE];
    ctl_wire_init(session);
    request(session, "B", reply);
    CHECK_TEXT(reply, "ok");
}

static void test_double_texts(void)
{
    static const struct {
        double value;
        const char *text;
    } rows[] = {
        {0.0, "0000000000000000"},
        {-0.0, "8000000000000000"},
        {1.0, "3ff0000000000000"},
        {-2.5, "c004000000000000"},
        {INFINITY, "7ff0000000000000"},
        {1.0000000000000002, "3ff0000000000001"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char text[17];
        double parsed = 42.0;
        ctl_wire_format_double(rows[i].value, text);
        CHECK_TEXT(text, rows[i].text);
        CHECK_TRUE(ctl_wire_parse_double(rows[i].text, &parsed));
        CHECK_TRUE(bits_of(parsed) == bits_of(rows[i].value));
    }
    CHECK_TRUE(nextafter(1.0, 2.0) == 1.0000000000000002);
}

static void test_double_nan(void)
{
    double parsed = 0.0;
    CHECK_TRUE(ctl_wire_parse_double("7ff8000000000000", &parsed));
    CHECK_TRUE(isnan(parsed));
}

static void test_double_refused(void)
{
    static const char *const rows[] = {
        "3ff000000000000", "3ff00000000000000", "3FF0000000000000", "3ff000000000000g", ""};
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        double parsed = 42.0;
        CHECK_TRUE(!ctl_wire_parse_double(rows[i], &parsed));
        CHECK_DOUBLE(parsed, 42.0);
    }
}

static void test_session_before_begin(void)
{
    static const char *const calls[] = {
        "O 0000000000000000 0 4035000000000000", "A 0000000000000000", "M 0000000000000000 7",
        "E 0000000000000000"};
    ctl_wire_session session;
    char reply[CTL_WIRE_REPLY_SIZE];
    size_t i;
    ctl_wire_init(&session);
    for (i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        request(&session, calls[i], reply);
        CHECK_TEXT(reply, "error");
    }
    CHECK_UINT(session.calls, 0);
    request(&session, "R", reply);
    CHECK_TEXT(reply, "ok");
    request(&session, "B", reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(session.begun);
}

static void test_session_bad_config(void)
{
    ctl_wire_session session;
    char reply[CTL_WIRE_REPLY_SIZE];
    ctl_wire_init(&session);
    request(&session, "C hysteresis_wh 0000000000000000", reply);
    CHECK_TEXT(reply, "ok");
    request(&session, "B", reply);
    CHECK_TEXT(reply, "bad 00000008");
    CHECK_TRUE(!session.begun);
    request(&session, "A 3ff0000000000000", reply);
    CHECK_TEXT(reply, "error");
}

static void test_session_reset(void)
{
    ctl_wire_session session;
    ctl_config defaults;
    char reply[CTL_WIRE_REPLY_SIZE];
    begun_session(&session);
    request(&session, "A 3ff0000000000000", reply);
    CHECK_UINT(session.calls, 1);
    request(&session, "C hysteresis_wh 0000000000000000", reply);
    request(&session, "R", reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(!session.begun);
    CHECK_UINT(session.calls, 0);
    ctl_config_defaults(&defaults);
    CHECK_DOUBLE(session.config.hysteresis_wh, defaults.hysteresis_wh);
    CHECK_TRUE(!session.state.has_t_last);
}

static void check_same_config(const ctl_config *actual, const ctl_config *expected)
{
    int i;
    CHECK_DOUBLE(actual->desired_indoor_c, expected->desired_indoor_c);
    CHECK_DOUBLE(actual->transmission_w_per_k, expected->transmission_w_per_k);
    CHECK_DOUBLE(actual->adjustment_w_per_k, expected->adjustment_w_per_k);
    CHECK_DOUBLE(actual->hysteresis_wh, expected->hysteresis_wh);
    CHECK_TRUE(actual->trim_limit_enabled == expected->trim_limit_enabled);
    CHECK_DOUBLE(actual->trim_limit_k, expected->trim_limit_k);
    CHECK_DOUBLE(actual->solar_gain_factor, expected->solar_gain_factor);
    CHECK_DOUBLE(actual->energy_clamp_factor, expected->energy_clamp_factor);
    CHECK_DOUBLE(actual->max_step_s, expected->max_step_s);
    CHECK_DOUBLE(actual->tick_interval_s, expected->tick_interval_s);
    CHECK_DOUBLE(actual->integration_interval_s, expected->integration_interval_s);
    CHECK_DOUBLE(actual->input_hold_s, expected->input_hold_s);
    for (i = 0; i < CTL_SIGNAL_COUNT; i++) {
        CHECK_DOUBLE(actual->max_age_s[i], expected->max_age_s[i]);
        CHECK_DOUBLE(actual->fallback_w[i], expected->fallback_w[i]);
    }
}

static void test_config_fields(void)
{
    static const struct {
        const char *name;
        double value;
    } rows[] = {
        {"desired_indoor_c", 20.5},      {"transmission_w_per_k", 160.0},
        {"adjustment_w_per_k", 800.0},   {"hysteresis_wh", 1000.0},
        {"trim_limit_k", 2.5},           {"solar_gain_factor", 0.03},
        {"energy_clamp_factor", 3.0},    {"max_step_s", 250.0},
        {"tick_interval_s", 30.0},       {"integration_interval_s", 5.0},
        {"input_hold_s", 800.0},         {"max_age_s.0", 100.0},
        {"max_age_s.1", 101.0},          {"max_age_s.2", 102.0},
        {"max_age_s.3", 103.0},          {"max_age_s.4", 104.0},
        {"fallback_w.0", 10.0},          {"fallback_w.1", 11.0},
        {"fallback_w.2", 12.0},          {"fallback_w.3", 13.0},
        {"fallback_w.4", 14.0},
    };
    ctl_wire_session session;
    ctl_config expected;
    char line[CTL_WIRE_LINE_MAX + 1];
    char reply[CTL_WIRE_REPLY_SIZE];
    size_t i;
    int signal;
    ctl_wire_init(&session);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        snprintf(line, sizeof line, "C %s %s", rows[i].name, hex(rows[i].value));
        request(&session, line, reply);
        CHECK_TEXT(reply, "ok");
    }
    ctl_config_defaults(&expected);
    expected.desired_indoor_c = 20.5;
    expected.transmission_w_per_k = 160.0;
    expected.adjustment_w_per_k = 800.0;
    expected.hysteresis_wh = 1000.0;
    expected.trim_limit_k = 2.5;
    expected.solar_gain_factor = 0.03;
    expected.energy_clamp_factor = 3.0;
    expected.max_step_s = 250.0;
    expected.tick_interval_s = 30.0;
    expected.integration_interval_s = 5.0;
    expected.input_hold_s = 800.0;
    for (signal = 0; signal < CTL_SIGNAL_COUNT; signal++) {
        expected.max_age_s[signal] = 100.0 + signal;
        expected.fallback_w[signal] = 10.0 + signal;
    }
    check_same_config(&session.config, &expected);

    request(&session, "C trim_limit_enabled 0", reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(!session.config.trim_limit_enabled);
    request(&session, "C trim_limit_enabled 1", reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(session.config.trim_limit_enabled);
}

static void test_config_refused(void)
{
    static const char *const rows[] = {
        "C unknown_field 3ff0000000000000", "C max_age_s.5 3ff0000000000000",
        "C trim_limit_enabled 2", "C desired_indoor_c 3ff000000000000"};
    ctl_wire_session session;
    ctl_config before;
    char reply[CTL_WIRE_REPLY_SIZE];
    size_t i;
    ctl_wire_init(&session);
    request(&session, "C trim_limit_enabled 0", reply);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        memcpy(&before, &session.config, sizeof before);
        request(&session, rows[i], reply);
        CHECK_TEXT(reply, "error");
        CHECK_TRUE(memcmp(&before, &session.config, sizeof before) == 0);
    }
}

static void check_config_after_begin(void)
{
    ctl_wire_session session;
    char reply[CTL_WIRE_REPLY_SIZE];
    begun_session(&session);
    request(&session, "C desired_indoor_c 4034000000000000", reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(session.begun);
    CHECK_DOUBLE(session.config.desired_indoor_c, 20.0);
}

/* The lines the protocol should write, formatted here from the core's own results, so that a
 * reply is compared with what the core computed and not with what the protocol computed. */
static void state_line(char *text, const ctl_state *state)
{
    char energy[17], integrated[17], skipped[17];
    ctl_wire_format_double(state->energy_wh, energy);
    ctl_wire_format_double(state->integrated_s, integrated);
    ctl_wire_format_double(state->skipped_s, skipped);
    snprintf(text, TEXT_MAX, "%s %s %s %d", energy, integrated, skipped, state->clamped ? 1 : 0);
}

static void decision_line(char *text, const ctl_decision *decision)
{
    char numbers[10][17];
    char p_net[17] = "-";
    const double values[10] = {
        decision->energy_wh, decision->dt_s, decision->integrated_s, decision->skipped_s,
        decision->ledger.produced_wh, decision->ledger.solar_wh, decision->ledger.internal_wh,
        decision->ledger.transmission_wh, decision->ledger.trim_wh, 0.0};
    int i;
    for (i = 0; i < 9; i++) {
        ctl_wire_format_double(values[i], numbers[i]);
    }
    if (decision->has_inputs) {
        ctl_wire_format_double(decision->balance.p_net_w, p_net);
    }
    snprintf(text, TEXT_MAX, "%d %d %d %s %s %s %s %d %s %02x %02x %s %s %s %s %s",
             (int)decision->demand, (int)decision->reason, (int)decision->phase, numbers[0],
             numbers[1], numbers[2], numbers[3], decision->clamped ? 1 : 0, p_net,
             (unsigned)decision->unhealthy, (unsigned)decision->substituted, numbers[4],
             numbers[5], numbers[6], numbers[7], numbers[8]);
}

typedef struct {
    ctl_wire_session session;
    ctl_state state;
    ctl_config config;
    int with_inputs;
    int without_inputs;
} twin_run;

static void twin_observe(twin_run *run, double t, ctl_signal signal, double value)
{
    char line[CTL_WIRE_LINE_MAX + 1], reply[CTL_WIRE_REPLY_SIZE], expected[TEXT_MAX];
    snprintf(line, sizeof line, "O %s %d %s", hex(t), (int)signal, hex(value));
    request(&run->session, line, reply);
    ctl_observe(&run->state, signal, value, t, &run->config);
    state_line(expected, &run->state);
    CHECK_TEXT(reply, expected);
}

static void twin_advance(twin_run *run, double t)
{
    char line[CTL_WIRE_LINE_MAX + 1], reply[CTL_WIRE_REPLY_SIZE], expected[TEXT_MAX];
    double dt_s;
    snprintf(line, sizeof line, "A %s", hex(t));
    request(&run->session, line, reply);
    (void)ctl_advance(&run->state, t, &run->config, &dt_s);
    state_line(expected, &run->state);
    CHECK_TEXT(reply, expected);
}

static void twin_allow(twin_run *run, double t, uint8_t allowed)
{
    char line[CTL_WIRE_LINE_MAX + 1], reply[CTL_WIRE_REPLY_SIZE], expected[TEXT_MAX];
    snprintf(line, sizeof line, "M %s %x", hex(t), (unsigned)allowed);
    request(&run->session, line, reply);
    ctl_set_allowed(&run->state, allowed, t, &run->config);
    state_line(expected, &run->state);
    CHECK_TEXT(reply, expected);
}

static void twin_evaluate(twin_run *run, double t)
{
    char line[CTL_WIRE_LINE_MAX + 1], reply[CTL_WIRE_REPLY_SIZE], expected[TEXT_MAX];
    ctl_decision decision;
    snprintf(line, sizeof line, "E %s", hex(t));
    request(&run->session, line, reply);
    ctl_evaluate(&run->state, t, &run->config, &decision);
    decision_line(expected, &decision);
    CHECK_TEXT(reply, expected);
    if (decision.has_inputs) {
        run->with_inputs++;
    } else {
        run->without_inputs++;
    }
}

static void test_replies_equal_core(void)
{
    static twin_run run;
    char reply[CTL_WIRE_REPLY_SIZE];
    begun_session(&run.session);
    ctl_config_defaults(&run.config);
    ctl_state_init(&run.state);
    twin_evaluate(&run, 0.0);
    twin_observe(&run, 0.0, CTL_SIGNAL_T_INDOOR, 21.0);
    twin_observe(&run, 0.0, CTL_SIGNAL_T_OUTDOOR, 5.0);
    twin_observe(&run, 0.0, CTL_SIGNAL_P_PRODUCED, 0.0);
    twin_advance(&run, 10.0);
    twin_evaluate(&run, 60.0);
    twin_allow(&run, 70.0, CTL_DEMAND_BIT(CTL_DEMAND_HEATING));
    twin_advance(&run, 120.0);
    twin_evaluate(&run, 180.0);
    twin_allow(&run, 200.0, CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_COOLING));
    twin_observe(&run, 250.0, CTL_SIGNAL_P_SOLAR, 300.0);
    twin_evaluate(&run, 300.0);
    /* Every required reading is older than its max_age_s of 600 s now. */
    twin_advance(&run, 900.0);
    twin_evaluate(&run, 1000.0);
    twin_evaluate(&run, NAN);
    CHECK_TRUE(run.with_inputs > 0);
    CHECK_TRUE(run.without_inputs > 0);
    CHECK_UINT(run.session.calls, 15);
    request(&run.session, "B", reply);
    CHECK_UINT(run.session.calls, 0);
}

static void test_malformed(void)
{
    static char too_long[CTL_WIRE_LINE_MAX + 2];
    static const char *rows[] = {
        "X",
        "A",
        "A 3ff0000000000000 1",
        "O 3ff0000000000000 5 3ff0000000000000",
        "M 3ff0000000000000 8",
        "M 3ff0000000000000 g",
        "E 3FF0000000000000",
        too_long,
        "A  3ff0000000000000",
        "",
        " A 3ff0000000000000",
        "A 3ff0000000000000 ",
        "S",
        "B 1",
        "R 1",
        "C hysteresis_wh",
        "M 3ff0000000000000 07",
        "O 3ff0000000000000 00 3ff0000000000000",
    };
    static ctl_wire_session session;
    static ctl_wire_session before;
    char reply[CTL_WIRE_REPLY_SIZE];
    size_t i;
    memset(too_long, 'A', CTL_WIRE_LINE_MAX + 1);
    begun_session(&session);
    request(&session, "O 0000000000000000 0 4035000000000000", reply);
    request(&session, "A 4024000000000000", reply);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        memcpy(&before, &session, sizeof before);
        request(&session, rows[i], reply);
        CHECK_TEXT(reply, "error");
        CHECK_TRUE(memcmp(&before, &session, sizeof before) == 0);
    }
}

static void test_line_length(void)
{
    static char longest[CTL_WIRE_LINE_MAX + 1];
    static char longest_with_return[CTL_WIRE_LINE_MAX + 2];
    static ctl_wire_session session;
    char reply[CTL_WIRE_REPLY_SIZE];
    begun_session(&session);
    /* No request of the protocol is this long, so a line of the full length is refused for
     * what it says; the carriage return after it does not count against the limit. */
    memset(longest, 'A', CTL_WIRE_LINE_MAX);
    request(&session, longest, reply);
    CHECK_TEXT(reply, "error");
    memset(longest_with_return, 'A', CTL_WIRE_LINE_MAX);
    longest_with_return[CTL_WIRE_LINE_MAX] = '\r';
    request(&session, longest_with_return, reply);
    CHECK_TEXT(reply, "error");
}

static void test_carriage_return(void)
{
    ctl_wire_session session;
    char with_return[CTL_WIRE_REPLY_SIZE];
    char without_return[CTL_WIRE_REPLY_SIZE];
    ctl_wire_init(&session);
    request(&session, "R\r", with_return);
    CHECK_TEXT(with_return, "ok");
    request(&session, "B\r", with_return);
    CHECK_TEXT(with_return, "ok");
    request(&session, "A 4024000000000000\r", with_return);
    request(&session, "B", without_return);
    request(&session, "A 4024000000000000", without_return);
    CHECK_TEXT(with_return, without_return);
    request(&session, "A 4034000000000000\r\r", with_return);
    CHECK_TEXT(with_return, "error");
}

static void test_reply_cut_to_size(void)
{
    ctl_wire_session session;
    char reply[4] = "xyz";
    ctl_wire_init(&session);
    ctl_wire_handle(&session, "X", reply, sizeof reply);
    CHECK_TEXT(reply, "err");
    ctl_wire_handle(&session, "X", reply, 0);
    CHECK_TEXT(reply, "err");
}

int main(void)
{
    test_double_texts();
    test_double_nan();
    test_double_refused();
    test_session_before_begin();
    test_session_bad_config();
    test_session_reset();
    test_config_fields();
    test_config_refused();
    check_config_after_begin();
    test_replies_equal_core();
    test_malformed();
    test_line_length();
    test_carriage_return();
    test_reply_cut_to_size();
    return check_result();
}
