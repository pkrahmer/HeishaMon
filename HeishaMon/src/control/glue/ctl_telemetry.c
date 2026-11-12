#include "ctl_telemetry.h"

#include "../adapt/ctl_rooms.h"
#include "../core/ctl_controller.h"
#include "../core/ctl_signals.h"
#include "../core/ctl_types.h"
#include "../loop/ctl_format.h"
#include "../loop/ctl_loop.h"
#include "../route/ctl_router.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define INPUTS_FIXED 9
#define ENTRIES_PER_ROOM 3
#define STATUS_COUNT 10
#define OUTPUTS_COUNT 3
/* Holds the longest value an entry writes: two commands at their fields' full width, or all five
 * signal names, with room to spare. */
#define VALUE_MAX 128
#define NOTHING "-"

/* Every text is written into a buffer of the module's own first, so that a text that does not fit
 * the caller's buffer leaves it untouched; once a piece does not fit, the writer stops. */
typedef struct {
    char *text;
    size_t size;
    size_t length;
    bool fits;
} text_writer;

static void writer_start(text_writer *writer, char *text, size_t size)
{
    writer->text = text;
    writer->size = size;
    writer->length = 0;
    writer->fits = true;
    text[0] = '\0';
}

/* Stops at limit as well, because a command's fields are arrays that may lack a terminator. */
static void put_bounded(text_writer *writer, const char *piece, size_t limit)
{
    size_t i;
    for (i = 0; writer->fits && i < limit && piece[i] != '\0'; i++) {
        if (writer->length + 1 >= writer->size) {
            writer->fits = false;
            return;
        }
        writer->text[writer->length++] = piece[i];
        writer->text[writer->length] = '\0';
    }
}

static void put_text(text_writer *writer, const char *piece)
{
    put_bounded(writer, piece, (size_t)-1);
}

static void put_number(text_writer *writer, double value, int decimals)
{
    char number[CTL_FORMAT_TEXT_MAX];
    if (!ctl_format_fixed(value, decimals, number, sizeof number)) {
        writer->fits = false;
        return;
    }
    put_text(writer, number);
}

static void put_count(text_writer *writer, size_t count)
{
    put_number(writer, (double)count, 0);
}

static void put_flag(text_writer *writer, bool flag)
{
    put_text(writer, flag ? "1" : "0");
}

/* Without inputs the core resolved nothing, and its zeros there are no measurement. */
static void put_input(text_writer *writer, bool has_inputs, double value, int decimals)
{
    if (has_inputs) {
        put_number(writer, value, decimals);
    } else {
        put_text(writer, NOTHING);
    }
}

static void put_signals(text_writer *writer, uint8_t bits)
{
    int signal;
    bool first = true;
    for (signal = 0; signal < CTL_SIGNAL_COUNT; signal++) {
        if ((bits & CTL_SIGNAL_BIT(signal)) == 0u) {
            continue;
        }
        if (!first) {
            put_text(writer, ",");
        }
        put_text(writer, ctl_signal_name((ctl_signal)signal));
        first = false;
    }
    if (first) {
        put_text(writer, NOTHING);
    }
}

static void put_commands(text_writer *writer, const ctl_commands *commands)
{
    size_t i;
    if (commands->count == 0) {
        put_text(writer, NOTHING);
        return;
    }
    for (i = 0; i < commands->count && i < CTL_ROUTER_COMMANDS_MAX; i++) {
        if (i > 0) {
            put_text(writer, ",");
        }
        put_bounded(writer, commands->command[i].name, CTL_ROUTER_NAME_MAX);
        put_text(writer, ":");
        put_bounded(writer, commands->command[i].value, CTL_ROUTER_VALUE_MAX);
    }
}

static const char *router_status_name(ctl_router_status status)
{
    switch (status) {
    case CTL_ROUTER_IDLE:
        return "IDLE";
    case CTL_ROUTER_WAITING:
        return "WAITING";
    case CTL_ROUTER_FAULT:
        return "FAULT";
    default:
        return "INVALID";
    }
}

static size_t room_count(const ctl_rooms_config *config)
{
    return config->count < CTL_ROOMS_MAX ? config->count : CTL_ROOMS_MAX;
}

static void put_fixed_input(text_writer *subtopic, text_writer *value,
                            const ctl_loop_record *record, size_t index)
{
    const ctl_decision *decision = &record->decision;
    const ctl_inputs *inputs = &decision->inputs;
    const bool has_inputs = decision->has_inputs;
    switch (index) {
    case 0:
        put_text(subtopic, "inputs/indoor_c");
        put_input(value, has_inputs, inputs->t_indoor_c, 2);
        break;
    case 1:
        put_text(subtopic, "inputs/outdoor_c");
        put_input(value, has_inputs, inputs->t_outdoor_c, 2);
        break;
    case 2:
        put_text(subtopic, "inputs/produced_w");
        put_input(value, has_inputs, inputs->p_produced_w, 0);
        break;
    case 3:
        put_text(subtopic, "inputs/solar_w");
        put_input(value, has_inputs, inputs->p_solar_w, 0);
        break;
    case 4:
        put_text(subtopic, "inputs/internal_w");
        put_input(value, has_inputs, inputs->p_internal_w, 0);
        break;
    case 5:
        put_text(subtopic, "inputs/substituted");
        put_signals(value, decision->substituted);
        break;
    case 6:
        put_text(subtopic, "inputs/unhealthy");
        put_signals(value, decision->unhealthy);
        break;
    case 7:
        put_text(subtopic, "inputs/rooms_fresh");
        put_count(value, record->rooms.fresh_count);
        break;
    default:
        put_text(subtopic, "inputs/rooms_fresh_weight");
        put_number(value, record->rooms.fresh_weight, 1);
        break;
    }
}

static void put_room_input(text_writer *subtopic, text_writer *value,
                           const ctl_telemetry_source *source, size_t index)
{
    const size_t room = index / ENTRIES_PER_ROOM;
    const ctl_rooms *rooms = source->rooms;
    put_text(subtopic, "inputs/room/");
    put_count(subtopic, room);
    switch (index % ENTRIES_PER_ROOM) {
    case 0:
        put_text(subtopic, "/value_c");
        put_input(value, rooms->has_value[room], rooms->value_c[room], 2);
        break;
    case 1:
        put_text(subtopic, "/fresh");
        put_flag(value, ctl_rooms_is_fresh(rooms, source->rooms_config, room,
                                           source->record->t));
        break;
    default:
        put_text(subtopic, "/weight");
        put_number(value, source->rooms_config->weight[room], 1);
        break;
    }
}

static void put_input_entry(text_writer *subtopic, text_writer *value,
                            const ctl_telemetry_source *source, size_t index)
{
    if (index < INPUTS_FIXED) {
        put_fixed_input(subtopic, value, source->record, index);
    } else {
        put_room_input(subtopic, value, source, index - INPUTS_FIXED);
    }
}

static void put_status_entry(text_writer *subtopic, text_writer *value,
                             const ctl_telemetry_source *source, size_t index)
{
    const ctl_decision *decision = &source->record->decision;
    switch (index) {
    case 0:
        put_text(subtopic, "status/demand");
        put_text(value, ctl_demand_name(decision->demand));
        break;
    case 1:
        put_text(subtopic, "status/reason");
        put_text(value, ctl_reason_name(decision->reason));
        break;
    case 2:
        put_text(subtopic, "status/phase");
        put_text(value, ctl_phase_name(decision->phase));
        break;
    case 3:
        put_text(subtopic, "status/energy_wh");
        put_number(value, decision->energy_wh, 1);
        break;
    case 4:
        put_text(subtopic, "status/p_net_w");
        put_input(value, decision->has_inputs, decision->balance.p_net_w, 0);
        break;
    case 5:
        put_text(subtopic, "status/clamped");
        put_flag(value, decision->clamped);
        break;
    case 6:
        put_text(subtopic, "status/integrated_s");
        put_number(value, decision->integrated_s, 0);
        break;
    case 7:
        put_text(subtopic, "status/skipped_s");
        put_number(value, decision->skipped_s, 0);
        break;
    case 8:
        put_text(subtopic, "status/uptime_s");
        put_number(value, source->uptime_s, 0);
        break;
    default:
        put_text(subtopic, "status/enabled");
        put_flag(value, source->enabled);
        break;
    }
}

static void put_output_entry(text_writer *subtopic, text_writer *value,
                             const ctl_loop_record *record, size_t index)
{
    switch (index) {
    case 0:
        put_text(subtopic, "outputs/router");
        put_text(value, router_status_name(record->router_status));
        break;
    case 1:
        put_text(subtopic, "outputs/thermostat_state");
        if (record->sent_state < 0) {
            put_text(value, NOTHING);
        } else {
            put_count(value, (size_t)record->sent_state);
        }
        break;
    default:
        put_text(subtopic, "outputs/commands");
        put_commands(value, &record->commands);
        break;
    }
}

/* Copies both texts out only when both fit the caller's buffers, so that a refusal writes
 * nothing. */
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
    text_writer subtopic_writer;
    text_writer value_writer;

    if (index >= ctl_telemetry_count(source, group)) {
        return false;
    }
    writer_start(&subtopic_writer, subtopic_text, sizeof subtopic_text);
    writer_start(&value_writer, value_text, sizeof value_text);
    switch (group) {
    case CTL_GROUP_INPUTS:
        put_input_entry(&subtopic_writer, &value_writer, source, index);
        break;
    case CTL_GROUP_STATUS:
        put_status_entry(&subtopic_writer, &value_writer, source, index);
        break;
    default:
        put_output_entry(&subtopic_writer, &value_writer, source->record, index);
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
    text_writer subtopic_writer;

    if (!ctl_settings_name_at(settings, index, name, sizeof name) ||
        !ctl_settings_get(settings, name, value_text, sizeof value_text)) {
        return false;
    }
    writer_start(&subtopic_writer, subtopic_text, sizeof subtopic_text);
    put_text(&subtopic_writer, "config/");
    put_text(&subtopic_writer, name);
    if (!subtopic_writer.fits) {
        return false;
    }
    return deliver(subtopic_text, value_text, subtopic, subtopic_size, value, value_size);
}
