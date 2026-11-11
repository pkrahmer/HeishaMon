#include "check.h"

#include "adapt/ctl_pump.h"
#include "core/ctl_types.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MARKER_VALUE -12345.0
#define MARKER_T -54321.0

typedef enum { HEAT, VALVE, DEFROST } pump_input;

/* A reading no function writes, so that a refusal that writes anyway is seen. */
static ctl_reading marker_reading(void)
{
    ctl_reading reading;
    reading.signal = CTL_SIGNAL_COUNT;
    reading.value = MARKER_VALUE;
    reading.t = MARKER_T;
    return reading;
}

static void check_reading(const char *file, int line, const ctl_reading *actual, ctl_signal signal,
                          double value, double t)
{
    check_uint(file, line, (uint32_t)actual->signal, (uint32_t)signal);
    check_double(file, line, actual->value, value);
    check_double(file, line, actual->t, t);
}

static void check_no_reading(const char *file, int line, bool wrote, const ctl_reading *actual)
{
    check_true(file, line, !wrote, "no reading");
    check_reading(file, line, actual, CTL_SIGNAL_COUNT, MARKER_VALUE, MARKER_T);
}

static void check_pump_equal(const char *file, int line, const ctl_pump *actual,
                             const ctl_pump *expected)
{
    check_true(file, line, actual->has_heat == expected->has_heat, "has_heat");
    check_double(file, line, actual->heat_w, expected->heat_w);
    check_true(file, line, actual->hot_water == expected->hot_water, "hot_water");
    check_true(file, line, actual->defrost == expected->defrost, "defrost");
}

static bool feed(ctl_pump *pump, pump_input input, const char *text, double t, ctl_reading *reading)
{
    switch (input) {
    case HEAT:
        return ctl_pump_heat(pump, text, strlen(text), t, reading);
    case VALVE:
        return ctl_pump_valve(pump, text, strlen(text), t, reading);
    default:
        return ctl_pump_defrost(pump, text, strlen(text), t, reading);
    }
}

/* One step of a sequence on one pump: the input, what it gives (has_reading, value) and the
 * flags it leaves behind. */
typedef struct {
    pump_input input;
    const char *text;
    double t;
    bool has_reading;
    double value;
    bool hot_water;
    bool defrost;
    double heat_w;
} pump_step;

static void run_steps(const char *file, int line, ctl_pump *pump, const pump_step *steps,
                      size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        ctl_reading reading = marker_reading();
        bool wrote = feed(pump, steps[i].input, steps[i].text, steps[i].t, &reading);
        if (steps[i].has_reading) {
            check_true(file, line, wrote, steps[i].text);
            check_reading(file, line, &reading, CTL_SIGNAL_P_PRODUCED, steps[i].value, steps[i].t);
        } else {
            check_no_reading(file, line, wrote, &reading);
        }
        check_true(file, line, pump->hot_water == steps[i].hot_water, "hot_water");
        check_true(file, line, pump->defrost == steps[i].defrost, "defrost");
        check_double(file, line, pump->heat_w, steps[i].heat_w);
    }
}

static void test_init(void)
{
    ctl_pump pump;
    pump.has_heat = true;
    pump.heat_w = 1.0;
    pump.hot_water = true;
    pump.defrost = true;
    ctl_pump_init(&pump);
    CHECK_TRUE(!pump.has_heat);
    CHECK_TRUE(!pump.hot_water);
    CHECK_TRUE(!pump.defrost);
}

static void test_outdoor(void)
{
    static const struct {
        const char *text;
        double t;
        bool accepted;
        double value;
    } rows[] = {
        {"5", 100.0, true, 5.0},
        {"-60", 100.0, true, -60.0},
        {"60", 100.0, true, 60.0},
        {"-60.5", 100.0, false, 0.0},
        {"61", 100.0, false, 0.0},
        {"x", 100.0, false, 0.0},
        {"5", NAN, false, 0.0},
        {"5", INFINITY, false, 0.0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_reading reading = marker_reading();
        bool wrote = ctl_pump_outdoor(rows[i].text, strlen(rows[i].text), rows[i].t, &reading);
        if (rows[i].accepted) {
            CHECK_TRUE(wrote);
            check_reading(__FILE__, __LINE__, &reading, CTL_SIGNAL_T_OUTDOOR, rows[i].value,
                          rows[i].t);
        } else {
            check_no_reading(__FILE__, __LINE__, wrote, &reading);
        }
    }
}

static void test_heat_unmasked(void)
{
    static const struct {
        const char *text;
        double t;
        bool accepted;
        double value;
    } rows[] = {
        {"3200", 0.0, true, 3200.0},
        {"-1", 1.0, false, 0.0},
        {"30001", 2.0, false, 0.0},
        {"{\"v\":\"x\"}", 3.0, false, 0.0},
        {"3300", NAN, false, 0.0},
        {"0", 4.0, true, 0.0},
        {"30000", 5.0, true, 30000.0},
        {"-1", 6.0, false, 0.0},
    };
    ctl_pump pump;
    size_t i;
    ctl_pump_init(&pump);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_pump expected = pump;
        ctl_reading reading = marker_reading();
        bool wrote = ctl_pump_heat(&pump, rows[i].text, strlen(rows[i].text), rows[i].t, &reading);
        if (rows[i].accepted) {
            CHECK_TRUE(wrote);
            check_reading(__FILE__, __LINE__, &reading, CTL_SIGNAL_P_PRODUCED, rows[i].value,
                          rows[i].t);
            expected.has_heat = true;
            expected.heat_w = rows[i].value;
        } else {
            check_no_reading(__FILE__, __LINE__, wrote, &reading);
        }
        check_pump_equal(__FILE__, __LINE__, &pump, &expected);
    }
}

static void test_valve_mask(void)
{
    static const pump_step steps[] = {
        {HEAT, "3200", 0.0, true, 3200.0, false, false, 3200.0},
        {VALVE, "1", 10.0, true, 0.0, true, false, 3200.0},
        {HEAT, "3500", 20.0, true, 0.0, true, false, 3500.0},
        {VALVE, "1", 30.0, false, 0.0, true, false, 3500.0},
        {VALVE, "0", 40.0, true, 3500.0, false, false, 3500.0},
        {VALVE, "2", 50.0, false, 0.0, false, false, 3500.0},
        {VALVE, "0.5", 60.0, false, 0.0, false, false, 3500.0},
        {VALVE, "-1", 70.0, false, 0.0, false, false, 3500.0},
        {VALVE, "x", 80.0, false, 0.0, false, false, 3500.0},
        {VALVE, "1", NAN, false, 0.0, false, false, 3500.0},
    };
    ctl_pump pump;
    ctl_pump_init(&pump);
    run_steps(__FILE__, __LINE__, &pump, steps, sizeof steps / sizeof steps[0]);
}

static void test_defrost_mask(void)
{
    static const pump_step steps[] = {
        {HEAT, "3200", 0.0, true, 3200.0, false, false, 3200.0},
        {DEFROST, "1", 10.0, true, 0.0, false, true, 3200.0},
        {VALVE, "1", 20.0, false, 0.0, true, true, 3200.0},
        {DEFROST, "0", 30.0, false, 0.0, true, false, 3200.0},
        {VALVE, "0", 40.0, true, 3200.0, false, false, 3200.0},
        {DEFROST, "2", 50.0, false, 0.0, false, false, 3200.0},
        {DEFROST, "0.5", 60.0, false, 0.0, false, false, 3200.0},
        {DEFROST, "x", 70.0, false, 0.0, false, false, 3200.0},
        {DEFROST, "1", INFINITY, false, 0.0, false, false, 3200.0},
        {DEFROST, "0", 80.0, false, 0.0, false, false, 3200.0},
    };
    ctl_pump pump;
    ctl_pump_init(&pump);
    run_steps(__FILE__, __LINE__, &pump, steps, sizeof steps / sizeof steps[0]);
}

static void test_flags_before_heat(void)
{
    static const pump_step steps[] = {
        {VALVE, "1", 0.0, false, 0.0, true, false, 0.0},
        {DEFROST, "1", 10.0, false, 0.0, true, true, 0.0},
        {HEAT, "3200", 20.0, true, 0.0, true, true, 3200.0},
    };
    ctl_pump pump;
    ctl_pump_init(&pump);
    /* heat_w is not stated before the first heat; zero keeps the steps' last column meaningful. */
    pump.heat_w = 0.0;
    run_steps(__FILE__, __LINE__, &pump, steps, sizeof steps / sizeof steps[0]);
    CHECK_TRUE(pump.has_heat);
}

/* The mask switching is news even when the stored heat is 0 W, so the core still sees when. */
static void test_zero_heat_switch(void)
{
    static const pump_step steps[] = {
        {HEAT, "0", 0.0, true, 0.0, false, false, 0.0},
        {VALVE, "1", 10.0, true, 0.0, true, false, 0.0},
        {VALVE, "0", 20.0, true, 0.0, false, false, 0.0},
    };
    ctl_pump pump;
    ctl_pump_init(&pump);
    run_steps(__FILE__, __LINE__, &pump, steps, sizeof steps / sizeof steps[0]);
}

static void test_solar(void)
{
    static const struct {
        const char *text;
        double gain;
        double t;
        bool accepted;
        double value;
    } rows[] = {
        {"1500", 0.02, 100.0, true, 1500.0 * 0.02},
        {"0", 0.02, 100.0, true, 0.0},
        {"100000", 0.02, 100.0, true, 100000.0 * 0.02},
        {"100000.5", 0.02, 100.0, false, 0.0},
        {"-5", 0.02, 100.0, false, 0.0},
        {"x", 0.02, 100.0, false, 0.0},
        {"1500", -0.01, 100.0, false, 0.0},
        {"1500", NAN, 100.0, false, 0.0},
        {"1500", INFINITY, 100.0, false, 0.0},
        {"1500", 0.0, 100.0, true, 0.0},
        {"1500", 0.02, NAN, false, 0.0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_reading reading = marker_reading();
        bool wrote = ctl_solar(rows[i].text, strlen(rows[i].text), rows[i].gain, rows[i].t, &reading);
        if (rows[i].accepted) {
            CHECK_TRUE(wrote);
            check_reading(__FILE__, __LINE__, &reading, CTL_SIGNAL_P_SOLAR, rows[i].value,
                          rows[i].t);
        } else {
            check_no_reading(__FILE__, __LINE__, wrote, &reading);
        }
    }
}

int main(void)
{
    test_init();
    test_outdoor();
    test_heat_unmasked();
    test_valve_mask();
    test_defrost_mask();
    test_flags_before_heat();
    test_zero_heat_switch();
    test_solar();
    return check_result();
}
