#include "check.h"

#include "adapt/ctl_rooms.h"
#include "core/ctl_signals.h"
#include "core/ctl_types.h"
#include "glue/ctl_gate.h"
#include "glue/ctl_settings.h"
#include "glue/ctl_telemetry.h"
#include "loop/ctl_loop.h"
#include "route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ROOMS 4
#define VALUE_SIZE 128
#define UNTOUCHED "untouched"
#define THERMOSTAT "SetExternalThermostat1State"
#define HEAT_COOL "SetHeatCoolMode"

typedef struct {
    ctl_loop_record record;
    ctl_rooms rooms;
    ctl_rooms_config rooms_config;
    ctl_telemetry_source source;
} fixture;

typedef struct {
    const char *subtopic;
    const char *value;
} entry_row;

static void set_command(ctl_command *command, const char *name, const char *value)
{
    strcpy(command->name, name);
    strcpy(command->value, value);
}

static void two_commands(ctl_commands *commands)
{
    commands->count = 2;
    set_command(&commands->command[0], THERMOSTAT, "2");
    set_command(&commands->command[1], HEAT_COOL, "1");
}

/* Two rooms fresh at the record's instant, one stale and one that never received a value. */
static void set_rooms(fixture *f)
{
    static const double weight[ROOMS] = {1.0, 2.0, 0.5, 1.5};
    size_t i;
    ctl_rooms_config_defaults(&f->rooms_config, ROOMS);
    f->rooms_config.room_timeout_s = 300.0;
    for (i = 0; i < ROOMS; i++) {
        f->rooms_config.weight[i] = weight[i];
    }
    ctl_rooms_init(&f->rooms);
    f->rooms.has_value[0] = true;
    f->rooms.value_c[0] = 21.456;
    f->rooms.t[0] = 500.0;
    f->rooms.has_value[1] = true;
    f->rooms.value_c[1] = 22.0;
    f->rooms.t[1] = 550.0;
    f->rooms.has_value[2] = true;
    f->rooms.value_c[2] = 19.5;
    f->rooms.t[2] = 100.0;
}

static void set_record(ctl_loop_record *record)
{
    memset(record, 0, sizeof *record);
    record->t = 600.0;
    record->decision.t = 600.0;
    record->decision.demand = CTL_DEMAND_HEATING;
    record->decision.reason = CTL_REASON_NORMAL;
    record->decision.phase = CTL_PHASE_RUNNING;
    record->decision.energy_wh = -1523.44;
    record->decision.hysteresis_wh = 1500.0;
    record->decision.dt_s = 10.0;
    record->decision.integrated_s = 590.0;
    record->decision.skipped_s = 10.0;
    record->decision.has_inputs = true;
    record->decision.inputs.t_indoor_c = 22.456;
    record->decision.inputs.t_outdoor_c = 5.0;
    record->decision.inputs.p_produced_w = 3200.0;
    record->decision.inputs.p_internal_w = 550.0;
    record->decision.balance.p_net_w = -2735.4;
    record->decision.balance.p_integral_w = 12.34;
    record->decision.ledger.integral_wh = -45.67;
    record->decision.clamped = true;
    record->decision.unhealthy = 0u;
    record->decision.substituted =
        (uint8_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_P_SOLAR) | CTL_SIGNAL_BIT(CTL_SIGNAL_P_INTERNAL));
    record->rooms.has_average = true;
    record->rooms.average_c = 21.8;
    record->rooms.fresh_count = 2;
    record->rooms.fresh_weight = 3.0;
    record->rooms.total_weight = 5.0;
    two_commands(&record->commands);
    record->router_status = CTL_ROUTER_WAITING;
    record->sent_state = 2;
}

static void set_fixture(fixture *f)
{
    set_rooms(f);
    set_record(&f->record);
    f->source.record = &f->record;
    f->source.rooms = &f->rooms;
    f->source.rooms_config = &f->rooms_config;
    f->source.uptime_s = 612.7;
    f->source.enabled = true;
    f->source.gate = CTL_GATE_OPEN;
}

static void check_entry(const char *file, int line, const ctl_telemetry_source *source,
                        ctl_group group, size_t index, const entry_row *expected)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];
    strcpy(subtopic, UNTOUCHED);
    strcpy(value, UNTOUCHED);
    check_true(file, line,
               ctl_telemetry_entry(source, group, index, subtopic, sizeof subtopic, value,
                                   sizeof value),
               expected->subtopic);
    check_text(file, line, subtopic, expected->subtopic);
    check_text(file, line, value, expected->value);
}

static void check_group(const char *file, int line, const ctl_telemetry_source *source,
                        ctl_group group, const entry_row *rows, size_t count)
{
    size_t i;
    check_uint(file, line, (uint32_t)ctl_telemetry_count(source, group), (uint32_t)count);
    for (i = 0; i < count; i++) {
        check_entry(file, line, source, group, i, &rows[i]);
    }
}

#define CHECK_GROUP(source, group, rows) \
    check_group(__FILE__, __LINE__, (source), (group), (rows), sizeof(rows) / sizeof((rows)[0]))

/* Returns the value of the group's entry with the given subtopic, or "missing". */
static const char *value_of(const ctl_telemetry_source *source, ctl_group group,
                            const char *wanted, char *value, size_t size)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    size_t i;
    for (i = 0; i < ctl_telemetry_count(source, group); i++) {
        if (ctl_telemetry_entry(source, group, i, subtopic, sizeof subtopic, value, size) &&
            strcmp(subtopic, wanted) == 0) {
            return value;
        }
    }
    return "missing";
}

#define CHECK_VALUE(source, group, subtopic, expected)                                      \
    do {                                                                                     \
        char value_[VALUE_SIZE];                                                             \
        CHECK_TEXT(value_of((source), (group), (subtopic), value_, sizeof value_), (expected)); \
    } while (0)

static void test_counts(void)
{
    fixture f;
    set_fixture(&f);
    CHECK_UINT((uint32_t)ctl_telemetry_count(&f.source, CTL_GROUP_INPUTS), 20u);
    CHECK_UINT((uint32_t)ctl_telemetry_count(&f.source, CTL_GROUP_STATUS), 12u);
    CHECK_UINT((uint32_t)ctl_telemetry_count(&f.source, CTL_GROUP_OUTPUTS), 4u);
}

static void test_entries(void)
{
    static const entry_row inputs[] = {
        {"inputs/indoor_c", "22.46"},
        {"inputs/outdoor_c", "5.00"},
        {"inputs/produced_w", "3200"},
        {"inputs/internal_w", "550"},
        {"inputs/substituted", "P_SOLAR,P_INTERNAL"},
        {"inputs/unhealthy", "-"},
        {"inputs/rooms_fresh", "2"},
        {"inputs/rooms_fresh_weight", "3.0"},
        {"inputs/room/0/value_c", "21.46"},
        {"inputs/room/0/fresh", "1"},
        {"inputs/room/0/weight", "1.0"},
        {"inputs/room/1/value_c", "22.00"},
        {"inputs/room/1/fresh", "1"},
        {"inputs/room/1/weight", "2.0"},
        {"inputs/room/2/value_c", "19.50"},
        {"inputs/room/2/fresh", "0"},
        {"inputs/room/2/weight", "0.5"},
        {"inputs/room/3/value_c", "-"},
        {"inputs/room/3/fresh", "0"},
        {"inputs/room/3/weight", "1.5"},
    };
    static const entry_row status[] = {
        {"status/demand", "HEATING"},
        {"status/reason", "NORMAL"},
        {"status/phase", "RUNNING"},
        {"status/energy_wh", "-1523.4"},
        {"status/p_net_w", "-2735"},
        {"status/clamped", "1"},
        {"status/integrated_s", "590"},
        {"status/skipped_s", "10"},
        {"status/uptime_s", "613"},
        {"status/enabled", "1"},
        {"status/p_integral_w", "12.3"},
        {"status/integral_wh", "-45.7"},
    };
    static const entry_row outputs[] = {
        {"outputs/router", "WAITING"},
        {"outputs/thermostat_state", "2"},
        {"outputs/commands", THERMOSTAT ":2," HEAT_COOL ":1"},
        {"outputs/blocked", "-"},
    };
    fixture f;
    set_fixture(&f);
    CHECK_GROUP(&f.source, CTL_GROUP_INPUTS, inputs);
    CHECK_GROUP(&f.source, CTL_GROUP_STATUS, status);
    CHECK_GROUP(&f.source, CTL_GROUP_OUTPUTS, outputs);
}

static void test_unhealthy_and_flags(void)
{
    fixture f;
    set_fixture(&f);
    f.record.decision.unhealthy =
        (uint8_t)(CTL_SIGNAL_BIT(CTL_SIGNAL_T_INDOOR) | CTL_SIGNAL_BIT(CTL_SIGNAL_P_PRODUCED));
    f.record.decision.substituted = 0u;
    f.record.decision.clamped = false;
    f.record.router_status = CTL_ROUTER_FAULT;
    f.source.enabled = false;
    CHECK_VALUE(&f.source, CTL_GROUP_INPUTS, "inputs/unhealthy", "T_INDOOR,P_PRODUCED");
    CHECK_VALUE(&f.source, CTL_GROUP_INPUTS, "inputs/substituted", "-");
    CHECK_VALUE(&f.source, CTL_GROUP_STATUS, "status/clamped", "0");
    CHECK_VALUE(&f.source, CTL_GROUP_STATUS, "status/enabled", "0");
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/router", "FAULT");
}

static void test_blocked(void)
{
    fixture f;
    set_fixture(&f);
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/blocked", "-");
    f.source.gate = CTL_GATE_DISABLED;
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/blocked", "disabled");
}

static void test_without_inputs(void)
{
    static const char *const dashed[] = {
        "inputs/indoor_c", "inputs/outdoor_c", "inputs/produced_w", "inputs/internal_w",
    };
    fixture f;
    size_t i;
    set_fixture(&f);
    f.record.decision.has_inputs = false;
    for (i = 0; i < sizeof dashed / sizeof dashed[0]; i++) {
        CHECK_VALUE(&f.source, CTL_GROUP_INPUTS, dashed[i], "-");
    }
    CHECK_VALUE(&f.source, CTL_GROUP_STATUS, "status/p_net_w", "-");
    CHECK_VALUE(&f.source, CTL_GROUP_STATUS, "status/p_integral_w", "-");
    CHECK_VALUE(&f.source, CTL_GROUP_STATUS, "status/integral_wh", "-45.7");
    CHECK_VALUE(&f.source, CTL_GROUP_STATUS, "status/energy_wh", "-1523.4");
}

static void test_thermostat_state_and_commands(void)
{
    fixture f;
    set_fixture(&f);
    f.record.sent_state = -1;
    f.record.commands.count = 0;
    f.record.router_status = CTL_ROUTER_IDLE;
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/thermostat_state", "-");
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/commands", "-");
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/router", "IDLE");

    f.record.sent_state = 2;
    two_commands(&f.record.commands);
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/thermostat_state", "2");
    CHECK_VALUE(&f.source, CTL_GROUP_OUTPUTS, "outputs/commands",
                "SetExternalThermostat1State:2,SetHeatCoolMode:1");
}

static void test_settings(void)
{
    ctl_settings settings;
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t last;
    ctl_settings_defaults(&settings);
    last = ctl_settings_name_count(&settings) - 1;

    CHECK_TRUE(ctl_telemetry_setting(&settings, 0, subtopic, sizeof subtopic, value, sizeof value));
    CHECK_TEXT(subtopic, "config/enabled");
    CHECK_TEXT(value, "0");

    CHECK_TRUE(
        ctl_telemetry_setting(&settings, last, subtopic, sizeof subtopic, value, sizeof value));
    CHECK_TEXT(subtopic, "config/smart_grid");
    CHECK_TEXT(value, "0");

    strcpy(subtopic, UNTOUCHED);
    strcpy(value, UNTOUCHED);
    CHECK_TRUE(!ctl_telemetry_setting(&settings, last + 1, subtopic, sizeof subtopic, value,
                                      sizeof value));
    CHECK_TEXT(subtopic, UNTOUCHED);
    CHECK_TEXT(value, UNTOUCHED);
}

static void test_settings_list_rooms(void)
{
    ctl_settings settings;
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    ctl_settings_defaults(&settings);
    CHECK_UINT((uint32_t)ctl_settings_set(&settings, "rooms", "1"), (uint32_t)CTL_SETTING_OK);
    CHECK_UINT((uint32_t)ctl_settings_set(&settings, "room.0.topic", "home/living/temp"),
               (uint32_t)CTL_SETTING_OK);
    CHECK_TRUE(ctl_telemetry_setting(&settings, 3, subtopic, sizeof subtopic, value, sizeof value));
    CHECK_TEXT(subtopic, "config/room.0.topic");
    CHECK_TEXT(value, "home/living/temp");
}

/* Calls the entry with the given sizes and checks that it refused and wrote nothing. */
static void check_entry_refused(const char *file, int line, const ctl_telemetry_source *source,
                                ctl_group group, size_t index, size_t subtopic_size,
                                size_t value_size)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];
    strcpy(subtopic, UNTOUCHED);
    strcpy(value, UNTOUCHED);
    check_true(file, line,
               !ctl_telemetry_entry(source, group, index, subtopic, subtopic_size, value,
                                    value_size),
               "refused");
    check_text(file, line, subtopic, UNTOUCHED);
    check_text(file, line, value, UNTOUCHED);
}

static void check_setting_refused(const char *file, int line, const ctl_settings *settings,
                                  size_t index, size_t subtopic_size, size_t value_size)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    strcpy(subtopic, UNTOUCHED);
    strcpy(value, UNTOUCHED);
    check_true(file, line,
               !ctl_telemetry_setting(settings, index, subtopic, subtopic_size, value, value_size),
               "refused");
    check_text(file, line, subtopic, UNTOUCHED);
    check_text(file, line, value, UNTOUCHED);
}

static void test_refusals(void)
{
    /* "outputs/commands" and its value: both lengths and their terminating zeros. */
    const size_t subtopic_fits = sizeof "outputs/commands";
    const size_t value_fits = sizeof THERMOSTAT ":2," HEAT_COOL ":1";
    const size_t setting_subtopic_fits = sizeof "config/enabled";
    fixture f;
    ctl_settings settings;
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];

    set_fixture(&f);
    CHECK_TRUE(ctl_telemetry_entry(&f.source, CTL_GROUP_OUTPUTS, 2, subtopic, subtopic_fits, value,
                                   value_fits));
    check_entry_refused(__FILE__, __LINE__, &f.source, CTL_GROUP_OUTPUTS, 2, subtopic_fits - 1,
                        value_fits);
    check_entry_refused(__FILE__, __LINE__, &f.source, CTL_GROUP_OUTPUTS, 2, subtopic_fits,
                        value_fits - 1);
    check_entry_refused(__FILE__, __LINE__, &f.source, CTL_GROUP_OUTPUTS, 4,
                        CTL_TELEMETRY_SUBTOPIC_MAX, VALUE_SIZE);
    check_entry_refused(__FILE__, __LINE__, &f.source, CTL_GROUP_INPUTS, 22,
                        CTL_TELEMETRY_SUBTOPIC_MAX, VALUE_SIZE);
    check_entry_refused(__FILE__, __LINE__, &f.source, CTL_GROUP_STATUS, 12,
                        CTL_TELEMETRY_SUBTOPIC_MAX, VALUE_SIZE);
    check_entry_refused(__FILE__, __LINE__, &f.source, (ctl_group)3, 0,
                        CTL_TELEMETRY_SUBTOPIC_MAX, VALUE_SIZE);

    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_telemetry_setting(&settings, 0, subtopic, setting_subtopic_fits, value,
                                     sizeof "0"));
    check_setting_refused(__FILE__, __LINE__, &settings, 0, setting_subtopic_fits - 1,
                          sizeof "0");
    check_setting_refused(__FILE__, __LINE__, &settings, 0, setting_subtopic_fits,
                          sizeof "0" - 1);
}

/* A number the fixed format refuses is refused whole rather than written cut or empty. */
static void test_unwritable_number(void)
{
    fixture f;
    set_fixture(&f);
    f.source.uptime_s = 1e12;
    check_entry_refused(__FILE__, __LINE__, &f.source, CTL_GROUP_STATUS, 8,
                        CTL_TELEMETRY_SUBTOPIC_MAX, VALUE_SIZE);
}

/* The longest subtopic, a room's at the highest index, fits the constant. */
static void test_longest_subtopic_fits(void)
{
    fixture f;
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[VALUE_SIZE];
    set_fixture(&f);
    f.rooms_config.count = CTL_ROOMS_MAX;
    CHECK_UINT((uint32_t)ctl_telemetry_count(&f.source, CTL_GROUP_INPUTS),
               (uint32_t)(8 + 3 * CTL_ROOMS_MAX));
    CHECK_TRUE(ctl_telemetry_entry(&f.source, CTL_GROUP_INPUTS, 8 + 3 * CTL_ROOMS_MAX - 3,
                                   subtopic, sizeof subtopic, value, sizeof value));
    CHECK_TEXT(subtopic, "inputs/room/15/value_c");
}

/* A configured count above the maximum is capped, so that no room past the arrays is listed. */
static void test_room_count_is_capped(void)
{
    fixture f;
    set_fixture(&f);
    f.rooms_config.count = CTL_ROOMS_MAX + 1;
    CHECK_UINT((uint32_t)ctl_telemetry_count(&f.source, CTL_GROUP_INPUTS),
               (uint32_t)(8 + 3 * CTL_ROOMS_MAX));
}

static void test_no_group_has_no_entries(void)
{
    fixture f;
    set_fixture(&f);
    CHECK_UINT((uint32_t)ctl_telemetry_count(&f.source, (ctl_group)3), 0u);
}

int main(void)
{
    test_counts();
    test_entries();
    test_unhealthy_and_flags();
    test_blocked();
    test_without_inputs();
    test_thermostat_state_and_commands();
    test_settings();
    test_settings_list_rooms();
    test_refusals();
    test_unwritable_number();
    test_longest_subtopic_fits();
    test_room_count_is_capped();
    test_no_group_has_no_entries();
    return check_result();
}
