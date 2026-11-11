#include "ctl_loop.h"

#include "../adapt/ctl_payload.h"
#include "../adapt/ctl_pump.h"
#include "../adapt/ctl_rooms.h"
#include "../core/ctl_config.h"
#include "../core/ctl_controller.h"
#include "../core/ctl_relay.h"
#include "../core/ctl_types.h"
#include "../route/ctl_router.h"
#include "ctl_format.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEFAULT_ALLOWED \
    ((uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE)))

void ctl_loop_config_defaults(ctl_loop_config *config, size_t room_count)
{
    ctl_config_defaults(&config->core);
    ctl_rooms_config_defaults(&config->rooms, room_count);
    ctl_router_config_defaults(&config->router);
    config->allowed = DEFAULT_ALLOWED;
    config->assume_landed = false;
}

bool ctl_loop_config_valid(const ctl_loop_config *config)
{
    return ctl_config_validate(&config->core) == 0u
        && ctl_rooms_config_valid(&config->rooms)
        && ctl_router_config_valid(&config->router)
        && (config->allowed & (uint8_t)~CTL_DEMAND_ALL) == 0u;
}

void ctl_loop_init(ctl_loop *loop, const ctl_loop_config *config, double now)
{
    ctl_state_init(&loop->state);
    ctl_rooms_init(&loop->rooms);
    ctl_pump_init(&loop->pump);
    ctl_router_init(&loop->router);
    ctl_set_allowed(&loop->state, config->allowed, now, &config->core);
    loop->confirmed_known = false;
    loop->confirmed_state = 0;
    loop->next_advance = now + config->core.integration_interval_s;
    loop->next_evaluate = now + config->core.tick_interval_s;
}

/* The pump adapter answers false both for a refused flag and for an accepted one that left the
 * mask as it was; only the second is news the caller may ignore, so the flag is read again here
 * by the adapter's own rule: a payload the parser takes, of exactly 0 or 1. */
static bool is_flag_payload(const char *payload, size_t length)
{
    ctl_payload parsed = {0.0, false, 0};
    if (!ctl_payload_parse(payload, length, &parsed)) {
        return false;
    }
    return parsed.value == 0.0 || parsed.value == 1.0;
}

static ctl_input_status offer_room(ctl_loop *loop, const ctl_loop_config *config, size_t room,
                                   const char *payload, size_t length, double now)
{
    if (room >= config->rooms.count) {
        return CTL_INPUT_BAD;
    }
    if (ctl_rooms_offer(&loop->rooms, &config->rooms, room, payload, length, now) !=
        CTL_ROOM_ACCEPTED) {
        return CTL_INPUT_REFUSED;
    }
    return CTL_INPUT_TAKEN;
}

static bool is_flag_input(ctl_input input)
{
    return input == CTL_INPUT_VALVE || input == CTL_INPUT_DEFROST;
}

/* Returns whether the adapter gave a reading; the caller tells a refusal from silence. */
static bool adapt(ctl_loop *loop, const ctl_loop_config *config, ctl_input input,
                  const char *payload, size_t length, double now, ctl_reading *reading)
{
    switch (input) {
    case CTL_INPUT_OUTDOOR:
        return ctl_pump_outdoor(payload, length, now, reading);
    case CTL_INPUT_HEAT:
        return ctl_pump_heat(&loop->pump, payload, length, now, reading);
    case CTL_INPUT_VALVE:
        return ctl_pump_valve(&loop->pump, payload, length, now, reading);
    case CTL_INPUT_DEFROST:
        return ctl_pump_defrost(&loop->pump, payload, length, now, reading);
    default:
        return ctl_solar(payload, length, config->core.solar_gain_factor, now, reading);
    }
}

static ctl_input_status offer_pump(ctl_loop *loop, const ctl_loop_config *config, ctl_input input,
                                   const char *payload, size_t length, double now)
{
    ctl_reading reading;
    if (adapt(loop, config, input, payload, length, now, &reading)) {
        ctl_observe(&loop->state, reading.signal, reading.value, reading.t, &config->core);
        return CTL_INPUT_TAKEN;
    }
    if (is_flag_input(input) && is_flag_payload(payload, length)) {
        return CTL_INPUT_NO_READING;
    }
    return CTL_INPUT_REFUSED;
}

ctl_input_status ctl_loop_input(ctl_loop *loop, const ctl_loop_config *config, ctl_input input,
                                size_t room, const char *payload, size_t length, double now)
{
    if (!isfinite(now)) {
        return CTL_INPUT_BAD;
    }
    switch (input) {
    case CTL_INPUT_ROOM:
        return offer_room(loop, config, room, payload, length, now);
    case CTL_INPUT_OUTDOOR:
    case CTL_INPUT_HEAT:
    case CTL_INPUT_VALVE:
    case CTL_INPUT_DEFROST:
    case CTL_INPUT_PV:
        return offer_pump(loop, config, input, payload, length, now);
    default:
        return CTL_INPUT_BAD;
    }
}

void ctl_loop_confirm(ctl_loop *loop, bool known, int state)
{
    loop->confirmed_known = known;
    loop->confirmed_state = state;
}

/* A shadow sends nothing to a pump, so nothing would ever confirm its commands; assuming that
 * each one landed keeps its router from repeating and faulting on commands nobody sent. */
static void step_router(ctl_loop *loop, const ctl_loop_config *config, ctl_demand demand,
                        double now, ctl_commands *commands)
{
    bool known = loop->confirmed_known;
    int state = loop->confirmed_state;
    if (config->assume_landed) {
        known = true;
        state = loop->router.sent_state;
    }
    ctl_router_step(&loop->router, &config->router, demand, known, state, now, commands);
}

/* The rooms reach the core only here, as one average, so that the core sees the house's indoor
 * temperature on the decision's own clock rather than one room at a time. */
static void decide(ctl_loop *loop, const ctl_loop_config *config, double now,
                   ctl_loop_record *record)
{
    ctl_rooms_average average;
    ctl_decision decision;
    ctl_commands commands;

    ctl_rooms_average_at(&loop->rooms, &config->rooms, now, &average);
    if (average.has_average) {
        ctl_observe(&loop->state, CTL_SIGNAL_T_INDOOR, average.average_c, now, &config->core);
    }
    ctl_evaluate(&loop->state, now, &config->core, &decision);
    step_router(loop, config, decision.demand, now, &commands);
    loop->next_evaluate = now + config->core.tick_interval_s;

    record->t = now;
    record->decision = decision;
    record->rooms = average;
    record->commands = commands;
    record->router_status = loop->router.status;
}

bool ctl_loop_tick(ctl_loop *loop, const ctl_loop_config *config, double now,
                   ctl_loop_record *record)
{
    if (!isfinite(now)) {
        return false;
    }
    if (now >= loop->next_advance) {
        double dt_s;
        (void)ctl_advance(&loop->state, now, &config->core, &dt_s);
        loop->next_advance = now + config->core.integration_interval_s;
    }
    if (now < loop->next_evaluate) {
        return false;
    }
    decide(loop, config, now, record);
    return true;
}

/* The text is written straight into the caller's buffer; once a piece does not fit, the writer
 * stops and the whole text is refused rather than cut. */
typedef struct {
    char *text;
    size_t size;
    size_t length;
    bool fits;
} text_writer;

static void put_text(text_writer *writer, const char *piece)
{
    size_t i;
    for (i = 0; writer->fits && piece[i] != '\0'; i++) {
        if (writer->length + 1 >= writer->size) {
            writer->fits = false;
            return;
        }
        writer->text[writer->length++] = piece[i];
    }
}

/* A command's fields are arrays of a known size, so a missing terminator stops at their end. */
static void put_bounded(text_writer *writer, const char *piece, size_t limit)
{
    char one[2] = {'\0', '\0'};
    size_t i;
    for (i = 0; i < limit && piece[i] != '\0'; i++) {
        one[0] = piece[i];
        put_text(writer, one);
    }
}

static void put_number(text_writer *writer, double value, int decimals)
{
    char number[CTL_LOOP_TEXT_MAX];
    if (!ctl_format_fixed(value, decimals, number, sizeof number)) {
        writer->fits = false;
        return;
    }
    put_text(writer, number);
}

static void put_field(text_writer *writer, const char *key)
{
    if (writer->length > 0) {
        put_text(writer, " ");
    }
    put_text(writer, key);
    put_text(writer, "=");
}

static void put_number_field(text_writer *writer, const char *key, double value, int decimals)
{
    put_field(writer, key);
    put_number(writer, value, decimals);
}

/* Without inputs the core resolved nothing, and its zeros there are no measurement. */
static void put_input_field(text_writer *writer, const char *key, bool has_inputs, double value,
                            int decimals)
{
    put_field(writer, key);
    if (has_inputs) {
        put_number(writer, value, decimals);
    } else {
        put_text(writer, "-");
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

static void put_rooms(text_writer *writer, const ctl_rooms_average *rooms)
{
    put_field(writer, "rooms");
    put_number(writer, (double)rooms->fresh_count, 0);
    put_text(writer, ":");
    put_number(writer, rooms->fresh_weight, 1);
    put_text(writer, "/");
    put_number(writer, rooms->total_weight, 1);
}

static void put_commands(text_writer *writer, const ctl_commands *commands)
{
    size_t i;
    put_field(writer, "cmd");
    if (commands->count == 0) {
        put_text(writer, "-");
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

bool ctl_loop_format_record(const ctl_loop_record *record, char *text, size_t size)
{
    const ctl_decision *decision = &record->decision;
    const bool has_inputs = decision->has_inputs;
    text_writer writer;

    writer.text = text;
    writer.size = size;
    writer.length = 0;
    writer.fits = size > 0;

    put_number_field(&writer, "t", record->t, 1);
    put_field(&writer, "demand");
    put_text(&writer, ctl_demand_name(decision->demand));
    put_field(&writer, "reason");
    put_text(&writer, ctl_reason_name(decision->reason));
    put_field(&writer, "phase");
    put_text(&writer, ctl_phase_name(decision->phase));
    put_number_field(&writer, "E", decision->energy_wh, 1);
    put_input_field(&writer, "pnet", has_inputs, decision->balance.p_net_w, 0);
    put_input_field(&writer, "indoor", has_inputs, decision->inputs.t_indoor_c, 2);
    put_input_field(&writer, "outdoor", has_inputs, decision->inputs.t_outdoor_c, 2);
    put_input_field(&writer, "produced", has_inputs, decision->inputs.p_produced_w, 0);
    put_input_field(&writer, "solar", has_inputs, decision->inputs.p_solar_w, 0);
    put_rooms(&writer, &record->rooms);
    put_field(&writer, "router");
    put_text(&writer, router_status_name(record->router_status));
    put_commands(&writer, &record->commands);

    if (!writer.fits) {
        if (size > 0) {
            text[0] = '\0';
        }
        return false;
    }
    text[writer.length] = '\0';
    return true;
}
