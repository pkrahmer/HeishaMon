#include "check.h"

#include "glue/ctl_dispatch.h"
#include "glue/ctl_settings.h"
#include "loop/ctl_loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE "panasonic_heat_pump"
#define SET_PREFIX BASE "/control/set/"

typedef struct {
    const char *topic;
    ctl_topic_kind kind;
    ctl_input input;
    size_t room;
    const char *name;
} topic_row;

static void set_or_fail(ctl_settings *settings, const char *name, const char *value)
{
    ctl_setting_status status = ctl_settings_set(settings, name, value);
    check_true(__FILE__, __LINE__, status == CTL_SETTING_OK, name);
}

/* Two rooms, a/room1 and a/room2, and PV on pv/dc. */
static void two_rooms(ctl_settings *settings)
{
    ctl_settings_defaults(settings);
    set_or_fail(settings, "rooms", "2");
    set_or_fail(settings, "room.0.topic", "a/room1");
    set_or_fail(settings, "room.1.topic", "a/room2");
    set_or_fail(settings, "pv_topic", "pv/dc");
}

static void check_row(const char *file, int line, const ctl_settings *settings,
                      const topic_row *row)
{
    int failures_before = check_failures();
    ctl_topic actual = ctl_dispatch_topic(settings, BASE, row->topic);
    /* A field the kind does not use is checked too: it is zero, as the row's placeholder is. */
    check_uint(file, line, (uint32_t)actual.kind, (uint32_t)row->kind);
    check_uint(file, line, (uint32_t)actual.input, (uint32_t)row->input);
    check_uint(file, line, (uint32_t)actual.room, (uint32_t)row->room);
    check_text(file, line, actual.name, row->name);
    if (check_failures() != failures_before) {
        printf("%s:%d: the row above is the topic \"%s\"\n", file, line, row->topic);
    }
}

static void test_topics(void)
{
    static char longest_name[CTL_SETTINGS_NAME_MAX];
    static char too_long_name[CTL_SETTINGS_NAME_MAX + 1];
    static char longest_topic[sizeof SET_PREFIX + CTL_SETTINGS_NAME_MAX];
    static char too_long_topic[sizeof SET_PREFIX + CTL_SETTINGS_NAME_MAX + 1];
    ctl_settings settings;
    size_t i;
    memset(longest_name, 'x', CTL_SETTINGS_NAME_MAX - 1);
    memset(too_long_name, 'x', CTL_SETTINGS_NAME_MAX);
    strcpy(longest_topic, SET_PREFIX);
    strcat(longest_topic, longest_name);
    strcpy(too_long_topic, SET_PREFIX);
    strcat(too_long_topic, too_long_name);
    {
        const topic_row rows[] = {
            {"a/room1", CTL_TOPIC_INPUT, CTL_INPUT_ROOM, 0, ""},
            {"a/room2", CTL_TOPIC_INPUT, CTL_INPUT_ROOM, 1, ""},
            {"pv/dc", CTL_TOPIC_INPUT, CTL_INPUT_PV, 0, ""},
            {SET_PREFIX "hysteresis_wh", CTL_TOPIC_SETTING, CTL_INPUT_ROOM, 0, "hysteresis_wh"},
            {SET_PREFIX "room.1.topic", CTL_TOPIC_SETTING, CTL_INPUT_ROOM, 0, "room.1.topic"},
            {longest_topic, CTL_TOPIC_SETTING, CTL_INPUT_ROOM, 0, longest_name},
            {too_long_topic, CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {SET_PREFIX, CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {SET_PREFIX "a/b", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {BASE "/main/Outside_Temp", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {BASE "/control/set", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {"other_base/control/set/hysteresis_wh", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {"a/room3", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {"a/room", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {"a/room10", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {"pv/dc/x", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
            {"", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
        };
        two_rooms(&settings);
        for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            check_row(__FILE__, __LINE__, &settings, &rows[i]);
        }
    }
}

static void test_first_equal_room_wins(void)
{
    const topic_row row = {"a/room1", CTL_TOPIC_INPUT, CTL_INPUT_ROOM, 0, ""};
    ctl_settings settings;
    two_rooms(&settings);
    set_or_fail(&settings, "room.1.topic", "a/room1");
    check_row(__FILE__, __LINE__, &settings, &row);
}

static void test_rooms_above_the_count_match_nothing(void)
{
    const topic_row rows[] = {
        {"a/room1", CTL_TOPIC_INPUT, CTL_INPUT_ROOM, 0, ""},
        {"a/room2", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""},
    };
    ctl_settings settings;
    size_t i;
    two_rooms(&settings);
    set_or_fail(&settings, "rooms", "1");
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_row(__FILE__, __LINE__, &settings, &rows[i]);
    }
}

static void test_empty_topics_match_nothing(void)
{
    const topic_row row = {"", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""};
    ctl_settings settings;
    ctl_settings_defaults(&settings);
    set_or_fail(&settings, "rooms", "1");
    check_row(__FILE__, __LINE__, &settings, &row);
}

static void set_pump_topics(ctl_settings *settings)
{
    set_or_fail(settings, "outdoor_topic", "jeisha/main/Outside_Temp");
    set_or_fail(settings, "heat_topic", "jeisha/main/Heat_Power_Production");
    set_or_fail(settings, "valve_topic", "jeisha/main/ThreeWay_Valve_State");
    set_or_fail(settings, "defrost_topic", "jeisha/main/Defrosting_State");
}

static void test_pump_topics(void)
{
    static const struct {
        bool pump_topics_set;
        const char *shared_name; /* NULL: no setting shares a topic */
        const char *shared_topic;
        topic_row row;
    } rows[] = {
        {true, NULL, NULL,
         {"jeisha/main/Outside_Temp", CTL_TOPIC_INPUT, CTL_INPUT_OUTDOOR, 0, ""}},
        {true, NULL, NULL,
         {"jeisha/main/Heat_Power_Production", CTL_TOPIC_INPUT, CTL_INPUT_HEAT, 0, ""}},
        {true, NULL, NULL,
         {"jeisha/main/ThreeWay_Valve_State", CTL_TOPIC_INPUT, CTL_INPUT_VALVE, 0, ""}},
        {true, NULL, NULL,
         {"jeisha/main/Defrosting_State", CTL_TOPIC_INPUT, CTL_INPUT_DEFROST, 0, ""}},
        {true, NULL, NULL, {"a/room1", CTL_TOPIC_INPUT, CTL_INPUT_ROOM, 0, ""}},
        {true, NULL, NULL, {"pv/dc", CTL_TOPIC_INPUT, CTL_INPUT_PV, 0, ""}},
        {false, NULL, NULL, {"jeisha/main/Outside_Temp", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""}},
        {false, NULL, NULL,
         {"jeisha/main/Defrosting_State", CTL_TOPIC_NONE, CTL_INPUT_ROOM, 0, ""}},
        {true, "room.1.topic", "jeisha/main/Outside_Temp",
         {"jeisha/main/Outside_Temp", CTL_TOPIC_INPUT, CTL_INPUT_ROOM, 1, ""}},
        {true, "pv_topic", "jeisha/main/Outside_Temp",
         {"jeisha/main/Outside_Temp", CTL_TOPIC_INPUT, CTL_INPUT_PV, 0, ""}},
        {true, "outdoor_topic", "jeisha/main/Heat_Power_Production",
         {"jeisha/main/Heat_Power_Production", CTL_TOPIC_INPUT, CTL_INPUT_OUTDOOR, 0, ""}},
        {true, "heat_topic", "jeisha/main/ThreeWay_Valve_State",
         {"jeisha/main/ThreeWay_Valve_State", CTL_TOPIC_INPUT, CTL_INPUT_HEAT, 0, ""}},
        {true, "valve_topic", "jeisha/main/Defrosting_State",
         {"jeisha/main/Defrosting_State", CTL_TOPIC_INPUT, CTL_INPUT_VALVE, 0, ""}},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_settings settings;
        two_rooms(&settings);
        if (rows[i].pump_topics_set) {
            set_pump_topics(&settings);
        }
        if (rows[i].shared_name != NULL) {
            set_or_fail(&settings, rows[i].shared_name, rows[i].shared_topic);
        }
        check_row(__FILE__, __LINE__, &settings, &rows[i].row);
    }
}

static void check_none(const char *file, int line, ctl_topic actual)
{
    check_uint(file, line, (uint32_t)actual.kind, (uint32_t)CTL_TOPIC_NONE);
    check_uint(file, line, (uint32_t)actual.input, (uint32_t)CTL_INPUT_ROOM);
    check_uint(file, line, (uint32_t)actual.room, 0);
    check_text(file, line, actual.name, "");
}

static void test_null_arguments_give_none(void)
{
    ctl_settings settings;
    two_rooms(&settings);
    check_none(__FILE__, __LINE__, ctl_dispatch_topic(NULL, BASE, "a/room1"));
    check_none(__FILE__, __LINE__, ctl_dispatch_topic(&settings, NULL, SET_PREFIX "rooms"));
    check_none(__FILE__, __LINE__, ctl_dispatch_topic(&settings, BASE, NULL));
}

static void test_thermostat_state(void)
{
    static const struct {
        uint8_t byte_6;
        int state;
    } rows[] = {{0x40, 0}, {0x44, 1}, {0x48, 2}, {0x4C, 3}, {0xF3, 0}};
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_UINT((uint32_t)ctl_dispatch_thermostat_state(rows[i].byte_6),
                   (uint32_t)rows[i].state);
    }
}

int main(void)
{
    test_topics();
    test_first_equal_room_wins();
    test_rooms_above_the_count_match_nothing();
    test_empty_topics_match_nothing();
    test_pump_topics();
    test_null_arguments_give_none();
    test_thermostat_state();
    return check_result();
}
