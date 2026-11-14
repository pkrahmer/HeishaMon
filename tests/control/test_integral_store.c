#include "check.h"

#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "core/ctl_types.h"
#include "glue/ctl_integral_store.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define HOUR_S 3600.0
#define MINUTE_S 60.0

/* ---- When the file is written --------------------------------------------------------- */

typedef struct {
    double integral_w;
    double saved_w;
    double since_write_s;
    bool reset;
    bool due;
} due_row;

static void test_write_rule(void)
{
    static const due_row rows[] = {
        {110.0, 100.0, 6.0 * HOUR_S, false, true},
        {50.0, 0.0, 5.0 * HOUR_S + 59.0 * MINUTE_S, false, false},
        {109.9, 100.0, 7.0 * HOUR_S, false, false},
        {0.0, 0.0, 0.0, true, true},
        {-110.0, -100.0, 6.0 * HOUR_S, false, true},
        {90.0, 100.0, 6.0 * HOUR_S, false, true},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const due_row *row = &rows[i];
        CHECK_UINT(ctl_integral_store_due(row->integral_w, row->saved_w, row->since_write_s,
                                          row->reset),
                   row->due);
    }
}

/* ---- Reading the file ----------------------------------------------------------------- */

typedef struct {
    const char *text;
    bool parsed;
    double value_w;
} parse_row;

static void test_parse(void)
{
    static const parse_row rows[] = {
        {"42.5", true, 42.5},
        {"-12", true, -12.0},
        {"", false, 0.0},
        {"abc", false, 0.0},
        {"nan", false, 0.0},
        {"inf", false, 0.0},
        {"1e999", false, 0.0},
        {"-300.000\n", true, -300.0},
        {" 42", true, 42.0},
        {"{\"v\":5}", false, 0.0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        double value_w = 7.0;
        bool parsed = ctl_integral_store_parse(rows[i].text, strlen(rows[i].text), &value_w);
        CHECK_UINT(parsed, rows[i].parsed);
        CHECK_DOUBLE(value_w, rows[i].parsed ? rows[i].value_w : 7.0);
    }
}

/* ---- The start value at boot ---------------------------------------------------------- */

typedef struct {
    const char *text;
    double start_w;
} start_row;

static void test_start(void)
{
    static const start_row rows[] = {
        {"120", 120.0}, {"500", 300.0}, {"-500", -300.0}, {"abc", 0.0}, {"", 0.0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_DOUBLE(ctl_integral_store_start(rows[i].text, strlen(rows[i].text), 300.0),
                     rows[i].start_w);
    }
}

/* The produced power that makes the model and the trim cancel at this indoor temperature, so
 * that the energy stays far from its bounds and every interval may teach the integral. */
static double balancing_produced_w(double indoor_c, double outdoor_c, const ctl_config *config)
{
    const double p_transmission = config->transmission_w_per_k * (outdoor_c - indoor_c);
    const double p_trim = config->adjustment_w_per_k * (indoor_c - config->desired_indoor_c);
    return -(config->fallback_w[CTL_SIGNAL_P_INTERNAL] + p_transmission + p_trim);
}

/* Starts at the stored 120 W and runs an hour of fresh readings 1 K above the target, in steps
 * that integrate, and returns the integral at the end. */
static double integral_after_an_hour(double gain)
{
    const double indoor_c = 23.0;
    const double outdoor_c = 5.0;
    ctl_config config;
    ctl_state state;
    double produced_w;
    double t;
    double dt_s;
    ctl_config_defaults(&config);
    config.integral_w_per_kh = gain;
    produced_w = balancing_produced_w(indoor_c, outdoor_c, &config);
    ctl_state_init(&state);
    state.p_integral_w = ctl_integral_store_start("120", 3, config.integral_limit_w);
    for (t = 0.0; t < HOUR_S; t += MINUTE_S) {
        ctl_observe(&state, CTL_SIGNAL_T_INDOOR, indoor_c, t, &config);
        ctl_observe(&state, CTL_SIGNAL_T_OUTDOOR, outdoor_c, t, &config);
        ctl_observe(&state, CTL_SIGNAL_P_PRODUCED, produced_w, t, &config);
        CHECK_TRUE(ctl_advance(&state, t + MINUTE_S, &config, &dt_s));
    }
    return state.p_integral_w;
}

/* With the gain at 0 the loaded value is held, not cleared; the same hour with a gain moves it,
 * so the hour does reach the integral. */
static void test_start_is_held_without_gain(void)
{
    CHECK_DOUBLE(integral_after_an_hour(0.0), 120.0);
    CHECK_TRUE(fabs(integral_after_an_hour(20.0) - 120.0) > 1.0);
}

/* ---- What the glue remembers ---------------------------------------------------------- */

/* A missing or refused file starts at 0, and the load itself asks for no write. */
static void test_load_writes_nothing(void)
{
    ctl_integral_store store;
    CHECK_DOUBLE(ctl_integral_store_load(&store, NULL, 0, 300.0, 50.0), 0.0);
    CHECK_DOUBLE(store.saved_w, 0.0);
    CHECK_TRUE(!ctl_integral_store_begin(&store, 0.0, 50.0 + 7.0 * HOUR_S, false));
    CHECK_DOUBLE(ctl_integral_store_load(&store, "abc", 3, 300.0, 50.0), 0.0);
    CHECK_TRUE(!ctl_integral_store_begin(&store, 0.0, 50.0 + 7.0 * HOUR_S, false));
    CHECK_DOUBLE(ctl_integral_store_load(&store, "500", 3, 300.0, 50.0), 300.0);
    CHECK_DOUBLE(store.saved_w, 300.0);
    CHECK_TRUE(!ctl_integral_store_begin(&store, 300.0, 50.0 + 7.0 * HOUR_S, false));
    /* The 6 hours count from the boot, not from 0. */
    CHECK_TRUE(!ctl_integral_store_begin(&store, 250.0, 6.0 * HOUR_S, false));
    CHECK_TRUE(ctl_integral_store_begin(&store, 250.0, 50.0 + 6.0 * HOUR_S, false));
}

/* A failed write keeps the old saved value and is tried again no sooner than 6 hours later. */
static void test_failed_write_is_retried_later(void)
{
    ctl_integral_store store;
    ctl_integral_store_load(&store, "100", 3, 300.0, 0.0);
    CHECK_TRUE(ctl_integral_store_begin(&store, 150.0, 6.0 * HOUR_S, false));
    ctl_integral_store_end(&store, 150.0, false);
    CHECK_DOUBLE(store.saved_w, 100.0);
    CHECK_TRUE(!ctl_integral_store_begin(&store, 150.0, 6.0 * HOUR_S + MINUTE_S, false));
    CHECK_TRUE(!ctl_integral_store_begin(&store, 150.0, 11.0 * HOUR_S + 59.0 * MINUTE_S, false));
    CHECK_TRUE(ctl_integral_store_begin(&store, 150.0, 12.0 * HOUR_S, false));
    ctl_integral_store_end(&store, 150.0, true);
    CHECK_DOUBLE(store.saved_w, 150.0);
}

/* A reset writes at once, and a written reset starts the next 6 hours. */
static void test_reset_writes_at_once(void)
{
    ctl_integral_store store;
    ctl_integral_store_load(&store, "100", 3, 300.0, 0.0);
    CHECK_TRUE(ctl_integral_store_begin(&store, 0.0, MINUTE_S, true));
    ctl_integral_store_end(&store, 0.0, true);
    CHECK_DOUBLE(store.saved_w, 0.0);
    CHECK_TRUE(!ctl_integral_store_begin(&store, 50.0, 6.0 * HOUR_S, false));
    CHECK_TRUE(ctl_integral_store_begin(&store, 50.0, MINUTE_S + 6.0 * HOUR_S, false));
}

/* ---- The text written ----------------------------------------------------------------- */

static void test_format_reads_back(void)
{
    static const double values[] = {0.0, 42.5, -300.0, 123.456};
    char text[CTL_INTEGRAL_STORE_TEXT_MAX];
    size_t i;
    for (i = 0; i < sizeof values / sizeof values[0]; i++) {
        double read_w = 0.0;
        CHECK_TRUE(ctl_integral_store_format(values[i], text, sizeof text));
        CHECK_TRUE(ctl_integral_store_parse(text, strlen(text), &read_w));
        CHECK_DOUBLE(read_w, values[i]);
    }
    CHECK_TRUE(!ctl_integral_store_format(1.0, text, 2));
    CHECK_TRUE(!ctl_integral_store_format(NAN, text, sizeof text));
    CHECK_TRUE(!ctl_integral_store_format(INFINITY, text, sizeof text));
    CHECK_TRUE(!ctl_integral_store_format(-INFINITY, text, sizeof text));
}

/* ---- A simulated day ------------------------------------------------------------------ */

/* The integral moves 15 W each hour, checked every minute for 24 hours: the file is written
 * at most once in any 6 hours, so at most 4 times. */
static void test_writes_in_a_day(void)
{
    ctl_integral_store store;
    double previous_write_s = -1.0;
    unsigned writes = 0;
    int minute;
    ctl_integral_store_load(&store, NULL, 0, 300.0, 0.0);
    for (minute = 1; minute <= 24 * 60; minute++) {
        double now_s = minute * MINUTE_S;
        double integral_w = 15.0 * (double)(minute / 60);
        if (ctl_integral_store_begin(&store, integral_w, now_s, false)) {
            if (previous_write_s >= 0.0) {
                CHECK_TRUE(now_s - previous_write_s >= 6.0 * HOUR_S);
            }
            writes++;
            ctl_integral_store_end(&store, integral_w, true);
            previous_write_s = now_s;
        }
    }
    CHECK_TRUE(writes >= 1u);
    CHECK_TRUE(writes <= 4u);
}

/* A steady integral is never worth a write, however long it holds. */
static void test_steady_integral_is_not_written(void)
{
    int hour;
    for (hour = 1; hour <= 24; hour++) {
        CHECK_TRUE(!ctl_integral_store_due(120.0, 120.0, hour * HOUR_S, false));
    }
}

int main(void)
{
    test_write_rule();
    test_parse();
    test_start();
    test_start_is_held_without_gain();
    test_load_writes_nothing();
    test_failed_write_is_retried_later();
    test_reset_writes_at_once();
    test_format_reads_back();
    test_writes_in_a_day();
    test_steady_integral_is_not_written();
    return check_result();
}
