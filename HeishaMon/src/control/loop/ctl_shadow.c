#include "ctl_shadow.h"

#include "../adapt/ctl_payload.h"
#include "../adapt/ctl_rooms.h"
#include "../core/ctl_relay.h"
#include "../core/ctl_types.h"
#include "ctl_format.h"
#include "ctl_loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SHADOW_ROOMS 4
#define COOLING_BIT CTL_DEMAND_BIT(CTL_DEMAND_COOLING)
/* No configuration has more rooms than nine digits can name. */
#define ROOM_DIGITS_MAX 9
/* The most fields a request has, plus one, so that a surplus field shows in the count. */
#define FIELDS_MAX 5
#define CONFIGURE_FIELDS 3
#define WEIGHT_FIELDS 4
#define INPUT_FIELDS 4
#define COUNT_TEXT_MAX 16

typedef struct {
    const char *text;
    size_t length;
} field;

void ctl_shadow_init(ctl_shadow *shadow)
{
    ctl_loop_config_defaults(&shadow->config, SHADOW_ROOMS);
    shadow->config.assume_landed = true;
    shadow->started = false;
    shadow->inputs = 0;
    shadow->records = 0;
    shadow->has_record = false;
}

/* Returns the line's length, or CTL_SHADOW_LINE_MAX + 1 for any longer line, without reading
 * past that bound. */
static size_t bounded_length(const char *line)
{
    size_t length = 0;
    while (length <= CTL_SHADOW_LINE_MAX && line[length] != '\0') {
        length++;
    }
    return length;
}

/* Splits at single spaces into at most max fields; the last one keeps the rest of the line,
 * spaces included, which is how an input's payload may hold spaces. Two spaces in a row give an
 * empty field, which no request accepts. */
static size_t split(const char *line, size_t length, field *fields, size_t max)
{
    size_t count = 0;
    size_t start = 0;
    size_t i;
    for (i = 0; i < length && count + 1 < max; i++) {
        if (line[i] == ' ') {
            fields[count].text = line + start;
            fields[count].length = i - start;
            count++;
            start = i + 1;
        }
    }
    fields[count].text = line + start;
    fields[count].length = length - start;
    return count + 1;
}

static bool field_is(const field *f, const char *name)
{
    size_t length = strlen(name);
    return f->length == length && memcmp(f->text, name, length) == 0;
}

/* The payload parser also takes a JSON object and skips white space around a value; neither
 * belongs in a configuration line, so every byte must be one a bare number can hold. The parser
 * then checks the number's form. */
static bool is_bare_number(const field *f)
{
    size_t i;
    if (f->length == 0) {
        return false;
    }
    for (i = 0; i < f->length; i++) {
        if (strchr("0123456789+-.eE", f->text[i]) == NULL || f->text[i] == '\0') {
            return false;
        }
    }
    return true;
}

static bool parse_number(const field *f, double *value)
{
    ctl_payload parsed = {0.0, false, 0};
    if (!is_bare_number(f) || !ctl_payload_parse(f->text, f->length, &parsed)) {
        return false;
    }
    *value = parsed.value;
    return true;
}

static bool parse_whole(const field *f, size_t low, size_t high, size_t *whole)
{
    double value;
    if (!parse_number(f, &value) || value < (double)low || value > (double)high) {
        return false;
    }
    if ((double)(size_t)value != value) {
        return false;
    }
    *whole = (size_t)value;
    return true;
}

static bool parse_room(const field *f, size_t *room)
{
    size_t i;
    if (f->length == 0 || f->length > ROOM_DIGITS_MAX) {
        return false;
    }
    *room = 0;
    for (i = 0; i < f->length; i++) {
        if (f->text[i] < '0' || f->text[i] > '9') {
            return false;
        }
        *room = *room * 10u + (size_t)(f->text[i] - '0');
    }
    return true;
}

static bool parse_input(const field *f, ctl_input *input)
{
    static const struct {
        const char *name;
        ctl_input input;
    } inputs[] = {
        {"room", CTL_INPUT_ROOM},   {"outdoor", CTL_INPUT_OUTDOOR}, {"heat", CTL_INPUT_HEAT},
        {"valve", CTL_INPUT_VALVE}, {"defrost", CTL_INPUT_DEFROST}, {"pv", CTL_INPUT_PV},
    };
    size_t i;
    for (i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        if (field_is(f, inputs[i].name)) {
            *input = inputs[i].input;
            return true;
        }
    }
    return false;
}

static void set_reply(char *reply, size_t size, const char *text)
{
    size_t length = strlen(text);
    if (size == 0) {
        return;
    }
    if (length >= size) {
        reply[0] = '\0';
        return;
    }
    memcpy(reply, text, length + 1);
}

static bool set_rooms(ctl_shadow *shadow, const field *value)
{
    size_t count;
    double room_timeout_s = shadow->config.rooms.room_timeout_s;
    if (!parse_whole(value, 1, CTL_ROOMS_MAX, &count)) {
        return false;
    }
    /* The defaults reset every weight, which is the point, and the timeout, which is not: the
     * timeout's own line may have come first. */
    ctl_rooms_config_defaults(&shadow->config.rooms, count);
    shadow->config.rooms.room_timeout_s = room_timeout_s;
    return true;
}

static bool set_weight(ctl_shadow *shadow, const field *index, const field *value)
{
    size_t room;
    double weight;
    if (shadow->config.rooms.count == 0 ||
        !parse_whole(index, 0, shadow->config.rooms.count - 1, &room) ||
        !parse_number(value, &weight)) {
        return false;
    }
    shadow->config.rooms.weight[room] = weight;
    return true;
}

static bool set_cooling(ctl_shadow *shadow, const field *value)
{
    size_t on;
    if (!parse_whole(value, 0, 1, &on)) {
        return false;
    }
    if (on == 1) {
        shadow->config.allowed = (uint8_t)(shadow->config.allowed | COOLING_BIT);
    } else {
        shadow->config.allowed = (uint8_t)(shadow->config.allowed & (uint8_t)~COOLING_BIT);
    }
    return true;
}

/* Returns whether the K line, split into count fields, was taken. Values whose range the
 * configuration's own check covers are taken as they are; the start refuses them with "bad". */
static bool configure(ctl_shadow *shadow, const field *fields, size_t count)
{
    const field *name = &fields[1];
    if (shadow->started) {
        return false;
    }
    if (count == WEIGHT_FIELDS && field_is(name, "weight")) {
        return set_weight(shadow, &fields[2], &fields[3]);
    }
    if (count != CONFIGURE_FIELDS) {
        return false;
    }
    if (field_is(name, "rooms")) {
        return set_rooms(shadow, &fields[2]);
    }
    if (field_is(name, "room_timeout_s")) {
        return parse_number(&fields[2], &shadow->config.rooms.room_timeout_s);
    }
    if (field_is(name, "solar_gain_factor")) {
        return parse_number(&fields[2], &shadow->config.core.solar_gain_factor);
    }
    if (field_is(name, "cooling")) {
        return set_cooling(shadow, &fields[2]);
    }
    return false;
}

static const char *start(ctl_shadow *shadow, double now)
{
    if (shadow->started) {
        return "error";
    }
    if (!ctl_loop_config_valid(&shadow->config)) {
        return "bad";
    }
    ctl_loop_init(&shadow->loop, &shadow->config, now);
    shadow->started = true;
    return "ok";
}

static const char *input_reply(ctl_input_status status)
{
    switch (status) {
    case CTL_INPUT_TAKEN:
        return "taken";
    case CTL_INPUT_NO_READING:
        return "no_reading";
    case CTL_INPUT_REFUSED:
        return "refused";
    default:
        return "bad";
    }
}

static const char *offer(ctl_shadow *shadow, const field *fields, size_t count, double now)
{
    ctl_input input;
    size_t room;
    ctl_input_status status;
    if (!shadow->started || count != INPUT_FIELDS || !parse_input(&fields[1], &input) ||
        !parse_room(&fields[2], &room)) {
        return "error";
    }
    status = ctl_loop_input(&shadow->loop, &shadow->config, input, room, fields[3].text,
                            fields[3].length, now);
    shadow->inputs++;
    return input_reply(status);
}

/* The counts are below 2^32, which ctl_format_fixed writes exactly with no decimals. */
static bool append_count(char *text, size_t size, const char *key, uint32_t count)
{
    char number[COUNT_TEXT_MAX];
    size_t length = strlen(text);
    if (!ctl_format_fixed((double)count, 0, number, sizeof number) ||
        length + strlen(key) + strlen(number) >= size) {
        return false;
    }
    strcat(text, key);
    strcat(text, number);
    return true;
}

static void stats(const ctl_shadow *shadow, char *reply, size_t size)
{
    char text[sizeof "stats inputs= records=" + 2 * COUNT_TEXT_MAX];
    strcpy(text, "stats");
    (void)append_count(text, sizeof text, " inputs=", shadow->inputs);
    (void)append_count(text, sizeof text, " records=", shadow->records);
    set_reply(reply, size, text);
}

void ctl_shadow_handle(ctl_shadow *shadow, const char *line, double now, char *reply, size_t size)
{
    field fields[FIELDS_MAX];
    size_t length = bounded_length(line);
    size_t count;
    if (length > CTL_SHADOW_LINE_MAX) {
        set_reply(reply, size, "error");
        return;
    }
    count = split(line, length, fields, FIELDS_MAX);
    if (field_is(&fields[0], "K")) {
        set_reply(reply, size, configure(shadow, fields, count) ? "ok" : "error");
    } else if (field_is(&fields[0], "I")) {
        count = split(line, length, fields, INPUT_FIELDS);
        set_reply(reply, size, offer(shadow, fields, count, now));
    } else if (field_is(&fields[0], "G") && count == 1) {
        set_reply(reply, size, start(shadow, now));
    } else if (field_is(&fields[0], "S") && count == 1) {
        stats(shadow, reply, size);
    } else {
        set_reply(reply, size, "error");
    }
}

static const char RECORD_PREFIX[] = "D ";

bool ctl_shadow_poll(ctl_shadow *shadow, double now, char *text, size_t size)
{
    char record[CTL_LOOP_TEXT_MAX];
    size_t prefix = sizeof RECORD_PREFIX - 1;
    size_t length;
    if (!shadow->started) {
        return false;
    }
    if (ctl_loop_tick(&shadow->loop, &shadow->config, now, &shadow->record)) {
        shadow->has_record = true;
    }
    if (!shadow->has_record ||
        !ctl_loop_format_record(&shadow->record, record, sizeof record)) {
        return false;
    }
    length = strlen(record);
    if (prefix + length >= size) {
        return false;
    }
    memcpy(text, RECORD_PREFIX, prefix);
    memcpy(text + prefix, record, length + 1);
    shadow->has_record = false;
    shadow->records++;
    return true;
}
