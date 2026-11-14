#include "check.h"

#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "glue/ctl_integral_store.h"

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

/* With the gain at 0 the loaded value is held, not cleared. */
static void test_start_is_held_without_gain(void)
{
    ctl_config config;
    ctl_state state;
    double dt_s;
    ctl_config_defaults(&config);
    CHECK_DOUBLE(config.integral_w_per_kh, 0.0);
    ctl_state_init(&state);
    state.p_integral_w = ctl_integral_store_start("120", 3, config.integral_limit_w);
    ctl_advance(&state, 0.0, &config, &dt_s);
    ctl_advance(&state, HOUR_S, &config, &dt_s);
    CHECK_DOUBLE(state.p_integral_w, 120.0);
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
}

/* ---- A simulated day ------------------------------------------------------------------ */

/* The integral moves 15 W each hour, checked every minute for 24 hours: the file is written
 * at most once in any 6 hours, so at most 4 times. */
static void test_writes_in_a_day(void)
{
    double saved_w = 0.0;
    double last_write_s = 0.0;
    double previous_write_s = -1.0;
    unsigned writes = 0;
    int minute;
    for (minute = 1; minute <= 24 * 60; minute++) {
        double now_s = minute * MINUTE_S;
        double integral_w = 15.0 * (double)(minute / 60);
        if (ctl_integral_store_due(integral_w, saved_w, now_s - last_write_s, false)) {
            if (previous_write_s >= 0.0) {
                CHECK_TRUE(now_s - previous_write_s >= 6.0 * HOUR_S);
            }
            writes++;
            saved_w = integral_w;
            previous_write_s = now_s;
            last_write_s = now_s;
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
    test_format_reads_back();
    test_writes_in_a_day();
    test_steady_integral_is_not_written();
    return check_result();
}
