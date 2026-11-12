#include "check.h"

#include "core/ctl_relay.h"
#include "core/ctl_types.h"
#include "loop/ctl_loop.h"
#include "loop/ctl_shadow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REPLY_SIZE 64
#define RECORD_SIZE (CTL_LOOP_TEXT_MAX + 2)
#define COOLING_BIT CTL_DEMAND_BIT(CTL_DEMAND_COOLING)

typedef struct {
    const char *line;
    const char *reply;
} exchange;

static void handle(ctl_shadow *shadow, const char *line, double now, char *reply)
{
    ctl_shadow_handle(shadow, line, now, reply, REPLY_SIZE);
}

/* Each row is checked as "<line> -> <reply>", so that a failure names the line it came from. */
static void check_exchanges(ctl_shadow *shadow, const exchange *rows, size_t count, double now)
{
    char reply[REPLY_SIZE];
    char actual[CTL_SHADOW_LINE_MAX + REPLY_SIZE + 8];
    char expected[CTL_SHADOW_LINE_MAX + REPLY_SIZE + 8];
    size_t i;
    for (i = 0; i < count; i++) {
        handle(shadow, rows[i].line, now, reply);
        snprintf(actual, sizeof actual, "%s -> %s", rows[i].line, reply);
        snprintf(expected, sizeof expected, "%s -> %s", rows[i].line, rows[i].reply);
        CHECK_TEXT(actual, expected);
    }
}

static void start(ctl_shadow *shadow, double now)
{
    char reply[REPLY_SIZE];
    ctl_shadow_init(shadow);
    handle(shadow, "G", now, reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(shadow->started);
}

static void test_init(void)
{
    ctl_shadow shadow;
    ctl_shadow_init(&shadow);
    CHECK_UINT((uint32_t)shadow.config.rooms.count, 4u);
    CHECK_TRUE(shadow.config.assume_landed);
    CHECK_TRUE(!shadow.started);
    CHECK_TRUE(!shadow.has_record);
    CHECK_UINT(shadow.inputs, 0u);
    CHECK_UINT(shadow.records, 0u);
}

static void test_configure_lines(void)
{
    static const exchange rows[] = {
        {"K weight 2 2.5", "ok"},
        {"K rooms 1", "ok"},
        {"K rooms 16", "ok"},
        {"K rooms 3", "ok"},
        {"K weight 3 1", "error"},
        {"K rooms 0", "error"},
        {"K rooms 17", "error"},
        {"K rooms 2.5", "error"},
        {"K rooms", "error"},
        {"K rooms 3 4", "error"},
        {"K rooms x", "error"},
        {"K weight 2", "error"},
        {"K weight 1.5 1", "error"},
        {"K weight 0 {\"v\":1}", "error"},
        {"K weight 0 \t{\"v\":1}", "error"},
        {"K weight 0 1{", "error"},
        {"K weight 0 \t1", "error"},
        {"K weight 0 1\r", "error"},
        {"K rooms 3\t", "error"},
        {"K room_timeout_s 600\n", "error"},
        {"K room_timeout_s 10800", "ok"},
        {"K room_timeout_s abc", "error"},
        {"K solar_gain_factor 0.02", "ok"},
        {"K cooling 2", "error"},
        {"K nothing 1", "error"},
        {"K", "error"},
        {"", "error"},
        {"X", "error"},
    };
    ctl_shadow shadow;
    ctl_shadow_init(&shadow);
    check_exchanges(&shadow, rows, sizeof rows / sizeof rows[0], 0.0);
    CHECK_UINT((uint32_t)shadow.config.rooms.count, 3u);
    CHECK_DOUBLE(shadow.config.rooms.weight[0], 1.0);
    CHECK_DOUBLE(shadow.config.rooms.weight[1], 1.0);
    CHECK_DOUBLE(shadow.config.rooms.weight[2], 1.0);
    CHECK_DOUBLE(shadow.config.rooms.room_timeout_s, 10800.0);
    CHECK_DOUBLE(shadow.config.core.solar_gain_factor, 0.02);
}

static void test_weight_and_values(void)
{
    static const exchange rows[] = {
        {"K room_timeout_s 600", "ok"},
        {"K rooms 3", "ok"},
        {"K weight 2 2.5", "ok"},
        {"K solar_gain_factor 0.05", "ok"},
    };
    ctl_shadow shadow;
    ctl_shadow_init(&shadow);
    check_exchanges(&shadow, rows, sizeof rows / sizeof rows[0], 0.0);
    CHECK_DOUBLE(shadow.config.rooms.room_timeout_s, 600.0);
    CHECK_DOUBLE(shadow.config.rooms.weight[2], 2.5);
    CHECK_DOUBLE(shadow.config.core.solar_gain_factor, 0.05);
}

static void test_cooling(void)
{
    ctl_shadow shadow;
    char reply[REPLY_SIZE];
    ctl_shadow_init(&shadow);
    CHECK_TRUE((shadow.config.allowed & COOLING_BIT) == 0u);
    handle(&shadow, "K cooling 1", 0.0, reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE((shadow.config.allowed & COOLING_BIT) != 0u);
    handle(&shadow, "K cooling 0", 0.0, reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE((shadow.config.allowed & COOLING_BIT) == 0u);
}

static void test_start_and_order(void)
{
    ctl_shadow shadow;
    char reply[REPLY_SIZE];
    ctl_shadow_init(&shadow);
    handle(&shadow, "I outdoor 0 5", 0.0, reply);
    CHECK_TEXT(reply, "error");
    handle(&shadow, "G", 0.0, reply);
    CHECK_TEXT(reply, "ok");
    CHECK_TRUE(shadow.started);
    handle(&shadow, "K rooms 2", 0.0, reply);
    CHECK_TEXT(reply, "error");
    handle(&shadow, "G", 0.0, reply);
    CHECK_TEXT(reply, "error");
    CHECK_UINT(shadow.inputs, 0u);
}

static void test_start_refused(void)
{
    ctl_shadow shadow;
    char reply[REPLY_SIZE];
    ctl_shadow_init(&shadow);
    handle(&shadow, "K room_timeout_s 0", 0.0, reply);
    CHECK_TEXT(reply, "ok");
    handle(&shadow, "G", 0.0, reply);
    CHECK_TEXT(reply, "bad");
    CHECK_TRUE(!shadow.started);
}

static void test_input_lines(void)
{
    static const exchange rows[] = {
        {"I room 0 {\"ts\":1,\"v\":21.0}", "taken"},
        {"I room 9 21.0", "bad"},
        {"I outdoor 0 5", "taken"},
        {"I outdoor 0 99", "refused"},
        {"I valve 0 0", "no_reading"},
        {"I heat 0 3200", "taken"},
        {"I fan 0 1", "error"},
        {"I room x 1", "error"},
        {"I room 0", "error"},
        {"I room", "error"},
        {"I room -1 21.0", "error"},
    };
    char longest[CTL_SHADOW_LINE_MAX + 2];
    char reply[REPLY_SIZE];
    ctl_shadow shadow;
    start(&shadow, 0.0);
    check_exchanges(&shadow, rows, sizeof rows / sizeof rows[0], 1.0);

    memset(longest, '1', sizeof longest - 1);
    memcpy(longest, "I outdoor 0 ", strlen("I outdoor 0 "));
    longest[CTL_SHADOW_LINE_MAX + 1] = '\0';
    handle(&shadow, longest, 1.0, reply);
    CHECK_TEXT(reply, "error");

    handle(&shadow, "S", 1.0, reply);
    CHECK_TEXT(reply, "stats inputs=6 records=0");
}

static void test_longest_line_is_read(void)
{
    char longest[CTL_SHADOW_LINE_MAX + 1];
    char reply[REPLY_SIZE];
    ctl_shadow shadow;
    start(&shadow, 0.0);
    memset(longest, '1', sizeof longest - 1);
    memcpy(longest, "I outdoor 0 ", strlen("I outdoor 0 "));
    longest[CTL_SHADOW_LINE_MAX] = '\0';
    handle(&shadow, longest, 1.0, reply);
    CHECK_TEXT(reply, "refused");
}

static void test_records(void)
{
    char text[RECORD_SIZE];
    char reply[REPLY_SIZE];
    const char *start_of_text = "D t=60.0 demand=NO_DEMAND reason=WAITING_FOR_INPUTS";
    ctl_shadow shadow;

    ctl_shadow_init(&shadow);
    strcpy(text, "untouched");
    CHECK_TRUE(!ctl_shadow_poll(&shadow, 60.0, text, sizeof text));
    CHECK_TEXT(text, "untouched");

    start(&shadow, 0.0);
    CHECK_TRUE(!ctl_shadow_poll(&shadow, 59.0, text, sizeof text));
    CHECK_TEXT(text, "untouched");
    CHECK_TRUE(ctl_shadow_poll(&shadow, 60.0, text, sizeof text));
    CHECK_TRUE(strncmp(text, start_of_text, strlen(start_of_text)) == 0);
    if (strncmp(text, start_of_text, strlen(start_of_text)) != 0) {
        CHECK_TEXT(text, start_of_text);
    }
    CHECK_TRUE(!ctl_shadow_poll(&shadow, 119.0, text, sizeof text));
    CHECK_TRUE(ctl_shadow_poll(&shadow, 120.0, text, sizeof text));
    CHECK_TRUE(strncmp(text, "D t=120.0 ", strlen("D t=120.0 ")) == 0);

    handle(&shadow, "S", 120.0, reply);
    CHECK_TEXT(reply, "stats inputs=0 records=2");
}

static void test_record_waits_for_room(void)
{
    char small[8];
    char text[RECORD_SIZE];
    ctl_shadow shadow;
    start(&shadow, 0.0);
    CHECK_TRUE(!ctl_shadow_poll(&shadow, 60.0, small, sizeof small));
    CHECK_TRUE(shadow.has_record);
    CHECK_UINT(shadow.records, 0u);
    CHECK_TRUE(ctl_shadow_poll(&shadow, 61.0, text, sizeof text));
    CHECK_TRUE(strncmp(text, "D t=60.0 ", strlen("D t=60.0 ")) == 0);
    CHECK_TRUE(!shadow.has_record);
    CHECK_UINT(shadow.records, 1u);
}

int main(void)
{
    test_init();
    test_configure_lines();
    test_weight_and_values();
    test_cooling();
    test_start_and_order();
    test_start_refused();
    test_input_lines();
    test_longest_line_is_read();
    test_records();
    test_record_waits_for_room();
    return check_result();
}
