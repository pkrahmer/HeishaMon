#include "check.h"

#include "adapt/ctl_rooms.h"
#include "glue/ctl_settings.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FILE_ROOMS 10

static void check_status(const char *file, int line, ctl_setting_status actual,
                         ctl_setting_status expected)
{
    check_uint(file, line, (uint32_t)actual, (uint32_t)expected);
}

static void check_value(const char *file, int line, const ctl_settings *settings,
                        const char *name, const char *expected)
{
    char value[CTL_SETTINGS_VALUE_MAX];
    check_true(file, line, ctl_settings_get(settings, name, value, sizeof value), name);
    check_text(file, line, value, expected);
}

/* Compares what the getter writes, so that the structs' padding plays no part. */
static void check_same_settings(const char *file, int line, const ctl_settings *actual,
                                const ctl_settings *expected)
{
    char name[CTL_SETTINGS_NAME_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    const size_t count = ctl_settings_name_count(expected);
    size_t i;
    check_uint(file, line, (uint32_t)ctl_settings_name_count(actual), (uint32_t)count);
    for (i = 0; i < count; i++) {
        check_true(file, line, ctl_settings_name_at(expected, i, name, sizeof name), "name_at");
        check_true(file, line, ctl_settings_get(expected, name, value, sizeof value), name);
        check_value(file, line, actual, name, value);
    }
}

static void test_limit_is_eight(void)
{
    CHECK_UINT((uint32_t)CTL_ROOMS_MAX, 8u);
}

/* A message or a form sets rooms and a room's topic through the setter alone. */
static void test_setter_refuses_past_the_limit(void)
{
    ctl_settings settings;
    ctl_settings_defaults(&settings);
    check_status(__FILE__, __LINE__, ctl_settings_set(&settings, "rooms", "9"),
                 CTL_SETTING_BAD_VALUE);
    check_value(__FILE__, __LINE__, &settings, "rooms", "0");
    check_status(__FILE__, __LINE__, ctl_settings_set(&settings, "rooms", "8"), CTL_SETTING_OK);
    check_status(__FILE__, __LINE__, ctl_settings_set(&settings, "room.7.topic", "house/eighth"),
                 CTL_SETTING_OK);
    check_value(__FILE__, __LINE__, &settings, "room.7.topic", "house/eighth");
    check_status(__FILE__, __LINE__, ctl_settings_set(&settings, "room.8.topic", "house/ninth"),
                 CTL_SETTING_UNKNOWN);
    check_status(__FILE__, __LINE__, ctl_settings_set(&settings, "room.8.weight", "1"),
                 CTL_SETTING_UNKNOWN);
    check_status(__FILE__, __LINE__, ctl_settings_set(&settings, "rooms", "9"),
                 CTL_SETTING_BAD_VALUE);
    check_value(__FILE__, __LINE__, &settings, "rooms", "8");
    CHECK_UINT((uint32_t)ctl_settings_name_count(&settings),
               (uint32_t)(CTL_SETTINGS_FIXED_NAMES + 2 * 8));
    CHECK_UINT((uint32_t)CTL_SETTINGS_NAMES_MAX, (uint32_t)(CTL_SETTINGS_FIXED_NAMES + 2 * 8));
}

/* A file written for more rooms, in the order the settings list their names. */
static void test_file_with_ten_rooms_loads_eight(void)
{
    ctl_settings settings;
    char name[CTL_SETTINGS_NAME_MAX];
    char topic[CTL_SETTINGS_TOPIC_MAX + 1];
    size_t i;
    ctl_settings_defaults(&settings);
    check_status(__FILE__, __LINE__, ctl_settings_load(&settings, "rooms", "10"),
                 CTL_SETTING_OK);
    check_value(__FILE__, __LINE__, &settings, "rooms", "8");
    for (i = 0; i < FILE_ROOMS; i++) {
        const ctl_setting_status expected = i < 8 ? CTL_SETTING_OK : CTL_SETTING_UNKNOWN;
        snprintf(name, sizeof name, "room.%u.topic", (unsigned)i);
        snprintf(topic, sizeof topic, "house/room%u", (unsigned)i);
        check_status(__FILE__, __LINE__, ctl_settings_load(&settings, name, topic), expected);
        snprintf(name, sizeof name, "room.%u.weight", (unsigned)i);
        check_status(__FILE__, __LINE__, ctl_settings_load(&settings, name, "2"), expected);
    }
    check_value(__FILE__, __LINE__, &settings, "room.0.topic", "house/room0");
    check_value(__FILE__, __LINE__, &settings, "room.7.topic", "house/room7");
    check_value(__FILE__, __LINE__, &settings, "room.7.weight", "2");
    CHECK_TRUE(ctl_settings_valid(&settings));
}

/* Only a count above the limit is capped; everything else is the setter's answer. */
static void test_load_is_the_setter_otherwise(void)
{
    static const struct {
        const char *name;
        const char *value;
        ctl_setting_status status;
    } rows[] = {
        {"rooms", "5", CTL_SETTING_OK},          {"rooms", "8", CTL_SETTING_OK},
        {"rooms", "-1", CTL_SETTING_BAD_VALUE},  {"rooms", "9.5", CTL_SETTING_BAD_VALUE},
        {"rooms", "many", CTL_SETTING_BAD_VALUE}, {"rooms", NULL, CTL_SETTING_BAD_VALUE},
        {"rooms", "1e99", CTL_SETTING_BAD_VALUE}, {"room.0.topic", "a", CTL_SETTING_UNKNOWN},
        {"cooling", "2", CTL_SETTING_BAD_VALUE},  {NULL, "1", CTL_SETTING_UNKNOWN},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_settings loaded;
        ctl_settings set;
        ctl_settings_defaults(&loaded);
        ctl_settings_defaults(&set);
        check_status(__FILE__, __LINE__, ctl_settings_load(&loaded, rows[i].name, rows[i].value),
                     rows[i].status);
        check_status(__FILE__, __LINE__, ctl_settings_set(&set, rows[i].name, rows[i].value),
                     rows[i].status);
        check_same_settings(__FILE__, __LINE__, &loaded, &set);
    }
}

int main(void)
{
    test_limit_is_eight();
    test_setter_refuses_past_the_limit();
    test_file_with_ten_rooms_loads_eight();
    test_load_is_the_setter_otherwise();
    return check_result();
}
