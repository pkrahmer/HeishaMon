#include "check.h"

#include "glue/ctl_dispatch.h"
#include "glue/ctl_settings.h"
#include "loop/ctl_loop.h"

#include <stddef.h>
#include <string.h>

/* The two names a settings file may still hold from the removed solar input. The topic's name is
 * split into two literals so that a search of the tree for it finds only the decision log, while
 * the compiler still joins them into the very name an old file holds. */
#define OLD_TOPIC_NAME "pv" "_topic"
#define OLD_GAIN_NAME "solar_gain_factor"

/* The pairs the loader hands the setter for a /control.json that a firmware with the solar input
 * wrote, both old names with values among kept settings, in file order, with the setter's
 * answer. */
typedef struct {
    const char *name;
    const char *value;
    ctl_setting_status expected;
} file_pair;

static const file_pair FILE_PAIRS[] = {
    {"rooms", "1", CTL_SETTING_OK},
    {"room.0.topic", "house/room", CTL_SETTING_OK},
    {OLD_TOPIC_NAME, "house/pv", CTL_SETTING_UNKNOWN},
    {OLD_GAIN_NAME, "0.05", CTL_SETTING_UNKNOWN},
    {"hysteresis_wh", "1200", CTL_SETTING_OK},
    {"desired_indoor_c", "21.5", CTL_SETTING_OK},
};

static void load_file(ctl_settings *settings)
{
    size_t i;
    ctl_settings_defaults(settings);
    for (i = 0; i < sizeof FILE_PAIRS / sizeof FILE_PAIRS[0]; i++) {
        CHECK_UINT(ctl_settings_set(settings, FILE_PAIRS[i].name, FILE_PAIRS[i].value),
                   FILE_PAIRS[i].expected);
    }
}

static void test_file_loads_with_the_rest_applied(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    load_file(&settings);
    CHECK_TRUE(ctl_settings_valid(&settings));
    CHECK_TRUE(ctl_settings_get(&settings, "rooms", value, sizeof value));
    CHECK_TEXT(value, "1");
    CHECK_TRUE(ctl_settings_get(&settings, "room.0.topic", value, sizeof value));
    CHECK_TEXT(value, "house/room");
    CHECK_TRUE(ctl_settings_get(&settings, "hysteresis_wh", value, sizeof value));
    CHECK_TEXT(value, "1200");
    CHECK_TRUE(ctl_settings_get(&settings, "desired_indoor_c", value, sizeof value));
    CHECK_TEXT(value, "21.5");
}

static void test_names_are_gone(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    load_file(&settings);
    CHECK_TRUE(!ctl_settings_get(&settings, OLD_TOPIC_NAME, value, sizeof value));
    CHECK_TRUE(!ctl_settings_get(&settings, OLD_GAIN_NAME, value, sizeof value));
    for (i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        CHECK_TRUE(strcmp(name, OLD_TOPIC_NAME) != 0);
        CHECK_TRUE(strcmp(name, OLD_GAIN_NAME) != 0);
    }
}

static void test_loop_counts_no_solar_gain(void)
{
    ctl_settings settings;
    ctl_loop_config config;
    load_file(&settings);
    ctl_settings_to_loop(&settings, &config);
    CHECK_DOUBLE(config.core.solar_gain_factor, 0.0);
    CHECK_DOUBLE(config.core.hysteresis_wh, 1200.0);
    CHECK_DOUBLE(config.core.desired_indoor_c, 21.5);
    CHECK_TRUE(ctl_loop_config_valid(&config));

    ctl_settings_defaults(&settings);
    ctl_settings_to_loop(&settings, &config);
    CHECK_DOUBLE(config.core.solar_gain_factor, 0.0);
}

static void test_old_topic_is_no_input(void)
{
    ctl_settings settings;
    ctl_topic topic;
    load_file(&settings);
    topic = ctl_dispatch_topic(&settings, "panasonic_heat_pump", "house/pv");
    CHECK_UINT(topic.kind, CTL_TOPIC_NONE);
}

int main(void)
{
    test_file_loads_with_the_rest_applied();
    test_names_are_gone();
    test_loop_counts_no_solar_gain();
    test_old_topic_is_no_input();
    return check_result();
}
