#include "check.h"

#include "adapt/ctl_rooms.h"
#include "core/ctl_config.h"
#include "core/ctl_relay.h"
#include "core/ctl_types.h"
#include "glue/ctl_settings.h"
#include "loop/ctl_loop.h"
#include "route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UNTOUCHED "untouched"

/* The settings start zeroed byte for byte, so that a refused change can be checked with memcmp:
 * a refusal writes nothing, padding included. */
static void fresh_defaults(ctl_settings *settings)
{
    memset(settings, 0, sizeof *settings);
    ctl_settings_defaults(settings);
}

static void check_get(const char *file, int line, const ctl_settings *settings, const char *name,
                      const char *expected)
{
    char value[CTL_SETTINGS_VALUE_MAX];
    strcpy(value, UNTOUCHED);
    check_true(file, line, ctl_settings_get(settings, name, value, sizeof value), name);
    check_text(file, line, value, expected);
}

/* Sets a value that must be refused with the given status and checks that nothing changed. */
static void check_refused(const char *file, int line, ctl_settings *settings, const char *name,
                          const char *value, ctl_setting_status expected)
{
    ctl_settings before = *settings;
    check_uint(file, line, (uint32_t)ctl_settings_set(settings, name, value), (uint32_t)expected);
    check_true(file, line, memcmp(&before, settings, sizeof before) == 0, "unchanged");
}

static void check_set(const char *file, int line, ctl_settings *settings, const char *name,
                      const char *value)
{
    check_uint(file, line, (uint32_t)ctl_settings_set(settings, name, value),
               (uint32_t)CTL_SETTING_OK);
}

static void test_defaults(void)
{
    static const struct {
        const char *name;
        const char *text;
    } rows[] = {
        {"enabled", "0"},
        {"cooling", "0"},
        {"rooms", "0"},
        {"room_timeout_s", "1800"},
        {"desired_indoor_c", "22"},
        {"transmission_w_per_k", "155"},
        {"adjustment_w_per_k", "805"},
        {"hysteresis_wh", "1500"},
        {"trim_limit_k", "3"},
        {"integral_w_per_kh", "0"},
        {"integral_limit_w", "300"},
        {"internal_w", "550"},
        {"heat_cool_heat_value", "-1"},
        {"min_change_interval_s", "300"},
        {"retry_interval_s", "120"},
        {"max_retries", "3"},
        {"smart_grid", "0"},
    };
    ctl_settings settings;
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    fresh_defaults(&settings);
    CHECK_TRUE(ctl_settings_valid(&settings));
    CHECK_UINT((uint32_t)ctl_settings_name_count(&settings), 17u);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_get(__FILE__, __LINE__, &settings, rows[i].name, rows[i].text);
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        CHECK_TEXT(name, rows[i].name);
    }
}

static void test_name_list(void)
{
    static const char *const expected[] = {
        "enabled",          "cooling",        "rooms",          "room.0.topic", "room.0.weight",
        "room.1.topic",     "room.1.weight",  "room_timeout_s", "desired_indoor_c",
    };
    ctl_settings settings;
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    fresh_defaults(&settings);
    check_set(__FILE__, __LINE__, &settings, "rooms", "2");
    check_set(__FILE__, __LINE__, &settings, "room.0.topic", "house/room0");
    check_set(__FILE__, __LINE__, &settings, "room.1.topic", "house/room1");
    CHECK_UINT((uint32_t)ctl_settings_name_count(&settings), 21u);
    for (i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        CHECK_TEXT(name, expected[i]);
    }
    CHECK_TRUE(ctl_settings_name_at(&settings, 19, name, sizeof name));
    CHECK_TEXT(name, "max_retries");
    strcpy(name, UNTOUCHED);
    CHECK_TRUE(ctl_settings_name_at(&settings, 20, name, sizeof name));
    CHECK_TEXT(name, "smart_grid");
    CHECK_TRUE(!ctl_settings_name_at(&settings, 21, name, sizeof name));
    CHECK_TRUE(!ctl_settings_name_at(&settings, 3, name, strlen("room.0.topic")));
    CHECK_TRUE(ctl_settings_name_at(&settings, 3, name, strlen("room.0.topic") + 1));
}

/* Each number setting: an accepted value, which get writes back as given, and one just outside
 * its rule. A rule of the row's own gives BAD_VALUE; a rule the core, the rooms or the router
 * check gives INVALID_SET. */
static void test_numbers(void)
{
    static const struct {
        const char *name;
        const char *accepted;
        const char *outside;
        ctl_setting_status status;
    } rows[] = {
        {"enabled", "1", "2", CTL_SETTING_BAD_VALUE},
        {"cooling", "1", "-1", CTL_SETTING_BAD_VALUE},
        {"rooms", "16", "17", CTL_SETTING_BAD_VALUE},
        {"rooms", "0", "-1", CTL_SETTING_BAD_VALUE},
        {"room.0.weight", "1000", "1000.001", CTL_SETTING_BAD_VALUE},
        {"room.0.weight", "0.5", "-0.001", CTL_SETTING_BAD_VALUE},
        {"room_timeout_s", "0.001", "0", CTL_SETTING_BAD_VALUE},
        {"desired_indoor_c", "30", "30.001", CTL_SETTING_INVALID_SET},
        {"desired_indoor_c", "5", "4.999", CTL_SETTING_INVALID_SET},
        {"transmission_w_per_k", "0.001", "0", CTL_SETTING_INVALID_SET},
        {"adjustment_w_per_k", "0", "-0.001", CTL_SETTING_INVALID_SET},
        {"hysteresis_wh", "0.001", "0", CTL_SETTING_INVALID_SET},
        {"trim_limit_k", "0.001", "0", CTL_SETTING_BAD_VALUE},
        {"integral_w_per_kh", "25", "-1", CTL_SETTING_INVALID_SET},
        {"integral_w_per_kh", "200", "201", CTL_SETTING_INVALID_SET},
        {"integral_w_per_kh", "0", "nan", CTL_SETTING_BAD_VALUE},
        {"integral_w_per_kh", "1", "inf", CTL_SETTING_BAD_VALUE},
        {"integral_limit_w", "300", "0", CTL_SETTING_INVALID_SET},
        {"integral_limit_w", "2000", "2001", CTL_SETTING_INVALID_SET},
        {"integral_limit_w", "1", "nan", CTL_SETTING_BAD_VALUE},
        {"integral_limit_w", "300", "1e999", CTL_SETTING_BAD_VALUE},
        {"internal_w", "-100", "1e999", CTL_SETTING_BAD_VALUE},
        {"heat_cool_heat_value", "1", "2", CTL_SETTING_BAD_VALUE},
        {"heat_cool_heat_value", "-1", "-2", CTL_SETTING_BAD_VALUE},
        {"min_change_interval_s", "0", "-0.001", CTL_SETTING_INVALID_SET},
        {"retry_interval_s", "0", "-0.001", CTL_SETTING_INVALID_SET},
        {"max_retries", "10", "11", CTL_SETTING_INVALID_SET},
        {"max_retries", "0", "-1", CTL_SETTING_INVALID_SET},
        {"smart_grid", "1", "2", CTL_SETTING_BAD_VALUE},
        {"smart_grid", "0", "2", CTL_SETTING_BAD_VALUE},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_settings settings;
        fresh_defaults(&settings);
        check_set(__FILE__, __LINE__, &settings, "rooms", "1");
        check_set(__FILE__, __LINE__, &settings, rows[i].name, rows[i].accepted);
        check_get(__FILE__, __LINE__, &settings, rows[i].name, rows[i].accepted);
        check_refused(__FILE__, __LINE__, &settings, rows[i].name, rows[i].outside,
                      rows[i].status);
    }
}

/* Texts no number setting takes, whatever its range. */
static void test_unparsed_numbers(void)
{
    static const struct {
        const char *name;
        const char *value;
    } rows[] = {
        {"hysteresis_wh", ""},
        {"hysteresis_wh", "abc"},
        {"hysteresis_wh", "12x"},
        {"hysteresis_wh", "{\"v\":1000}"},
        {"hysteresis_wh", "1e12"},
        {"internal_w", "0.0004"},
        {"max_retries", "2.5"},
        {"rooms", "1.5"},
        {"enabled", "yes"},
        {"trim_limit_k", "None"},
        {"hysteresis_wh", NULL},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_settings settings;
        fresh_defaults(&settings);
        check_refused(__FILE__, __LINE__, &settings, rows[i].name, rows[i].value,
                      CTL_SETTING_BAD_VALUE);
    }
}

/* The largest whole number the getter can still write back is taken, and read back unchanged. */
static void test_largest_number(void)
{
    ctl_settings settings;
    fresh_defaults(&settings);
    check_set(__FILE__, __LINE__, &settings, "hysteresis_wh", "999999999999");
    check_get(__FILE__, __LINE__, &settings, "hysteresis_wh", "999999999999");
}

static void test_topics(void)
{
    char longest[CTL_SETTINGS_TOPIC_MAX + 1];
    char too_long[CTL_SETTINGS_TOPIC_MAX + 2];
    const struct {
        const char *value;
        ctl_setting_status status;
    } rows[] = {
        {"ccu/device/status/000A9D89A7961A/1/ACTUAL_TEMPERATURE", CTL_SETTING_OK},
        {"jeisha/commands", CTL_SETTING_OK},
        {longest, CTL_SETTING_OK},
        {"", CTL_SETTING_OK},
        {too_long, CTL_SETTING_BAD_VALUE},
        {"a+b", CTL_SETTING_BAD_VALUE},
        {"a/#", CTL_SETTING_BAD_VALUE},
        {"a b", CTL_SETTING_BAD_VALUE},
        {"a\tb", CTL_SETTING_BAD_VALUE},
        {"a\x7f", CTL_SETTING_BAD_VALUE},
        {NULL, CTL_SETTING_BAD_VALUE},
    };
    static const char *const names[] = {"room.0.topic"};
    size_t i;
    size_t n;
    memset(longest, 'a', CTL_SETTINGS_TOPIC_MAX);
    longest[CTL_SETTINGS_TOPIC_MAX] = '\0';
    memset(too_long, 'a', CTL_SETTINGS_TOPIC_MAX + 1);
    too_long[CTL_SETTINGS_TOPIC_MAX + 1] = '\0';
    for (n = 0; n < sizeof names / sizeof names[0]; n++) {
        for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            ctl_settings settings;
            fresh_defaults(&settings);
            check_set(__FILE__, __LINE__, &settings, "rooms", "1");
            /* A topic that is not the default, so that accepting the empty text changes it. */
            check_set(__FILE__, __LINE__, &settings, names[n], "before");
            if (rows[i].status == CTL_SETTING_OK) {
                check_set(__FILE__, __LINE__, &settings, names[n], rows[i].value);
                check_get(__FILE__, __LINE__, &settings, names[n], rows[i].value);
            } else {
                check_refused(__FILE__, __LINE__, &settings, names[n], rows[i].value,
                              rows[i].status);
            }
        }
    }
}

static void test_rooms(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    fresh_defaults(&settings);
    check_refused(__FILE__, __LINE__, &settings, "room.0.topic", "a", CTL_SETTING_UNKNOWN);
    check_set(__FILE__, __LINE__, &settings, "rooms", "2");
    check_get(__FILE__, __LINE__, &settings, "room.0.topic", "");
    check_get(__FILE__, __LINE__, &settings, "room.1.topic", "");
    check_get(__FILE__, __LINE__, &settings, "room.0.weight", "1");
    check_get(__FILE__, __LINE__, &settings, "room.1.weight", "1");
    check_set(__FILE__, __LINE__, &settings, "room.0.topic", "house/room0");
    check_set(__FILE__, __LINE__, &settings, "room.1.topic", "house/room1");
    check_refused(__FILE__, __LINE__, &settings, "room.2.topic", "a", CTL_SETTING_UNKNOWN);
    check_refused(__FILE__, __LINE__, &settings, "rooms", "17", CTL_SETTING_BAD_VALUE);
    check_set(__FILE__, __LINE__, &settings, "room.1.weight", "0");
    check_refused(__FILE__, __LINE__, &settings, "room.0.weight", "0",
                  CTL_SETTING_INVALID_SET);

    /* A lower count keeps what lies above it; the higher count lists it again. */
    check_set(__FILE__, __LINE__, &settings, "rooms", "1");
    CHECK_UINT(ctl_settings_name_count(&settings), 19);
    CHECK_TRUE(!ctl_settings_get(&settings, "room.1.topic", value, sizeof value));
    check_refused(__FILE__, __LINE__, &settings, "room.1.topic", "a", CTL_SETTING_UNKNOWN);
    check_set(__FILE__, __LINE__, &settings, "rooms", "2");
    check_get(__FILE__, __LINE__, &settings, "room.1.topic", "house/room1");
    check_get(__FILE__, __LINE__, &settings, "room.1.weight", "0");
}

static void test_room_names(void)
{
    static const char *const unknown[] = {
        "room.00.topic", "room.-1.topic", "room..topic", "room.0.temp", "room.0.topic.",
        "room.0",        "room.16.topic", "room.99999999999999999999.weight",
    };
    size_t i;
    ctl_settings settings;
    fresh_defaults(&settings);
    check_set(__FILE__, __LINE__, &settings, "rooms", "16");
    check_set(__FILE__, __LINE__, &settings, "room.15.topic", "house/last");
    for (i = 0; i < sizeof unknown / sizeof unknown[0]; i++) {
        check_refused(__FILE__, __LINE__, &settings, unknown[i], "a", CTL_SETTING_UNKNOWN);
    }
}

static void test_trim_limit(void)
{
    ctl_settings settings;
    fresh_defaults(&settings);
    check_set(__FILE__, __LINE__, &settings, "trim_limit_k", "none");
    CHECK_TRUE(!settings.trim_limit_enabled);
    check_get(__FILE__, __LINE__, &settings, "trim_limit_k", "none");
    check_set(__FILE__, __LINE__, &settings, "trim_limit_k", "2.5");
    CHECK_TRUE(settings.trim_limit_enabled);
    CHECK_DOUBLE(settings.trim_limit_k, 2.5);
    check_get(__FILE__, __LINE__, &settings, "trim_limit_k", "2.5");
    check_refused(__FILE__, __LINE__, &settings, "trim_limit_k", "0", CTL_SETTING_BAD_VALUE);
}

static void test_formatting(void)
{
    /* size 0 offers the whole buffer; expected NULL means the call must refuse. */
    static const struct {
        const char *name;
        double value;
        size_t size;
        const char *expected;
    } rows[] = {
        {"hysteresis_wh", 1500.0, 0, "1500"},
        {"internal_w", 0.02, 0, "0.02"},
        {"desired_indoor_c", 22.5, 0, "22.5"},
        {"adjustment_w_per_k", 805.0, 0, "805"},
        {"internal_w", -0.125, 0, "-0.125"},
        {"internal_w", 100.1, 0, "100.1"},
        {"adjustment_w_per_k", 805.0, 4, "805"},
        {"adjustment_w_per_k", 805.0, 3, NULL},
        {"desired_indoor_c", 22.5, 5, "22.5"},
        {"desired_indoor_c", 22.5, 4, NULL},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_settings settings;
        char value[CTL_SETTINGS_VALUE_MAX];
        const size_t size = rows[i].size == 0 ? sizeof value : rows[i].size;
        bool written;
        fresh_defaults(&settings);
        settings.hysteresis_wh = rows[i].value;
        settings.desired_indoor_c = rows[i].value;
        settings.adjustment_w_per_k = rows[i].value;
        settings.internal_w = rows[i].value;
        written = ctl_settings_get(&settings, rows[i].name, value, size);
        CHECK_TRUE(written == (rows[i].expected != NULL));
        if (rows[i].expected != NULL) {
            CHECK_TEXT(value, rows[i].expected);
        }
    }
    {
        ctl_settings settings;
        char topic[CTL_SETTINGS_VALUE_MAX];
        fresh_defaults(&settings);
        check_set(__FILE__, __LINE__, &settings, "rooms", "1");
        check_set(__FILE__, __LINE__, &settings, "room.0.topic", "gx/room");
        CHECK_TRUE(!ctl_settings_get(&settings, "room.0.topic", topic, strlen("gx/room")));
        CHECK_TRUE(ctl_settings_get(&settings, "room.0.topic", topic, strlen("gx/room") + 1));
        CHECK_TRUE(!ctl_settings_get(&settings, "trim_limit_k", topic, 1));
        CHECK_TRUE(!ctl_settings_get(&settings, "enabled", topic, 1));
    }
}

static void check_core_equal(const ctl_config *actual, const ctl_config *expected)
{
    int i;
    CHECK_DOUBLE(actual->desired_indoor_c, expected->desired_indoor_c);
    CHECK_DOUBLE(actual->transmission_w_per_k, expected->transmission_w_per_k);
    CHECK_DOUBLE(actual->adjustment_w_per_k, expected->adjustment_w_per_k);
    CHECK_DOUBLE(actual->hysteresis_wh, expected->hysteresis_wh);
    CHECK_TRUE(actual->trim_limit_enabled == expected->trim_limit_enabled);
    CHECK_DOUBLE(actual->trim_limit_k, expected->trim_limit_k);
    CHECK_DOUBLE(actual->integral_w_per_kh, expected->integral_w_per_kh);
    CHECK_DOUBLE(actual->integral_limit_w, expected->integral_limit_w);
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

static void check_loop_equal(const ctl_loop_config *actual, const ctl_loop_config *expected)
{
    size_t i;
    check_core_equal(&actual->core, &expected->core);
    CHECK_UINT((uint32_t)actual->rooms.count, (uint32_t)expected->rooms.count);
    for (i = 0; i < expected->rooms.count && i < CTL_ROOMS_MAX; i++) {
        CHECK_DOUBLE(actual->rooms.weight[i], expected->rooms.weight[i]);
    }
    CHECK_DOUBLE(actual->rooms.room_timeout_s, expected->rooms.room_timeout_s);
    CHECK_DOUBLE(actual->router.min_change_interval_s, expected->router.min_change_interval_s);
    CHECK_DOUBLE(actual->router.retry_interval_s, expected->router.retry_interval_s);
    CHECK_UINT((uint32_t)actual->router.max_retries, (uint32_t)expected->router.max_retries);
    CHECK_UINT((uint32_t)actual->router.heat_cool_heat_value,
               (uint32_t)expected->router.heat_cool_heat_value);
    CHECK_UINT(actual->allowed, expected->allowed);
    CHECK_TRUE(actual->assume_landed == expected->assume_landed);
}

static void test_to_loop(void)
{
    const uint8_t heating_and_none =
        (uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE));
    ctl_settings settings;
    ctl_loop_config expected;
    ctl_loop_config actual;

    fresh_defaults(&settings);
    ctl_loop_config_defaults(&expected, 1);
    expected.core.solar_gain_factor = 0.0;
    expected.allowed = heating_and_none;
    expected.assume_landed = false;
    ctl_settings_to_loop(&settings, &actual);
    check_loop_equal(&actual, &expected);

    check_set(__FILE__, __LINE__, &settings, "cooling", "1");
    check_set(__FILE__, __LINE__, &settings, "internal_w", "400");
    ctl_settings_to_loop(&settings, &actual);
    CHECK_UINT(actual.allowed,
               (uint32_t)(heating_and_none | CTL_DEMAND_BIT(CTL_DEMAND_COOLING)));
    CHECK_DOUBLE(actual.core.fallback_w[CTL_SIGNAL_P_INTERNAL], 400.0);
}

/* Every setting reaches its field of the loop's configuration. */
static void test_to_loop_fields(void)
{
    ctl_settings settings;
    ctl_loop_config config;
    fresh_defaults(&settings);
    check_set(__FILE__, __LINE__, &settings, "rooms", "3");
    check_set(__FILE__, __LINE__, &settings, "room.1.weight", "2.5");
    check_set(__FILE__, __LINE__, &settings, "room_timeout_s", "600");
    check_set(__FILE__, __LINE__, &settings, "desired_indoor_c", "21");
    check_set(__FILE__, __LINE__, &settings, "transmission_w_per_k", "160");
    check_set(__FILE__, __LINE__, &settings, "adjustment_w_per_k", "700");
    check_set(__FILE__, __LINE__, &settings, "hysteresis_wh", "1200");
    check_set(__FILE__, __LINE__, &settings, "trim_limit_k", "none");
    check_set(__FILE__, __LINE__, &settings, "integral_w_per_kh", "25");
    check_set(__FILE__, __LINE__, &settings, "integral_limit_w", "500");
    check_set(__FILE__, __LINE__, &settings, "heat_cool_heat_value", "0");
    check_set(__FILE__, __LINE__, &settings, "min_change_interval_s", "400");
    check_set(__FILE__, __LINE__, &settings, "retry_interval_s", "90");
    check_set(__FILE__, __LINE__, &settings, "max_retries", "5");
    ctl_settings_to_loop(&settings, &config);
    CHECK_UINT((uint32_t)config.rooms.count, 3u);
    CHECK_DOUBLE(config.rooms.weight[0], 1.0);
    CHECK_DOUBLE(config.rooms.weight[1], 2.5);
    CHECK_DOUBLE(config.rooms.weight[2], 1.0);
    CHECK_DOUBLE(config.rooms.room_timeout_s, 600.0);
    CHECK_DOUBLE(config.core.desired_indoor_c, 21.0);
    CHECK_DOUBLE(config.core.transmission_w_per_k, 160.0);
    CHECK_DOUBLE(config.core.adjustment_w_per_k, 700.0);
    CHECK_DOUBLE(config.core.hysteresis_wh, 1200.0);
    CHECK_TRUE(!config.core.trim_limit_enabled);
    CHECK_DOUBLE(config.core.integral_w_per_kh, 25.0);
    CHECK_DOUBLE(config.core.integral_limit_w, 500.0);
    CHECK_DOUBLE(config.core.solar_gain_factor, 0.0);
    CHECK_UINT((uint32_t)config.router.heat_cool_heat_value, 0u);
    CHECK_DOUBLE(config.router.min_change_interval_s, 400.0);
    CHECK_DOUBLE(config.router.retry_interval_s, 90.0);
    CHECK_UINT((uint32_t)config.router.max_retries, 5u);
    CHECK_TRUE(ctl_loop_config_valid(&config));
}

/* A file written before the integral settings existed holds none of them: the loader sets only
 * what the file names onto the defaults, so the integral stays off. */
static void test_file_without_integral(void)
{
    ctl_settings settings;
    fresh_defaults(&settings);
    check_set(__FILE__, __LINE__, &settings, "hysteresis_wh", "1200");
    check_set(__FILE__, __LINE__, &settings, "trim_limit_k", "2");
    check_get(__FILE__, __LINE__, &settings, "integral_w_per_kh", "0");
    check_get(__FILE__, __LINE__, &settings, "integral_limit_w", "300");
    CHECK_TRUE(ctl_settings_valid(&settings));
}

static void test_unknown(void)
{
    static const char *const names[] = {"unknown", "", "Enabled", "enabled ", "pv", NULL};
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t i;
    fresh_defaults(&settings);
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        check_refused(__FILE__, __LINE__, &settings, names[i], "1", CTL_SETTING_UNKNOWN);
        CHECK_TRUE(!ctl_settings_get(&settings, names[i], value, sizeof value));
    }
}

int main(void)
{
    test_defaults();
    test_name_list();
    test_numbers();
    test_unparsed_numbers();
    test_largest_number();
    test_topics();
    test_rooms();
    test_room_names();
    test_trim_limit();
    test_formatting();
    test_to_loop();
    test_to_loop_fields();
    test_file_without_integral();
    test_unknown();
    return check_result();
}
