#include "check.h"

#include "adapt/ctl_rooms.h"
#include "core/ctl_signals.h"
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

/* The page as the firmware rendered it before its constant text moved to flash; where the text
 * lives must not change a byte of it. */
#define REFERENCE_PATH "tests/control/data/control_page.html"
#define ROOMS 3
#define ROW_MAX 512 /* the page's chunk */
#define PAGE_MAX 32768
#define VALUE_SIZE 128

static ctl_settings settings;
static ctl_form form;
static ctl_form_report report;
static ctl_loop_record record;
static ctl_rooms rooms;
static ctl_rooms_config rooms_config;
static ctl_telemetry_source source;
static char page[PAGE_MAX];
static char reference[PAGE_MAX];

static void append(const char *text)
{
    const size_t length = strlen(page);
    CHECK_TRUE(length + strlen(text) < PAGE_MAX);
    if (length + strlen(text) < PAGE_MAX) {
        memcpy(page + length, text, strlen(text) + 1);
    }
}

static ctl_setting_status set_setting(void *context, const char *name, const char *value,
                                      bool *changed)
{
    char before[CTL_SETTINGS_VALUE_MAX];
    char after[CTL_SETTINGS_VALUE_MAX];
    ctl_setting_status status;
    (void)context;
    *changed = false;
    if (!ctl_settings_get(&settings, name, before, sizeof before)) {
        return ctl_settings_set(&settings, name, value);
    }
    status = ctl_settings_set(&settings, name, value);
    *changed = status == CTL_SETTING_OK
               && ctl_settings_get(&settings, name, after, sizeof after)
               && strcmp(before, after) != 0;
    return status;
}

static void keep(const char *name, const char *value)
{
    ctl_form_keep(&form, name, value, strlen(value));
}

/* A save with every answer the page shows: saved, unchanged and two refusals; one topic needs
 * escaping. */
static void set_settings(void)
{
    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "3") == CTL_SETTING_OK);
    ctl_form_start(&form);
    keep("enabled", "1");
    keep("room.0.topic", "home/living&dining/temp");
    keep("room.1.topic", "home/'bed'<room>/\"temp\"");
    keep("room.0.weight", "2.5");
    keep("desired_indoor_c", "21.5");
    keep("trim_limit_k", "none");
    keep("hysteresis_wh", "a lot");
    keep("room.2.weight", "0");
    keep("room.1.weight", "0");
    keep("room.0.weight", "0");
    ctl_form_apply(&form, &settings, set_setting, NULL, &report);
}

static void set_source(void)
{
    static const double weight[ROOMS] = {1.0, 2.0, 0.5};
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
    memset(&record, 0, sizeof record);
    record.t = 600.0;
    record.decision.t = 600.0;
    record.decision.demand = CTL_DEMAND_HEATING;
    record.decision.reason = CTL_REASON_HELD_STALE_INPUT;
    record.decision.phase = CTL_PHASE_RUNNING;
    record.decision.energy_wh = -1523.44;
    record.decision.integrated_s = 590.0;
    record.decision.skipped_s = 10.0;
    record.decision.has_inputs = true;
    record.decision.inputs.t_indoor_c = 22.456;
    record.decision.inputs.t_outdoor_c = -5.25;
    record.decision.inputs.p_produced_w = 3200.0;
    record.decision.inputs.p_internal_w = 550.0;
    record.decision.balance.p_net_w = -2735.4;
    record.decision.balance.p_integral_w = 12.34;
    record.decision.ledger.integral_wh = -45.67;
    record.decision.unhealthy = (uint8_t)CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR);
    record.decision.substituted =
        (uint8_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_P_SOLAR) | CTL_SIGNAL_BIT(CTL_SIGNAL_P_INTERNAL));
    record.rooms.fresh_count = 1;
    record.rooms.fresh_weight = 1.0;
    record.commands.count = 1;
    strcpy(record.commands.command[0].name, "SetExternalThermostat1State");
    strcpy(record.commands.command[0].value, "2");
    record.router_status = CTL_ROUTER_WAITING;
    record.sent_state = 2;
    source.record = &record;
    source.rooms = &rooms;
    source.rooms_config = &rooms_config;
    source.uptime_s = 612.7;
    source.enabled = true;
    source.gate = CTL_GATE_LISTEN_ONLY;
    source.desired_c = 21.5;
    source.desired_source = CTL_DESIRED_SETTING;
    source.heap_free = 23456u;
    source.heap_max_block = 11208u;
    source.has_heap_fragmentation = true;
    source.heap_fragmentation = 37u;
}

static void render_group(ctl_group group)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];
    char row[ROW_MAX];
    size_t i;
    for (i = 0; i < ctl_telemetry_count(&source, group); i++) {
        CHECK_TRUE(ctl_telemetry_entry(&source, group, i, subtopic, sizeof subtopic, value,
                                       sizeof value));
        CHECK_TRUE(ctl_html_row(subtopic, value, row, sizeof row));
        append(row);
    }
}

/* The rows of the page in the order the firmware sends them, the settings as editable rows
 * and once more as rows too long to show, and the settings' telemetry. */
static void render_page(void)
{
    char row[ROW_MAX];
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t i;
    page[0] = '\0';
    render_group(CTL_GROUP_STATUS);
    render_group(CTL_GROUP_OUTPUTS);
    render_group(CTL_GROUP_INPUTS);
    append(CTL_HTML_SETTINGS_OPEN);
    for (i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_form_row(&settings, &report, i, true, row, sizeof row));
        append(row);
        CHECK_TRUE(ctl_form_row(&settings, &report, i, false, row, sizeof row));
        append(row);
        CHECK_TRUE(ctl_telemetry_setting(&settings, i, subtopic, sizeof subtopic, value,
                                         sizeof value));
        CHECK_TRUE(ctl_html_row(subtopic, value, row, sizeof row));
        append(row);
    }
    append(CTL_HTML_SETTINGS_CLOSE);
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

static void test_page_matches_reference(void)
{
    const size_t length = read_reference();
    size_t at = 0;
    set_settings();
    set_source();
    render_page();
    CHECK_UINT(strlen(page), length);
    while (at < length && page[at] == reference[at]) {
        at++;
    }
    /* The first byte that differs, so that a failure points at its place. */
    CHECK_UINT(at, length);
    CHECK_TEXT(page + at, reference + at);
}

int main(void)
{
    test_page_matches_reference();
    return check_result();
}
