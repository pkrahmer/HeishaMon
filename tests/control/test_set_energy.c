#include "check.h"

#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "core/ctl_relay.h"
#include "core/ctl_signals.h"
#include "core/ctl_types.h"
#include "glue/ctl_dispatch.h"
#include "glue/ctl_settings.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BASE "panasonic_heat_pump"
#define ENERGY_TOPIC BASE "/control/set/energy_wh"

/* With the defaults and the fallbacks, the three readings give this net power. */
static const double net_power_w = -2735.0;

/* Hysteresis 1500 Wh and energy clamp factor 2, as the rows below assume. */
static void config_for_rows(ctl_config *config)
{
    ctl_config_defaults(config);
    config->hysteresis_wh = 1500.0;
    config->energy_clamp_factor = 2.0;
}

static void observe_three_readings(ctl_state *state, double t, const ctl_config *config)
{
    ctl_observe(state, CTL_SIGNAL_T_INDOOR, 21.0, t, config);
    ctl_observe(state, CTL_SIGNAL_T_OUTDOOR, 5.0, t, config);
    ctl_observe(state, CTL_SIGNAL_P_PRODUCED, 0.0, t, config);
}

static void test_rows_with_every_direction_allowed(void)
{
    static const struct {
        double set_wh;
        bool taken;
        double expected_wh;
    } rows[] = {
        {-1200.0, true, -1200.0},
        {-5000.0, true, -3000.0},
        {5000.0, true, 3000.0},
        {NAN, false, 250.0},
        {INFINITY, false, 250.0},
        {-INFINITY, false, 250.0},
    };
    ctl_config config;
    ctl_state state;
    size_t i;
    config_for_rows(&config);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_state_init(&state);
        state.energy_wh = 250.0;
        CHECK_TRUE(ctl_set_energy(&state, rows[i].set_wh, 0.0, &config) == rows[i].taken);
        CHECK_DOUBLE(state.energy_wh, rows[i].expected_wh);
    }
}

/* A refused value changes nothing, not even the integration reference. */
static void test_refused_value_changes_nothing(void)
{
    ctl_config config;
    ctl_state state;
    config_for_rows(&config);
    ctl_state_init(&state);
    observe_three_readings(&state, 0.0, &config);
    CHECK_TRUE(!ctl_set_energy(&state, NAN, 300.0, &config));
    CHECK_DOUBLE(state.t_last, 0.0);
    CHECK_DOUBLE(state.integrated_s, 0.0);
    CHECK_TRUE(!ctl_set_energy(&state, 100.0, NAN, &config));
    CHECK_TRUE(!ctl_set_energy(&state, 100.0, INFINITY, &config));
    CHECK_DOUBLE(state.t_last, 0.0);
    CHECK_DOUBLE(state.energy_wh, 0.0);
}

static void test_masked_direction_clamps_like_set_allowed(void)
{
    const uint8_t heating = CTL_DEMAND_BIT(CTL_DEMAND_HEATING);
    ctl_config config;
    ctl_state set;
    ctl_state masked;
    config_for_rows(&config);

    ctl_state_init(&masked);
    masked.energy_wh = 2000.0;
    ctl_set_allowed(&masked, heating, 0.0, &config);

    ctl_state_init(&set);
    ctl_set_allowed(&set, heating, 0.0, &config);
    CHECK_TRUE(ctl_set_energy(&set, 2000.0, 0.0, &config));
    CHECK_DOUBLE(set.energy_wh, masked.energy_wh);
    CHECK_DOUBLE(set.energy_wh, 1500.0);
}

/* The interval up to the set is integrated with the old balance, and the next one starts from
 * the value set. */
static void test_integrates_up_to_the_set_first(void)
{
    const double hours = 300.0 / 3600.0;
    ctl_config config;
    ctl_state advanced;
    ctl_state set;
    double dt_s;
    config_for_rows(&config);

    ctl_state_init(&advanced);
    observe_three_readings(&advanced, 0.0, &config);
    set = advanced;
    CHECK_TRUE(ctl_advance(&advanced, 300.0, &config, &dt_s));
    CHECK_DOUBLE(advanced.energy_wh, 0.0 + net_power_w * hours);

    CHECK_TRUE(ctl_set_energy(&set, -1200.0, 300.0, &config));
    CHECK_DOUBLE(set.energy_wh, -1200.0);
    CHECK_DOUBLE(set.t_last, 300.0);
    CHECK_DOUBLE(set.integrated_s, advanced.integrated_s);
    CHECK_DOUBLE(set.ledger.internal_wh, advanced.ledger.internal_wh);
    CHECK_DOUBLE(set.ledger.transmission_wh, advanced.ledger.transmission_wh);
    CHECK_DOUBLE(set.ledger.trim_wh, advanced.ledger.trim_wh);

    observe_three_readings(&set, 300.0, &config);
    CHECK_TRUE(ctl_advance(&set, 600.0, &config, &dt_s));
    CHECK_DOUBLE(set.energy_wh, -1200.0 + net_power_w * hours);
    CHECK_DOUBLE(set.integrated_s, 600.0);
}

/* The next decision reads the value set: far below the start threshold, it heats. */
static void test_next_decision_uses_the_value(void)
{
    ctl_config config;
    ctl_state state;
    ctl_decision decision;
    config_for_rows(&config);
    ctl_state_init(&state);
    observe_three_readings(&state, 0.0, &config);
    ctl_evaluate(&state, 0.0, &config, &decision);
    CHECK_TRUE(ctl_set_energy(&state, -2000.0, 0.0, &config));
    ctl_evaluate(&state, 0.0, &config, &decision);
    CHECK_DOUBLE(decision.energy_wh, -2000.0);
    CHECK_UINT((uint32_t)decision.demand, (uint32_t)CTL_DEMAND_HEATING);
}

static void test_topic_is_a_command(void)
{
    ctl_settings settings;
    ctl_topic topic;
    char value[CTL_SETTINGS_VALUE_MAX];
    ctl_settings_defaults(&settings);
    topic = ctl_dispatch_topic(&settings, BASE, ENERGY_TOPIC);
    CHECK_UINT((uint32_t)topic.kind, (uint32_t)CTL_TOPIC_SET_ENERGY);
    CHECK_TEXT(topic.name, "");
    /* No setting of the name exists, so nothing could be stored under it. */
    CHECK_TRUE(ctl_settings_set(&settings, "energy_wh", "1") != CTL_SETTING_OK);
    CHECK_TRUE(!ctl_settings_get(&settings, "energy_wh", value, sizeof value));
}

static void test_payloads(void)
{
    static const struct {
        const char *text;
        bool taken;
        double value;
    } rows[] = {
        {"-1200", true, -1200.0},
        {"2000.5", true, 2000.5},
        {"0", true, 0.0},
        {"", false, 0.0},
        {"abc", false, 0.0},
        {"nan", false, 0.0},
        {"inf", false, 0.0},
        {"1e999", false, 0.0},
        {"12 Wh", false, 0.0},
        {"{\"v\": 12}", false, 0.0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        double value = 0.0;
        bool taken = ctl_dispatch_energy_wh(rows[i].text, strlen(rows[i].text), &value);
        CHECK_TRUE(taken == rows[i].taken);
        CHECK_DOUBLE(value, rows[i].value);
    }
    {
        double value = 0.0;
        CHECK_TRUE(!ctl_dispatch_energy_wh(NULL, 0, &value));
        /* Only the length counts: the bytes after it are not read. */
        CHECK_TRUE(ctl_dispatch_energy_wh("15x", 2, &value));
        CHECK_DOUBLE(value, 15.0);
    }
}

int main(void)
{
    test_rows_with_every_direction_allowed();
    test_refused_value_changes_nothing();
    test_masked_direction_clamps_like_set_allowed();
    test_integrates_up_to_the_set_first();
    test_next_decision_uses_the_value();
    test_topic_is_a_command();
    test_payloads();
    return check_result();
}
