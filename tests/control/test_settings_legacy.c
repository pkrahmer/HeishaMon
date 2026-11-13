#include "check.h"

#include "glue/ctl_settings.h"

#include <stddef.h>
#include <string.h>

/* The pairs the loader hands the setter for a file an older firmware wrote, in file order:
 * {"heat_topic":"a/b","outdoor_topic":"c/d","hysteresis_wh":"1200"}, with the setter's answer. */
typedef struct {
    const char *name;
    const char *value;
    ctl_setting_status expected;
} legacy_pair;

static const legacy_pair LEGACY_PAIRS[] = {
    {"heat_topic", "a/b", CTL_SETTING_UNKNOWN},
    {"outdoor_topic", "c/d", CTL_SETTING_UNKNOWN},
    {"hysteresis_wh", "1200", CTL_SETTING_OK},
};

static void load_legacy(ctl_settings *settings)
{
    size_t i;
    ctl_settings_defaults(settings);
    for (i = 0; i < sizeof LEGACY_PAIRS / sizeof LEGACY_PAIRS[0]; i++) {
        CHECK_UINT(ctl_settings_set(settings, LEGACY_PAIRS[i].name, LEGACY_PAIRS[i].value),
                   LEGACY_PAIRS[i].expected);
    }
}

/* The text the glue writes: every listed name with its value, in the list's order. */
static void write_text(const ctl_settings *settings, char *text, size_t size)
{
    char name[CTL_SETTINGS_NAME_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t count = ctl_settings_name_count(settings);
    size_t i;
    text[0] = '\0';
    for (i = 0; i < count; i++) {
        CHECK_TRUE(ctl_settings_name_at(settings, i, name, sizeof name));
        CHECK_TRUE(ctl_settings_get(settings, name, value, sizeof value));
        CHECK_TRUE(strlen(text) + strlen(name) + strlen(value) + 7 < size);
        if (strlen(text) + strlen(name) + strlen(value) + 7 < size) {
            strcat(text, i == 0 ? "{\"" : ",\"");
            strcat(text, name);
            strcat(text, "\":\"");
            strcat(text, value);
            strcat(text, "\"");
        }
    }
    strcat(text, "}");
}

static void test_removed_names_change_nothing(void)
{
    ctl_settings defaults;
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    char expected[CTL_SETTINGS_VALUE_MAX];
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    ctl_settings_defaults(&defaults);
    load_legacy(&settings);
    CHECK_TRUE(ctl_settings_valid(&settings));
    CHECK_UINT(ctl_settings_name_count(&settings), ctl_settings_name_count(&defaults));
    for (i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        if (strcmp(name, "hysteresis_wh") != 0) {
            CHECK_TRUE(ctl_settings_get(&settings, name, value, sizeof value));
            CHECK_TRUE(ctl_settings_get(&defaults, name, expected, sizeof expected));
            CHECK_TEXT(value, expected);
        }
    }
    CHECK_TRUE(!ctl_settings_get(&settings, "heat_topic", value, sizeof value));
    CHECK_TRUE(!ctl_settings_get(&settings, "outdoor_topic", value, sizeof value));
}

static void test_hysteresis_applies(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    load_legacy(&settings);
    CHECK_TRUE(ctl_settings_get(&settings, "hysteresis_wh", value, sizeof value));
    CHECK_TEXT(value, "1200");
}

static void test_written_text_drops_them(void)
{
    ctl_settings settings;
    static char text[CTL_SETTINGS_NAMES_MAX * (CTL_SETTINGS_NAME_MAX + CTL_SETTINGS_VALUE_MAX + 6)];
    load_legacy(&settings);
    write_text(&settings, text, sizeof text);
    CHECK_TRUE(strstr(text, "\"hysteresis_wh\":\"1200\"") != NULL);
    CHECK_TRUE(strstr(text, "heat_topic") == NULL);
    CHECK_TRUE(strstr(text, "outdoor_topic") == NULL);
}

int main(void)
{
    test_removed_names_change_nothing();
    test_hysteresis_applies();
    test_written_text_drops_them();
    return check_result();
}
