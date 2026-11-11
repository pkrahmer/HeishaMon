#include "check.h"
#include "core/ctl_config.h"
#include "core/ctl_relay.h"
#include "core/ctl_types.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    ctl_demand previous;
    double energy_wh;
    ctl_demand expected;
} relay_row;

static void test_relay(void)
{
    const double hysteresis = 1500.0;
    const relay_row rows[] = {
        {CTL_DEMAND_NONE, -1500.0, CTL_DEMAND_HEATING},
        {CTL_DEMAND_NONE, nextafter(-1500.0, INFINITY), CTL_DEMAND_NONE},
        {CTL_DEMAND_NONE, 0.0, CTL_DEMAND_NONE},
        {CTL_DEMAND_NONE, nextafter(1500.0, -INFINITY), CTL_DEMAND_NONE},
        {CTL_DEMAND_NONE, 1500.0, CTL_DEMAND_COOLING},
        {CTL_DEMAND_NONE, NAN, CTL_DEMAND_NONE},
        {CTL_DEMAND_HEATING, nextafter(0.0, -INFINITY), CTL_DEMAND_HEATING},
        {CTL_DEMAND_HEATING, 0.0, CTL_DEMAND_NONE},
        {CTL_DEMAND_HEATING, 2000.0, CTL_DEMAND_NONE},
        {CTL_DEMAND_HEATING, NAN, CTL_DEMAND_HEATING},
        {CTL_DEMAND_COOLING, nextafter(0.0, INFINITY), CTL_DEMAND_COOLING},
        {CTL_DEMAND_COOLING, 0.0, CTL_DEMAND_NONE},
        {CTL_DEMAND_COOLING, -2000.0, CTL_DEMAND_NONE},
        {CTL_DEMAND_COOLING, NAN, CTL_DEMAND_COOLING},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const ctl_demand actual = ctl_relay(rows[i].energy_wh, rows[i].previous, hysteresis);
        CHECK_TEXT(ctl_demand_name(actual), ctl_demand_name(rows[i].expected));
    }
}

typedef struct {
    uint8_t allowed;
    double low;
    double high;
} bounds_row;

static void test_clamp_bounds(void)
{
    const uint8_t heating = CTL_DEMAND_BIT(CTL_DEMAND_HEATING);
    const uint8_t cooling = CTL_DEMAND_BIT(CTL_DEMAND_COOLING);
    const uint8_t none = CTL_DEMAND_BIT(CTL_DEMAND_NONE);
    const bounds_row rows[] = {
        {CTL_DEMAND_ALL, -3000.0, 3000.0},
        {(uint8_t)(cooling | none), -1500.0, 3000.0},
        {(uint8_t)(heating | none), -3000.0, 1500.0},
        {none, -1500.0, 1500.0},
        {(uint8_t)(heating | cooling), -3000.0, 3000.0},
    };
    ctl_config config;
    size_t i;

    ctl_config_defaults(&config);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        double low = NAN;
        double high = NAN;
        ctl_clamp_bounds(rows[i].allowed, &config, &low, &high);
        CHECK_DOUBLE(low, rows[i].low);
        CHECK_DOUBLE(high, rows[i].high);
    }
}

static void test_clamp(void)
{
    const double rows[][2] = {
        {-1.5, -1.0}, {-1.0, -1.0}, {0.5, 0.5}, {2.0, 2.0}, {2.5, 2.0}, {NAN, NAN},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_DOUBLE(ctl_clamp(rows[i][0], -1.0, 2.0), rows[i][1]);
    }
}

int main(void)
{
    test_relay();
    test_clamp_bounds();
    test_clamp();
    return check_result();
}
