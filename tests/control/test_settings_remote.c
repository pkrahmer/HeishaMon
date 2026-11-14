#include "check.h"

#include "glue/ctl_settings.h"

#include <stddef.h>
#include <string.h>

/* The pairs the loader hands the setter for a file of a board that once commanded another one:
 * {"enabled":"1","frame_log_topic":"jeisha/log","pv_topic":"house/pv",
 *  "command_topic":"panasonic_heat_pump","hysteresis_wh":"1200"}, with the setter's answer. */
typedef struct {
    const char *name;
    const char *value;
    ctl_setting_status expected;
} file_pair;

static const file_pair FILE_PAIRS[] = {
    {"enabled", "1", CTL_SETTING_OK},
    {"frame_log_topic", "jeisha/log", CTL_SETTING_UNKNOWN},
    {"pv_topic", "house/pv", CTL_SETTING_OK},
    {"command_topic", "panasonic_heat_pump", CTL_SETTING_UNKNOWN},
    {"hysteresis_wh", "1200", CTL_SETTING_OK},
};

#define PAIR_COUNT (sizeof FILE_PAIRS / sizeof FILE_PAIRS[0])

static void load_file(ctl_settings *settings)
{
    size_t i;
    ctl_settings_defaults(settings);
    for (i = 0; i < PAIR_COUNT; i++) {
        CHECK_UINT(ctl_settings_set(settings, FILE_PAIRS[i].name, FILE_PAIRS[i].value),
                   FILE_PAIRS[i].expected);
    }
}

/* The value the file gives the name, or NULL when the file applies none to it. */
static const char *applied_value(const char *name)
{
    size_t i;
    for (i = 0; i < PAIR_COUNT; i++) {
        if (FILE_PAIRS[i].expected == CTL_SETTING_OK && strcmp(FILE_PAIRS[i].name, name) == 0) {
            return FILE_PAIRS[i].value;
        }
    }
    return NULL;
}

static void test_file_loads_without_them(void)
{
    ctl_settings defaults;
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    char expected[CTL_SETTINGS_VALUE_MAX];
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    ctl_settings_defaults(&defaults);
    load_file(&settings);
    CHECK_TRUE(ctl_settings_valid(&settings));
    CHECK_UINT(ctl_settings_name_count(&settings), ctl_settings_name_count(&defaults));
    for (i = 0; i < ctl_settings_name_count(&settings); i++) {
        const char *applied;
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        CHECK_TRUE(ctl_settings_get(&settings, name, value, sizeof value));
        applied = applied_value(name);
        if (applied == NULL) {
            CHECK_TRUE(ctl_settings_get(&defaults, name, expected, sizeof expected));
            CHECK_TEXT(value, expected);
        } else {
            CHECK_TEXT(value, applied);
        }
    }
}

static void test_every_other_setting_applies(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t i;
    load_file(&settings);
    for (i = 0; i < PAIR_COUNT; i++) {
        if (FILE_PAIRS[i].expected == CTL_SETTING_OK) {
            CHECK_TRUE(ctl_settings_get(&settings, FILE_PAIRS[i].name, value, sizeof value));
            CHECK_TEXT(value, FILE_PAIRS[i].value);
        }
    }
}

static void test_names_are_gone(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    load_file(&settings);
    CHECK_TRUE(!ctl_settings_get(&settings, "frame_log_topic", value, sizeof value));
    CHECK_TRUE(!ctl_settings_get(&settings, "command_topic", value, sizeof value));
    for (i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        CHECK_TRUE(strcmp(name, "frame_log_topic") != 0);
        CHECK_TRUE(strcmp(name, "command_topic") != 0);
    }
}

int main(void)
{
    test_file_loads_without_them();
    test_every_other_setting_applies();
    test_names_are_gone();
    return check_result();
}
