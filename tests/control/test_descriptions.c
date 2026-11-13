#include "check.h"

#include "glue/ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Every listed name, the rooms' included, has a description of its own. */
static void test_every_name(void)
{
    ctl_settings settings;
    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "2") == CTL_SETTING_OK);
    CHECK_UINT(ctl_settings_name_count(&settings), CTL_SETTINGS_FIXED_NAMES + 4);
    for (size_t i = 0; i < ctl_settings_name_count(&settings); i++) {
        char name[CTL_SETTINGS_NAME_MAX];
        char description[CTL_SETTINGS_DESCRIPTION_MAX];
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        description[0] = '\0';
        CHECK_TRUE(ctl_settings_describe(&settings, name, description, sizeof description));
        if (description[0] == '\0') {
            CHECK_TEXT(name, "a name with a description");
        }
    }
}

static void check_description(const ctl_settings *settings, const char *name, const char *expected)
{
    char description[CTL_SETTINGS_DESCRIPTION_MAX];
    CHECK_TRUE(ctl_settings_describe(settings, name, description, sizeof description));
    CHECK_TEXT(description, expected);
}

/* A room's description names the room's number, as its setting's name does. */
static void test_rooms_named(void)
{
    ctl_settings settings;
    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "2") == CTL_SETTING_OK);
    check_description(&settings, "room.0.topic", "MQTT topic of room 0's temperature.");
    check_description(&settings, "room.1.topic", "MQTT topic of room 1's temperature.");
    check_description(&settings, "room.0.weight",
                      "Weight of room 0 in the indoor average, 0 to 1000.");
    check_description(&settings, "room.1.weight",
                      "Weight of room 1 in the indoor average, 0 to 1000.");
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "16") == CTL_SETTING_OK);
    check_description(&settings, "room.15.topic", "MQTT topic of room 15's temperature.");
}

static void test_refused(void)
{
    ctl_settings settings;
    char description[CTL_SETTINGS_DESCRIPTION_MAX];
    ctl_settings_defaults(&settings);
    CHECK_TRUE(!ctl_settings_describe(&settings, "room.0.topic", description, sizeof description));
    CHECK_TRUE(!ctl_settings_describe(&settings, "nonsense", description, sizeof description));
    CHECK_TRUE(!ctl_settings_describe(&settings, NULL, description, sizeof description));
    check_description(&settings, "internal_w", "Heat from people and devices inside, in W.");
    CHECK_TRUE(ctl_settings_describe(&settings, "internal_w", description,
                                     sizeof "Heat from people and devices inside, in W."));
    CHECK_TRUE(!ctl_settings_describe(&settings, "internal_w", description,
                                      sizeof "Heat from people and devices inside, in W." - 1));
    CHECK_TRUE(!ctl_settings_describe(&settings, "internal_w", description, 0));
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "1") == CTL_SETTING_OK);
    CHECK_TRUE(ctl_settings_describe(&settings, "room.0.topic", description,
                                     sizeof "MQTT topic of room 0's temperature."));
    CHECK_TRUE(!ctl_settings_describe(&settings, "room.0.topic", description,
                                      sizeof "MQTT topic of room 0's temperature." - 1));
}

int main(void)
{
    test_every_name();
    test_rooms_named();
    test_refused();
    return check_result();
}
