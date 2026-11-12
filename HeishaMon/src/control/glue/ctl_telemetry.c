#include "ctl_telemetry.h"

#include "../adapt/ctl_rooms.h"
#include "../core/ctl_controller.h"
#include "../core/ctl_signals.h"
#include "../core/ctl_types.h"
#include "../loop/ctl_loop.h"
#include "../loop/ctl_text.h"
#include "../route/ctl_router.h"
#include "ctl_gate.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define INPUTS_FIXED 9
#define ENTRIES_PER_ROOM 3
#define STATUS_COUNT 10
#define OUTPUTS_COUNT 4
/* Holds the longest value an entry writes: two commands at their fields' full width, or all five
 * signal names, with room to spare. */
#define VALUE_MAX 128
#define NOTHING "-"

static void put_count(ctl_text *writer, size_t count)
{
    ctl_text_put_number(writer, (double)count, 0);
}

static void put_flag(ctl_text *writer, bool flag)
{
    ctl_text_put(writer, flag ? "1" : "0");
}

static void put_signals(ctl_text *writer, uint8_t bits)
{
    int signal;
    bool first = true;
    for (signal = 0; signal < CTL_SIGNAL_COUNT; signal++) {
        if ((bits & CTL_SIGNAL_BIT(signal)) == 0u) {
            continue;
        }
        if (!first) {
            ctl_text_put(writer, ",");
        }
        ctl_text_put(writer, ctl_signal_name((ctl_signal)signal));
        first = false;
    }
    if (first) {
        ctl_text_put(writer, NOTHING);
    }
}

static size_t room_count(const ctl_rooms_config *config)
{
    return config->count < CTL_ROOMS_MAX ? config->count : CTL_ROOMS_MAX;
}

static void put_fixed_input(ctl_text *subtopic, ctl_text *value,
                            const ctl_loop_record *record, size_t index)
{
    const ctl_decision *decision = &record->decision;
    const ctl_inputs *inputs = &decision->inputs;
    const bool has_inputs = decision->has_inputs;
    switch (index) {
    case 0:
        ctl_text_put(subtopic, "inputs/indoor_c");
        ctl_text_put_input(value, has_inputs, inputs->t_indoor_c, 2);
        break;
    case 1:
        ctl_text_put(subtopic, "inputs/outdoor_c");
        ctl_text_put_input(value, has_inputs, inputs->t_outdoor_c, 2);
        break;
    case 2:
        ctl_text_put(subtopic, "inputs/produced_w");
        ctl_text_put_input(value, has_inputs, inputs->p_produced_w, 0);
        break;
    case 3:
        ctl_text_put(subtopic, "inputs/solar_w");
        ctl_text_put_input(value, has_inputs, inputs->p_solar_w, 0);
        break;
    case 4:
        ctl_text_put(subtopic, "inputs/internal_w");
        ctl_text_put_input(value, has_inputs, inputs->p_internal_w, 0);
        break;
    case 5:
        ctl_text_put(subtopic, "inputs/substituted");
        put_signals(value, decision->substituted);
        break;
    case 6:
        ctl_text_put(subtopic, "inputs/unhealthy");
        put_signals(value, decision->unhealthy);
        break;
    case 7:
        ctl_text_put(subtopic, "inputs/rooms_fresh");
        put_count(value, record->rooms.fresh_count);
        break;
    default:
        ctl_text_put(subtopic, "inputs/rooms_fresh_weight");
        ctl_text_put_number(value, record->rooms.fresh_weight, 1);
        break;
    }
}

static void put_room_input(ctl_text *subtopic, ctl_text *value,
                           const ctl_telemetry_source *source, size_t index)
{
    const size_t room = index / ENTRIES_PER_ROOM;
    const ctl_rooms *rooms = source->rooms;
    ctl_text_put(subtopic, "inputs/room/");
    put_count(subtopic, room);
    switch (index % ENTRIES_PER_ROOM) {
    case 0:
        ctl_text_put(subtopic, "/value_c");
        ctl_text_put_input(value, rooms->has_value[room], rooms->value_c[room], 2);
        break;
    case 1:
        ctl_text_put(subtopic, "/fresh");
        put_flag(value, ctl_rooms_is_fresh(rooms, source->rooms_config, room,
                                           source->record->t));
        break;
    default:
        ctl_text_put(subtopic, "/weight");
        ctl_text_put_number(value, source->rooms_config->weight[room], 1);
        break;
    }
}

static void put_input_entry(ctl_text *subtopic, ctl_text *value,
                            const ctl_telemetry_source *source, size_t index)
{
    if (index < INPUTS_FIXED) {
        put_fixed_input(subtopic, value, source->record, index);
    } else {
        put_room_input(subtopic, value, source, index - INPUTS_FIXED);
    }
}

static void put_status_entry(ctl_text *subtopic, ctl_text *value,
                             const ctl_telemetry_source *source, size_t index)
{
    const ctl_decision *decision = &source->record->decision;
    switch (index) {
    case 0:
        ctl_text_put(subtopic, "status/demand");
        ctl_text_put(value, ctl_demand_name(decision->demand));
        break;
    case 1:
        ctl_text_put(subtopic, "status/reason");
        ctl_text_put(value, ctl_reason_name(decision->reason));
        break;
    case 2:
        ctl_text_put(subtopic, "status/phase");
        ctl_text_put(value, ctl_phase_name(decision->phase));
        break;
    case 3:
        ctl_text_put(subtopic, "status/energy_wh");
        ctl_text_put_number(value, decision->energy_wh, 1);
        break;
    case 4:
        ctl_text_put(subtopic, "status/p_net_w");
        ctl_text_put_input(value, decision->has_inputs, decision->balance.p_net_w, 0);
        break;
    case 5:
        ctl_text_put(subtopic, "status/clamped");
        put_flag(value, decision->clamped);
        break;
    case 6:
        ctl_text_put(subtopic, "status/integrated_s");
        ctl_text_put_number(value, decision->integrated_s, 0);
        break;
    case 7:
        ctl_text_put(subtopic, "status/skipped_s");
        ctl_text_put_number(value, decision->skipped_s, 0);
        break;
    case 8:
        ctl_text_put(subtopic, "status/uptime_s");
        ctl_text_put_number(value, source->uptime_s, 0);
        break;
    default:
        ctl_text_put(subtopic, "status/enabled");
        put_flag(value, source->enabled);
        break;
    }
}

static void put_output_entry(ctl_text *subtopic, ctl_text *value,
                             const ctl_telemetry_source *source, size_t index)
{
    const ctl_loop_record *record = source->record;
    switch (index) {
    case 0:
        ctl_text_put(subtopic, "outputs/router");
        ctl_text_put(value, ctl_router_status_name(record->router_status));
        break;
    case 1:
        ctl_text_put(subtopic, "outputs/thermostat_state");
        if (record->sent_state < 0) {
            ctl_text_put(value, NOTHING);
        } else {
            put_count(value, (size_t)record->sent_state);
        }
        break;
    case 2:
        ctl_text_put(subtopic, "outputs/commands");
        ctl_text_put_commands(value, &record->commands);
        break;
    default:
        ctl_text_put(subtopic, "outputs/blocked");
        ctl_text_put(value, ctl_gate_name(source->gate));
        break;
    }
}

/* Both texts are written into the module's own buffers first and copied out only when both fit
 * the caller's, so that a refusal writes nothing. */
static bool deliver(const char *subtopic_text, const char *value_text, char *subtopic,
                    size_t subtopic_size, char *value, size_t value_size)
{
    const size_t subtopic_length = strlen(subtopic_text);
    const size_t value_length = strlen(value_text);
    if (subtopic_length >= subtopic_size || value_length >= value_size) {
        return false;
    }
    memcpy(subtopic, subtopic_text, subtopic_length + 1);
    memcpy(value, value_text, value_length + 1);
    return true;
}

size_t ctl_telemetry_count(const ctl_telemetry_source *source, ctl_group group)
{
    switch (group) {
    case CTL_GROUP_INPUTS:
        return INPUTS_FIXED + ENTRIES_PER_ROOM * room_count(source->rooms_config);
    case CTL_GROUP_STATUS:
        return STATUS_COUNT;
    case CTL_GROUP_OUTPUTS:
        return OUTPUTS_COUNT;
    default:
        return 0;
    }
}

bool ctl_telemetry_entry(const ctl_telemetry_source *source, ctl_group group, size_t index,
                         char *subtopic, size_t subtopic_size, char *value, size_t value_size)
{
    char subtopic_text[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value_text[VALUE_MAX];
    ctl_text subtopic_writer;
    ctl_text value_writer;

    if (index >= ctl_telemetry_count(source, group)) {
        return false;
    }
    ctl_text_start(&subtopic_writer, subtopic_text, sizeof subtopic_text);
    ctl_text_start(&value_writer, value_text, sizeof value_text);
    switch (group) {
    case CTL_GROUP_INPUTS:
        put_input_entry(&subtopic_writer, &value_writer, source, index);
        break;
    case CTL_GROUP_STATUS:
        put_status_entry(&subtopic_writer, &value_writer, source, index);
        break;
    default:
        put_output_entry(&subtopic_writer, &value_writer, source, index);
        break;
    }
    if (!subtopic_writer.fits || !value_writer.fits) {
        return false;
    }
    return deliver(subtopic_text, value_text, subtopic, subtopic_size, value, value_size);
}

bool ctl_telemetry_setting(const ctl_settings *settings, size_t index,
                           char *subtopic, size_t subtopic_size, char *value, size_t value_size)
{
    char name[CTL_SETTINGS_NAME_MAX];
    char subtopic_text[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value_text[CTL_SETTINGS_VALUE_MAX];
    ctl_text subtopic_writer;

    if (!ctl_settings_name_at(settings, index, name, sizeof name) ||
        !ctl_settings_get(settings, name, value_text, sizeof value_text)) {
        return false;
    }
    ctl_text_start(&subtopic_writer, subtopic_text, sizeof subtopic_text);
    ctl_text_put(&subtopic_writer, "config/");
    ctl_text_put(&subtopic_writer, name);
    if (!subtopic_writer.fits) {
        return false;
    }
    return deliver(subtopic_text, value_text, subtopic, subtopic_size, value, value_size);
}
