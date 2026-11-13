#include "check.h"

#include "glue/ctl_settings.h"
#include "glue/ctl_settings_file.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Feeds the whole text from fresh defaults and returns the events in order as letters: T for a
 * text pair, N for a value that is no text. */
static void feed_text(ctl_settings_file *file, ctl_settings *settings, const char *text,
                      char *events, size_t size)
{
    size_t count = 0;
    size_t i;
    ctl_settings_defaults(settings);
    ctl_settings_file_start(file);
    for (i = 0; text[i] != '\0'; i++) {
        ctl_settings_file_event event = ctl_settings_file_feed(file, settings, text[i]);
        if (event != CTL_SETTINGS_FILE_NOTHING && count + 1 < size) {
            events[count++] = event == CTL_SETTINGS_FILE_TEXT ? 'T' : 'N';
        }
    }
    events[count] = '\0';
}

static void check_value(const char *file, int line, const ctl_settings *settings,
                        const char *name, const char *expected)
{
    char value[CTL_SETTINGS_VALUE_MAX];
    check_true(file, line, ctl_settings_get(settings, name, value, sizeof value), name);
    check_text(file, line, value, expected);
}

#define CHECK_VALUE(settings, name, expected) \
    check_value(__FILE__, __LINE__, (settings), (name), (expected))

static void test_complete_object(void)
{
    ctl_settings_file file;
    ctl_settings settings;
    char events[16];
    feed_text(&file, &settings,
              " {\n \"cooling\" : \"1\" ,\"pv_topic\":\"a\\/b\",\"rooms\":\"2\"}  trailing", events,
              sizeof events);
    CHECK_TRUE(ctl_settings_file_complete(&file));
    CHECK_TEXT(events, "TTT");
    CHECK_VALUE(&settings, "cooling", "1");
    CHECK_VALUE(&settings, "pv_topic", "a/b");
    CHECK_VALUE(&settings, "rooms", "2");
}

static void test_last_pair(void)
{
    ctl_settings_file file;
    ctl_settings settings;
    char events[16];
    feed_text(&file, &settings, "{\"nothing\":\"1\"", events, sizeof events);
    CHECK_TEXT(events, "T");
    CHECK_TEXT(file.name, "nothing");
    CHECK_UINT((uint32_t)file.status, (uint32_t)CTL_SETTING_UNKNOWN);
    feed_text(&file, &settings, "{\"hysteresis_wh\":\"x\"", events, sizeof events);
    CHECK_TEXT(file.name, "hysteresis_wh");
    CHECK_UINT((uint32_t)file.status, (uint32_t)CTL_SETTING_BAD_VALUE);
    feed_text(&file, &settings, "{\"hysteresis_wh\":\"900\"", events, sizeof events);
    CHECK_UINT((uint32_t)file.status, (uint32_t)CTL_SETTING_OK);
    CHECK_TRUE(!ctl_settings_file_complete(&file));
}

static void test_values_that_are_no_text(void)
{
    ctl_settings_file file;
    ctl_settings settings;
    char events[16];
    feed_text(&file, &settings,
              "{\"hysteresis_wh\":1200,\"a\":{\"b\":[1,\"}]\\\"\"]},\"c\":true ,"
              "\"smart_grid\":\"1\",\"d\":null}",
              events, sizeof events);
    CHECK_TRUE(ctl_settings_file_complete(&file));
    CHECK_TEXT(events, "NNNTN");
    CHECK_TEXT(file.name, "d");
    CHECK_VALUE(&settings, "smart_grid", "1");
    CHECK_VALUE(&settings, "hysteresis_wh", "1500");
}

static void test_broken_strings(void)
{
    ctl_settings_file file;
    ctl_settings settings;
    char events[16];
    char text[CTL_SETTINGS_VALUE_MAX + 40];
    feed_text(&file, &settings, "{\"pv_topic\":\"a\\u0041\"}", events, sizeof events);
    CHECK_TRUE(ctl_settings_file_complete(&file));
    CHECK_UINT((uint32_t)file.status, (uint32_t)CTL_SETTING_BAD_VALUE);
    CHECK_VALUE(&settings, "pv_topic", "");
    feed_text(&file, &settings, "{\"pv\\ntopic\":\"a\"}", events, sizeof events);
    CHECK_UINT((uint32_t)file.status, (uint32_t)CTL_SETTING_UNKNOWN);
    strcpy(text, "{\"pv_topic\":\"");
    memset(text + strlen(text), 'a', CTL_SETTINGS_VALUE_MAX);
    strcpy(text + strlen("{\"pv_topic\":\"") + CTL_SETTINGS_VALUE_MAX, "\"}");
    feed_text(&file, &settings, text, events, sizeof events);
    CHECK_TRUE(ctl_settings_file_complete(&file));
    CHECK_UINT((uint32_t)file.status, (uint32_t)CTL_SETTING_BAD_VALUE);
    CHECK_VALUE(&settings, "pv_topic", "");
}

static void test_not_an_object(void)
{
    static const char *const texts[] = {
        "", "[]", "\"a\"", "{\"a\" \"b\"}", "{\"a\":\"b\",}", "{\"a\":\"b\" \"c\":\"d\"}",
        "{\"a\":}", "{,}", "{\"a\":\"b\"",
    };
    size_t i;
    for (i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        ctl_settings_file file;
        ctl_settings settings;
        char events[16];
        feed_text(&file, &settings, texts[i], events, sizeof events);
        check_true(__FILE__, __LINE__, !ctl_settings_file_complete(&file), texts[i]);
    }
}

static void test_empty_object(void)
{
    ctl_settings_file file;
    ctl_settings settings;
    char events[16];
    feed_text(&file, &settings, "{ }", events, sizeof events);
    CHECK_TRUE(ctl_settings_file_complete(&file));
    CHECK_TEXT(events, "");
}

static void test_pieces(void)
{
    ctl_settings settings;
    char piece[CTL_SETTINGS_FILE_PIECE_MAX];
    size_t count;
    ctl_settings_defaults(&settings);
    count = ctl_settings_file_pieces(&settings);
    CHECK_UINT((uint32_t)count, (uint32_t)(ctl_settings_name_count(&settings) + 2u));
    CHECK_TRUE(ctl_settings_file_piece(&settings, 0, piece, sizeof piece));
    CHECK_TEXT(piece, "{");
    CHECK_TRUE(ctl_settings_file_piece(&settings, 1, piece, sizeof piece));
    CHECK_TEXT(piece, "\"enabled\":\"0\"");
    CHECK_TRUE(ctl_settings_file_piece(&settings, 2, piece, sizeof piece));
    CHECK_TEXT(piece, ",\"cooling\":\"0\"");
    CHECK_TRUE(ctl_settings_file_piece(&settings, count - 1, piece, sizeof piece));
    CHECK_TEXT(piece, "}\n");
    CHECK_TRUE(!ctl_settings_file_piece(&settings, count, piece, sizeof piece));
    CHECK_TRUE(!ctl_settings_file_piece(&settings, 1, piece, 13));
    CHECK_TRUE(ctl_settings_file_piece(&settings, 1, piece, 14));
}

/* Every setting written and read back, a topic with a quote and a backslash among them. */
static void test_round_trip(void)
{
    ctl_settings written;
    ctl_settings read;
    ctl_settings_file file;
    char piece[CTL_SETTINGS_FILE_PIECE_MAX];
    char name[CTL_SETTINGS_NAME_MAX];
    char expected[CTL_SETTINGS_VALUE_MAX];
    char actual[CTL_SETTINGS_VALUE_MAX];
    size_t i;
    ctl_settings_defaults(&written);
    CHECK_UINT((uint32_t)ctl_settings_set(&written, "rooms", "2"), (uint32_t)CTL_SETTING_OK);
    CHECK_UINT((uint32_t)ctl_settings_set(&written, "room.1.topic", "a\"b\\c"),
               (uint32_t)CTL_SETTING_OK);
    CHECK_UINT((uint32_t)ctl_settings_set(&written, "trim_limit_k", "none"),
               (uint32_t)CTL_SETTING_OK);
    ctl_settings_defaults(&read);
    ctl_settings_file_start(&file);
    for (i = 0; i < ctl_settings_file_pieces(&written); i++) {
        size_t j;
        CHECK_TRUE(ctl_settings_file_piece(&written, i, piece, sizeof piece));
        for (j = 0; piece[j] != '\0'; j++) {
            ctl_settings_file_feed(&file, &read, piece[j]);
        }
    }
    CHECK_TRUE(ctl_settings_file_complete(&file));
    CHECK_UINT((uint32_t)ctl_settings_name_count(&read),
               (uint32_t)ctl_settings_name_count(&written));
    for (i = 0; i < ctl_settings_name_count(&written); i++) {
        CHECK_TRUE(ctl_settings_name_at(&written, i, name, sizeof name));
        CHECK_TRUE(ctl_settings_get(&written, name, expected, sizeof expected));
        CHECK_TRUE(ctl_settings_get(&read, name, actual, sizeof actual));
        CHECK_TEXT(actual, expected);
    }
}

int main(void)
{
    test_complete_object();
    test_last_pair();
    test_values_that_are_no_text();
    test_broken_strings();
    test_not_an_object();
    test_empty_object();
    test_pieces();
    test_round_trip();
    return check_result();
}
