#include "check.h"

#include "core/ctl_balance.h"
#include "core/ctl_controller.h"
#include "glue/ctl_desired.h"
#include "glue/ctl_dispatch.h"
#include "glue/ctl_settings.h"
#include "loop/ctl_loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define THERMOSTAT_TOPIC "ccu/device/status/000A9D89A7961A/1/SET_POINT_TEMPERATURE"
#define OTHER_TOPIC "ccu/device/status/000A9D89A7961B/1/SET_POINT_TEMPERATURE"
#define BASE "panasonic_heat_pump"

static void set_ok(ctl_settings *settings, const char *name, const char *value)
{
    CHECK_UINT((uint32_t)ctl_settings_set(settings, name, value), (uint32_t)CTL_SETTING_OK);
}

static void start(ctl_settings *settings, ctl_desired *desired, const char *topic)
{
    ctl_settings_defaults(settings);
    set_ok(settings, "desired_indoor_c", "22");
    set_ok(settings, "desired_topic", topic);
    ctl_desired_init(desired, settings);
}

static void offer(ctl_desired *desired, const char *payload)
{
    ctl_desired_offer(desired, payload, strlen(payload));
}

static void check_target(const char *file, int line, const ctl_desired *desired,
                         const ctl_settings *settings, double expected_c,
                         ctl_desired_source expected_source)
{
    double target_c = -1.0;
    ctl_desired_source source = ctl_desired_target(desired, settings, &target_c);
    check_double(file, line, target_c, expected_c);
    check_uint(file, line, (uint32_t)source, (uint32_t)expected_source);
}

#define CHECK_TARGET(desired, settings, expected_c, expected_source) \
    check_target(__FILE__, __LINE__, (desired), (settings), (expected_c), (expected_source))

/* ---- The rule ---- */

typedef struct {
    const char *payload; /* NULL: no message since the start */
    double target_c;
    ctl_desired_source source;
} rule_row;

/* Each row follows a held setpoint of 21, so that a refused message shows that it clears it. */
static void test_rule(void)
{
    static const rule_row rows[] = {
        {NULL, 22.0, CTL_DESIRED_SETTING},
        {"{\"ts\":1,\"v\":21,\"s\":0}", 21.0, CTL_DESIRED_TOPIC},
        {"{\"ts\":2,\"v\":16,\"s\":0}", 16.0, CTL_DESIRED_TOPIC},
        {"{\"ts\":3,\"v\":26,\"s\":0}", 26.0, CTL_DESIRED_TOPIC},
        {"{\"ts\":4,\"v\":15.9,\"s\":0}", 22.0, CTL_DESIRED_SETTING},
        {"{\"ts\":5,\"v\":26.1,\"s\":0}", 22.0, CTL_DESIRED_SETTING},
        {"{\"ts\":6,\"v\":5.5,\"s\":0}", 22.0, CTL_DESIRED_SETTING},
        {"{\"ts\":7,\"v\":4.5,\"s\":0}", 22.0, CTL_DESIRED_SETTING},
        {"{\"ts\":8,\"v\":", 22.0, CTL_DESIRED_SETTING},
        {"warm", 22.0, CTL_DESIRED_SETTING},
        {"", 22.0, CTL_DESIRED_SETTING},
        {"21.5", 21.5, CTL_DESIRED_TOPIC},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_settings settings;
        ctl_desired desired;
        start(&settings, &desired, THERMOSTAT_TOPIC);
        if (rows[i].payload != NULL) {
            offer(&desired, "{\"ts\":0,\"v\":21,\"s\":0}");
            CHECK_TARGET(&desired, &settings, 21.0, CTL_DESIRED_TOPIC);
            offer(&desired, rows[i].payload);
        }
        CHECK_TARGET(&desired, &settings, rows[i].target_c, rows[i].source);
    }
}

static void test_valid_after_invalid(void)
{
    ctl_settings settings;
    ctl_desired desired;
    start(&settings, &desired, THERMOSTAT_TOPIC);
    offer(&desired, "{\"ts\":1,\"v\":21,\"s\":0}");
    offer(&desired, "{\"ts\":2,\"v\":30,\"s\":0}");
    CHECK_TARGET(&desired, &settings, 22.0, CTL_DESIRED_SETTING);
    offer(&desired, "{\"ts\":3,\"v\":20.5,\"s\":0}");
    CHECK_TARGET(&desired, &settings, 20.5, CTL_DESIRED_TOPIC);
}

/* The payload need not end with a zero: only its length is read. */
static void test_length_bounds_the_payload(void)
{
    static const char payload[] = "{\"v\":21}9";
    ctl_settings settings;
    ctl_desired desired;
    start(&settings, &desired, THERMOSTAT_TOPIC);
    CHECK_TRUE(ctl_desired_offer(&desired, payload, strlen(payload) - 1));
    CHECK_TARGET(&desired, &settings, 21.0, CTL_DESIRED_TOPIC);
    CHECK_TRUE(!ctl_desired_offer(&desired, NULL, 0));
    CHECK_TARGET(&desired, &settings, 22.0, CTL_DESIRED_SETTING);
}

/* The setting keeps its own value while a setpoint is held, and a change of it shows once the
 * setpoint is cleared. */
static void test_setting_keeps_its_value(void)
{
    ctl_settings settings;
    ctl_desired desired;
    char value[CTL_SETTINGS_VALUE_MAX];
    start(&settings, &desired, THERMOSTAT_TOPIC);
    offer(&desired, "{\"v\":21}");
    set_ok(&settings, "desired_indoor_c", "23");
    ctl_desired_follow(&desired, &settings);
    CHECK_TARGET(&desired, &settings, 21.0, CTL_DESIRED_TOPIC);
    CHECK_TRUE(ctl_settings_get(&settings, "desired_indoor_c", value, sizeof value));
    CHECK_TEXT(value, "23");
    offer(&desired, "");
    CHECK_TARGET(&desired, &settings, 23.0, CTL_DESIRED_SETTING);
}

static void test_source_names(void)
{
    CHECK_TEXT(ctl_desired_source_name(CTL_DESIRED_SETTING), "setting");
    CHECK_TEXT(ctl_desired_source_name(CTL_DESIRED_TOPIC), "topic");
}

/* ---- The topic ---- */

static void check_dispatch(const char *file, int line, const ctl_settings *settings,
                           const char *topic, ctl_topic_kind expected)
{
    ctl_topic target = ctl_dispatch_topic(settings, BASE, topic);
    check_uint(file, line, (uint32_t)target.kind, (uint32_t)expected);
}

#define CHECK_DISPATCH(settings, topic, expected) \
    check_dispatch(__FILE__, __LINE__, (settings), (topic), (expected))

static void test_dispatch(void)
{
    ctl_settings settings;
    ctl_desired desired;
    start(&settings, &desired, THERMOSTAT_TOPIC);
    CHECK_DISPATCH(&settings, THERMOSTAT_TOPIC, CTL_TOPIC_DESIRED);
    CHECK_DISPATCH(&settings, OTHER_TOPIC, CTL_TOPIC_NONE);
    CHECK_DISPATCH(&settings, BASE "/control/set/desired_topic", CTL_TOPIC_SETTING);
    /* A room on the same topic keeps it: the room was there first. */
    set_ok(&settings, "rooms", "1");
    set_ok(&settings, "room.0.topic", THERMOSTAT_TOPIC);
    CHECK_DISPATCH(&settings, THERMOSTAT_TOPIC, CTL_TOPIC_INPUT);
}

static void test_empty_topic_changes_nothing(void)
{
    ctl_settings settings;
    ctl_desired desired;
    start(&settings, &desired, THERMOSTAT_TOPIC);
    offer(&desired, "{\"v\":21}");
    set_ok(&settings, "desired_topic", "");
    ctl_desired_follow(&desired, &settings);
    CHECK_DISPATCH(&settings, THERMOSTAT_TOPIC, CTL_TOPIC_NONE);
    CHECK_DISPATCH(&settings, "", CTL_TOPIC_NONE);
    CHECK_TARGET(&desired, &settings, 22.0, CTL_DESIRED_SETTING);
}

static void test_new_topic_forgets_the_setpoint(void)
{
    ctl_settings settings;
    ctl_desired desired;
    start(&settings, &desired, THERMOSTAT_TOPIC);
    offer(&desired, "{\"v\":21}");
    /* A change of another setting keeps it. */
    set_ok(&settings, "hysteresis_wh", "1200");
    ctl_desired_follow(&desired, &settings);
    CHECK_TARGET(&desired, &settings, 21.0, CTL_DESIRED_TOPIC);

    set_ok(&settings, "desired_topic", OTHER_TOPIC);
    ctl_desired_follow(&desired, &settings);
    CHECK_TARGET(&desired, &settings, 22.0, CTL_DESIRED_SETTING);
    CHECK_DISPATCH(&settings, THERMOSTAT_TOPIC, CTL_TOPIC_NONE);
    CHECK_DISPATCH(&settings, OTHER_TOPIC, CTL_TOPIC_DESIRED);
    offer(&desired, "{\"v\":19.5}");
    CHECK_TARGET(&desired, &settings, 19.5, CTL_DESIRED_TOPIC);
}

/* ---- The setting ---- */

static void test_setting(void)
{
    static const char *const refused[] = {"a+b", "a/#", "a b", "a\tb"};
    ctl_settings settings;
    char name[CTL_SETTINGS_NAME_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t i;
    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_settings_get(&settings, "desired_topic", value, sizeof value));
    CHECK_TEXT(value, "");
    set_ok(&settings, "desired_topic", THERMOSTAT_TOPIC);
    CHECK_TRUE(ctl_settings_get(&settings, "desired_topic", value, sizeof value));
    CHECK_TEXT(value, THERMOSTAT_TOPIC);
    for (i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        CHECK_UINT((uint32_t)ctl_settings_set(&settings, "desired_topic", refused[i]),
                   (uint32_t)CTL_SETTING_BAD_VALUE);
    }
    CHECK_TRUE(ctl_settings_get(&settings, "desired_topic", value, sizeof value));
    CHECK_TEXT(value, THERMOSTAT_TOPIC);
    set_ok(&settings, "desired_topic", "");
    CHECK_TRUE(ctl_settings_get(&settings, "desired_topic", value, sizeof value));
    CHECK_TEXT(value, "");

    /* Right after desired_indoor_c, with rooms listed before both. */
    set_ok(&settings, "rooms", "1");
    for (i = 0; i + 1 < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        if (strcmp(name, "desired_indoor_c") == 0) {
            break;
        }
    }
    CHECK_TRUE(ctl_settings_name_at(&settings, i + 1, name, sizeof name));
    CHECK_TEXT(name, "desired_topic");
}

/* A file an older firmware wrote has no desired_topic; the loader sets only what it holds. */
static void test_file_without_the_setting(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    ctl_settings_defaults(&settings);
    set_ok(&settings, "desired_indoor_c", "21");
    set_ok(&settings, "hysteresis_wh", "1200");
    CHECK_TRUE(ctl_settings_valid(&settings));
    CHECK_TRUE(ctl_settings_get(&settings, "desired_topic", value, sizeof value));
    CHECK_TEXT(value, "");
}

/* ---- The loop ---- */

static void give(ctl_loop *loop, const ctl_loop_config *config, ctl_input input,
                 const char *text, double now)
{
    CHECK_UINT((uint32_t)ctl_loop_input(loop, config, input, 0, text, strlen(text), now),
               (uint32_t)CTL_INPUT_TAKEN);
}

/* Runs the loop to its first decision with the house at 20.5 degC and returns the decision. */
static ctl_decision first_decision(const ctl_loop_config *config)
{
    ctl_loop loop;
    ctl_loop_record record;
    double now = 0.0;
    size_t i;
    memset(&record, 0, sizeof record);
    ctl_loop_init(&loop, config, 0.0);
    for (i = 0; i < 100000; i++) {
        give(&loop, config, CTL_INPUT_ROOM, "{\"v\":20.5}", now);
        give(&loop, config, CTL_INPUT_OUTDOOR, "3", now);
        give(&loop, config, CTL_INPUT_HEAT, "4000", now);
        if (ctl_loop_tick(&loop, config, now, &record) && record.decision.has_inputs) {
            break;
        }
        now += 10.0;
    }
    CHECK_TRUE(record.decision.has_inputs);
    return record.decision;
}

static void test_loop_uses_the_setpoint(void)
{
    ctl_settings settings;
    ctl_desired desired;
    ctl_loop_config config;
    ctl_config at_21;
    ctl_config at_22;
    ctl_balance expected_21;
    ctl_balance expected_22;
    ctl_decision decision;
    start(&settings, &desired, THERMOSTAT_TOPIC);
    set_ok(&settings, "rooms", "1");
    set_ok(&settings, "room.0.topic", "house/room0");
    ctl_desired_follow(&desired, &settings);
    offer(&desired, "{\"ts\":1,\"v\":21,\"s\":0}");

    ctl_settings_to_loop(&settings, &config);
    ctl_desired_apply(&desired, &settings, &config);
    CHECK_DOUBLE(config.core.desired_indoor_c, 21.0);
    CHECK_TRUE(ctl_loop_config_valid(&config));
    decision = first_decision(&config);

    at_21 = config.core;
    at_22 = config.core;
    at_22.desired_indoor_c = 22.0;
    ctl_energy_balance(&decision.inputs, &at_21, &expected_21);
    ctl_energy_balance(&decision.inputs, &at_22, &expected_22);
    CHECK_DOUBLE(decision.balance.p_trim_w, expected_21.p_trim_w);
    CHECK_TRUE(decision.balance.p_trim_w != expected_22.p_trim_w);

    /* Cleared, the setting is the target again. */
    offer(&desired, "");
    ctl_desired_apply(&desired, &settings, &config);
    CHECK_DOUBLE(config.core.desired_indoor_c, 22.0);
}

int main(void)
{
    test_rule();
    test_valid_after_invalid();
    test_length_bounds_the_payload();
    test_setting_keeps_its_value();
    test_source_names();
    test_dispatch();
    test_empty_topic_changes_nothing();
    test_new_topic_forgets_the_setpoint();
    test_setting();
    test_file_without_the_setting();
    test_loop_uses_the_setpoint();
    return check_result();
}
