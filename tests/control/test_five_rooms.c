#include "check.h"

#include "adapt/ctl_rooms.h"
#include "core/ctl_types.h"
#include "glue/ctl_form.h"
#include "glue/ctl_gate.h"
#include "glue/ctl_html.h"
#include "glue/ctl_settings.h"
#include "glue/ctl_telemetry.h"
#include "loop/ctl_loop.h"
#include "route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A captured rendering of the owner's house, five rooms: every telemetry entry as it is published
 * and every row of the control page. How many room slots the tables hold must not change a byte. */
#define REFERENCE_PATH "tests/control/data/five_rooms.txt"
#define ROOMS 5
#define ROW_MAX 512 /* the page's chunk */
#define TEXT_MAX 32768
#define VALUE_SIZE 128

static ctl_settings settings;
static ctl_form_report report;
static ctl_loop_record record;
static ctl_rooms rooms;
static ctl_rooms_config rooms_config;
static ctl_telemetry_source source;
static char rendered[TEXT_MAX];
static char reference[TEXT_MAX];

static void append(const char *text)
{
    const size_t length = strlen(rendered);
    CHECK_TRUE(length + strlen(text) < TEXT_MAX);
    if (length + strlen(text) < TEXT_MAX) {
        memcpy(rendered + length, text, strlen(text) + 1);
    }
}

static void set_settings(void)
{
    static const char *const topics[ROOMS] = {
        "home/living/temp", "home/kitchen/temp", "home/bed/temp", "home/bath/temp",
        "home/office/temp",
    };
    static const char *const weights[ROOMS] = {"2.5", "1", "0.5", "0", "1"};
    char name[CTL_SETTINGS_NAME_MAX];
    size_t i;
    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "5") == CTL_SETTING_OK);
    for (i = 0; i < ROOMS; i++) {
        snprintf(name, sizeof name, "room.%u.topic", (unsigned)i);
        CHECK_TRUE(ctl_settings_set(&settings, name, topics[i]) == CTL_SETTING_OK);
        snprintf(name, sizeof name, "room.%u.weight", (unsigned)i);
        CHECK_TRUE(ctl_settings_set(&settings, name, weights[i]) == CTL_SETTING_OK);
    }
    CHECK_TRUE(ctl_settings_set(&settings, "desired_topic", "home/thermostat/set")
               == CTL_SETTING_OK);
    ctl_form_report_start(&report);
}

/* Rooms fresh, stale, without a value and with a timestamp, so that every room row form shows. */
static void set_source(void)
{
    static const double weight[ROOMS] = {2.5, 1.0, 0.5, 0.0, 1.0};
    size_t i;
    ctl_rooms_config_defaults(&rooms_config, ROOMS);
    rooms_config.room_timeout_s = 300.0;
    for (i = 0; i < ROOMS; i++) {
        rooms_config.weight[i] = weight[i];
    }
    ctl_rooms_init(&rooms);
    rooms.has_value[0] = true;
    rooms.value_c[0] = 21.456;
    rooms.t[0] = 500.0;
    rooms.has_value[1] = true;
    rooms.value_c[1] = 19.5;
    rooms.t[1] = 100.0;
    rooms.has_value[3] = true;
    rooms.value_c[3] = 23.0;
    rooms.t[3] = 590.0;
    rooms.has_value[4] = true;
    rooms.value_c[4] = 20.25;
    rooms.t[4] = 580.0;
    rooms.has_timestamp[4] = true;
    rooms.timestamp_ms[4] = INT64_C(1731600000000);
    memset(&record, 0, sizeof record);
    record.t = 600.0;
    record.decision.t = 600.0;
    record.decision.demand = CTL_DEMAND_HEATING;
    record.decision.reason = CTL_REASON_HELD_STALE_INPUT;
    record.decision.phase = CTL_PHASE_RUNNING;
    record.decision.energy_wh = -812.5;
    record.decision.has_inputs = true;
    record.decision.inputs.t_indoor_c = 21.25;
    record.decision.inputs.t_outdoor_c = 3.5;
    record.rooms.has_average = true;
    record.rooms.average_c = 21.25;
    record.rooms.fresh_count = 3;
    record.rooms.fresh_weight = 4.5;
    record.router_status = CTL_ROUTER_IDLE;
    record.sent_state = 2;
    source.record = &record;
    source.rooms = &rooms;
    source.rooms_config = &rooms_config;
    source.uptime_s = 612.7;
    source.enabled = true;
    source.gate = CTL_GATE_OPEN;
    source.desired_c = 21.5;
    source.desired_source = CTL_DESIRED_SETTING;
    source.heap_free = 23456u;
    source.heap_max_block = 11208u;
    source.has_heap_fragmentation = true;
    source.heap_fragmentation = 37u;
}

static void append_message(const char *subtopic, const char *value)
{
    append(subtopic);
    append(" ");
    append(value);
    append("\n");
}

/* Each entry twice: as the MQTT message the glue publishes and as the page's row. */
static void render_group(ctl_group group)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];
    char row[ROW_MAX];
    size_t i;
    for (i = 0; i < ctl_telemetry_count(&source, group); i++) {
        CHECK_TRUE(ctl_telemetry_entry(&source, group, i, subtopic, sizeof subtopic, value,
                                       sizeof value));
        append_message(subtopic, value);
        CHECK_TRUE(ctl_html_row(subtopic, value, row, sizeof row));
        append(row);
        append("\n");
    }
}

static void render_settings(void)
{
    char row[ROW_MAX];
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];
    size_t i;
    append(CTL_HTML_SETTINGS_OPEN);
    append("\n");
    for (i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_telemetry_setting(&settings, i, subtopic, sizeof subtopic, value,
                                         sizeof value));
        append_message(subtopic, value);
        CHECK_TRUE(ctl_form_row(&settings, &report, i, true, row, sizeof row));
        append(row);
        append("\n");
    }
    append(CTL_HTML_SETTINGS_CLOSE);
    append("\n");
}

static void render(void)
{
    rendered[0] = '\0';
    set_settings();
    set_source();
    render_group(CTL_GROUP_STATUS);
    render_group(CTL_GROUP_OUTPUTS);
    render_group(CTL_GROUP_INPUTS);
    render_settings();
}

static size_t read_reference(void)
{
    FILE *file = fopen(REFERENCE_PATH, "rb");
    size_t length = 0;
    CHECK_TRUE(file != NULL);
    if (file != NULL) {
        length = fread(reference, 1, sizeof reference - 1, file);
        fclose(file);
    }
    reference[length] = '\0';
    return length;
}

static void test_five_rooms_match_reference(void)
{
    const size_t length = read_reference();
    size_t at = 0;
    render();
    CHECK_UINT(strlen(rendered), length);
    while (at < length && rendered[at] == reference[at]) {
        at++;
    }
    /* The first byte that differs, so that a failure points at its place. */
    CHECK_UINT(at, length);
    CHECK_TEXT(rendered + at, reference + at);
}

int main(void)
{
    test_five_rooms_match_reference();
    return check_result();
}
