#include "ctl_wire.h"

#include "../core/ctl_balance.h"
#include "../core/ctl_config.h"
#include "../core/ctl_controller.h"
#include "../core/ctl_relay.h"
#include "../core/ctl_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The longest request, O, has a name and three fields. */
#define FIELDS_MAX 4
#define DOUBLE_DIGITS 16
#define SIGNAL_PREFIX_MAX_AGE "max_age_s."
#define SIGNAL_PREFIX_FALLBACK "fallback_w."

static const char HEX_DIGITS[] = "0123456789abcdef";

/* A reply under construction: what does not fit in size - 1 characters is cut, so that a small
 * buffer can never be overrun. */
typedef struct {
    char *text;
    size_t size;
    size_t length;
} reply_text;

static void reply_start(reply_text *reply, char *text, size_t size)
{
    reply->text = text;
    reply->size = size;
    reply->length = 0;
    text[0] = '\0';
}

static void append_char(reply_text *reply, char c)
{
    if (reply->length + 1 < reply->size) {
        reply->text[reply->length++] = c;
        reply->text[reply->length] = '\0';
    }
}

static void append_text(reply_text *reply, const char *text)
{
    size_t i;
    for (i = 0; text[i] != '\0'; i++) {
        append_char(reply, text[i]);
    }
}

static void append_field(reply_text *reply, const char *text)
{
    if (reply->length > 0) {
        append_char(reply, ' ');
    }
    append_text(reply, text);
}

static void append_double(reply_text *reply, double value)
{
    char text[DOUBLE_DIGITS + 1];
    ctl_wire_format_double(value, text);
    append_field(reply, text);
}

/* Enums and flags of the protocol are below 10, but a wider value must not break the line. */
static void append_decimal(reply_text *reply, uint32_t value)
{
    char text[11];
    size_t length = 0;
    size_t i;
    do {
        text[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    if (reply->length > 0) {
        append_char(reply, ' ');
    }
    for (i = length; i > 0; i--) {
        append_char(reply, text[i - 1]);
    }
}

static void append_hex(reply_text *reply, uint32_t value, int digits)
{
    int shift;
    if (reply->length > 0) {
        append_char(reply, ' ');
    }
    for (shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        append_char(reply, HEX_DIGITS[(value >> shift) & 0xfu]);
    }
}

void ctl_wire_format_double(double value, char *text)
{
    uint64_t bits;
    int i;
    memcpy(&bits, &value, sizeof bits);
    for (i = DOUBLE_DIGITS - 1; i >= 0; i--) {
        text[i] = HEX_DIGITS[bits & 0xfu];
        bits >>= 4;
    }
    text[DOUBLE_DIGITS] = '\0';
}

/* Returns the digit's value, or -1 for anything but a lowercase hexadecimal digit. */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

bool ctl_wire_parse_double(const char *text, double *value)
{
    uint64_t bits = 0;
    int i;
    /* The terminating zero is no digit, so a short text stops the loop before its end. */
    for (i = 0; i < DOUBLE_DIGITS; i++) {
        int digit = hex_value(text[i]);
        if (digit < 0) {
            return false;
        }
        bits = (bits << 4) | (uint64_t)digit;
    }
    if (text[DOUBLE_DIGITS] != '\0') {
        return false;
    }
    memcpy(value, &bits, sizeof *value);
    return true;
}

/* A one-character field whose value lies from 0 to max, in the given base. */
static bool parse_digit(const char *text, int max, int *value)
{
    int digit = hex_value(text[0]);
    if (digit < 0 || digit > max || text[1] != '\0') {
        return false;
    }
    *value = digit;
    return true;
}

static bool parse_signal(const char *text, ctl_signal *signal)
{
    int index;
    if (!parse_digit(text, CTL_SIGNAL_COUNT - 1, &index)) {
        return false;
    }
    *signal = (ctl_signal)index;
    return true;
}

/* Returns the length of the text, or limit + 1 when it is longer than limit, so that a line
 * without its terminating zero in reach is never read past. */
static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length <= limit && text[length] != '\0') {
        length++;
    }
    return length;
}

/* Splits the copy of the line at single spaces; an empty field, as two spaces in a row or a
 * space at either end make, is refused, as are more than FIELDS_MAX fields. */
static bool split(char *line, char **fields, int *count)
{
    char *start = line;
    int i;
    *count = 0;
    for (i = 0; i <= CTL_WIRE_LINE_MAX; i++) {
        char *stop = strchr(start, ' ');
        if (*start == '\0' || start == stop || *count == FIELDS_MAX) {
            return false;
        }
        fields[(*count)++] = start;
        if (stop == NULL) {
            return true;
        }
        *stop = '\0';
        start = stop + 1;
    }
    return false;
}

static void write_state(const ctl_state *state, reply_text *reply)
{
    append_double(reply, state->energy_wh);
    append_double(reply, state->integrated_s);
    append_double(reply, state->skipped_s);
    append_decimal(reply, state->clamped ? 1u : 0u);
}

static void write_decision(const ctl_decision *decision, reply_text *reply)
{
    append_decimal(reply, (uint32_t)decision->demand);
    append_decimal(reply, (uint32_t)decision->reason);
    append_decimal(reply, (uint32_t)decision->phase);
    append_double(reply, decision->energy_wh);
    append_double(reply, decision->dt_s);
    append_double(reply, decision->integrated_s);
    append_double(reply, decision->skipped_s);
    append_decimal(reply, decision->clamped ? 1u : 0u);
    if (decision->has_inputs) {
        append_double(reply, decision->balance.p_net_w);
    } else {
        append_field(reply, "-");
    }
    append_hex(reply, decision->unhealthy, 2);
    append_hex(reply, decision->substituted, 2);
    append_double(reply, decision->ledger.produced_wh);
    append_double(reply, decision->ledger.solar_wh);
    append_double(reply, decision->ledger.internal_wh);
    append_double(reply, decision->ledger.transmission_wh);
    append_double(reply, decision->ledger.trim_wh);
}

/* Returns the field of a per-signal array the name ends in, as in "max_age_s.3", or NULL. */
static double *signal_field(double *fields, const char *name, const char *prefix)
{
    size_t length = strlen(prefix);
    ctl_signal signal;
    if (strncmp(name, prefix, length) != 0 || !parse_signal(name + length, &signal)) {
        return NULL;
    }
    return &fields[signal];
}

/* Returns the double field of the configuration the name gives, or NULL for any other name. */
static double *config_field(ctl_config *config, const char *name)
{
    const struct {
        const char *name;
        double *field;
    } fields[] = {
        {"desired_indoor_c", &config->desired_indoor_c},
        {"transmission_w_per_k", &config->transmission_w_per_k},
        {"adjustment_w_per_k", &config->adjustment_w_per_k},
        {"hysteresis_wh", &config->hysteresis_wh},
        {"trim_limit_k", &config->trim_limit_k},
        {"solar_gain_factor", &config->solar_gain_factor},
        {"energy_clamp_factor", &config->energy_clamp_factor},
        {"max_step_s", &config->max_step_s},
        {"tick_interval_s", &config->tick_interval_s},
        {"integration_interval_s", &config->integration_interval_s},
        {"input_hold_s", &config->input_hold_s},
    };
    double *field;
    size_t i;
    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        if (strcmp(name, fields[i].name) == 0) {
            return fields[i].field;
        }
    }
    field = signal_field(config->max_age_s, name, SIGNAL_PREFIX_MAX_AGE);
    if (field == NULL) {
        field = signal_field(config->fallback_w, name, SIGNAL_PREFIX_FALLBACK);
    }
    return field;
}

static bool handle_config(ctl_wire_session *session, char **fields, reply_text *reply)
{
    double *field;
    double value;
    int flag;
    if (strcmp(fields[1], "trim_limit_enabled") == 0) {
        if (!parse_digit(fields[2], 1, &flag)) {
            return false;
        }
        session->config.trim_limit_enabled = flag == 1;
    } else {
        field = config_field(&session->config, fields[1]);
        if (field == NULL || !ctl_wire_parse_double(fields[2], &value)) {
            return false;
        }
        *field = value;
    }
    append_text(reply, "ok");
    return true;
}

static void handle_reset(ctl_wire_session *session, reply_text *reply)
{
    ctl_wire_init(session);
    append_text(reply, "ok");
}

static void handle_begin(ctl_wire_session *session, reply_text *reply)
{
    uint32_t problems = ctl_config_validate(&session->config);
    if (problems != 0u) {
        session->begun = false;
        append_text(reply, "bad");
        append_hex(reply, problems, 8);
        return;
    }
    ctl_state_init(&session->state);
    session->begun = true;
    session->calls = 0;
    append_text(reply, "ok");
}

static bool handle_observe(ctl_wire_session *session, char **fields, reply_text *reply)
{
    double t;
    double value;
    ctl_signal signal;
    if (!ctl_wire_parse_double(fields[1], &t) || !parse_signal(fields[2], &signal) ||
        !ctl_wire_parse_double(fields[3], &value)) {
        return false;
    }
    ctl_observe(&session->state, signal, value, t, &session->config);
    write_state(&session->state, reply);
    return true;
}

static bool handle_advance(ctl_wire_session *session, char **fields, reply_text *reply)
{
    double t;
    double dt_s;
    if (!ctl_wire_parse_double(fields[1], &t)) {
        return false;
    }
    (void)ctl_advance(&session->state, t, &session->config, &dt_s);
    write_state(&session->state, reply);
    return true;
}

static bool handle_allowed(ctl_wire_session *session, char **fields, reply_text *reply)
{
    double t;
    int allowed;
    if (!ctl_wire_parse_double(fields[1], &t) || !parse_digit(fields[2], CTL_DEMAND_ALL, &allowed)) {
        return false;
    }
    ctl_set_allowed(&session->state, (uint8_t)allowed, t, &session->config);
    write_state(&session->state, reply);
    return true;
}

static bool handle_evaluate(ctl_wire_session *session, char **fields, reply_text *reply)
{
    double t;
    ctl_decision decision;
    if (!ctl_wire_parse_double(fields[1], &t)) {
        return false;
    }
    ctl_evaluate(&session->state, t, &session->config, &decision);
    write_decision(&decision, reply);
    return true;
}

/* The calls on the core; each needs a successful B first and counts as one call. */
static bool handle_call(ctl_wire_session *session, char **fields, int count, reply_text *reply)
{
    bool answered = false;
    if (!session->begun || fields[0][1] != '\0') {
        return false;
    }
    switch (fields[0][0]) {
    case 'O':
        answered = count == 4 && handle_observe(session, fields, reply);
        break;
    case 'A':
        answered = count == 2 && handle_advance(session, fields, reply);
        break;
    case 'M':
        answered = count == 3 && handle_allowed(session, fields, reply);
        break;
    case 'E':
        answered = count == 2 && handle_evaluate(session, fields, reply);
        break;
    default:
        break;
    }
    if (answered) {
        session->calls++;
    }
    return answered;
}

static bool handle_fields(ctl_wire_session *session, char **fields, int count, reply_text *reply)
{
    if (strcmp(fields[0], "R") == 0 && count == 1) {
        handle_reset(session, reply);
        return true;
    }
    if (strcmp(fields[0], "B") == 0 && count == 1) {
        handle_begin(session, reply);
        return true;
    }
    if (strcmp(fields[0], "C") == 0) {
        return count == 3 && handle_config(session, fields, reply);
    }
    return handle_call(session, fields, count, reply);
}

void ctl_wire_init(ctl_wire_session *session)
{
    ctl_config_defaults(&session->config);
    ctl_state_init(&session->state);
    session->begun = false;
    session->calls = 0;
}

void ctl_wire_handle(ctl_wire_session *session, const char *line, char *reply, size_t reply_size)
{
    char copy[CTL_WIRE_LINE_MAX + 2];
    char *fields[FIELDS_MAX];
    reply_text text;
    size_t length;
    int count;
    if (reply_size == 0) {
        return;
    }
    reply_start(&text, reply, reply_size);
    /* One character beyond the limit leaves room for a carriage return after a full line. */
    length = bounded_length(line, CTL_WIRE_LINE_MAX + 1);
    if (length > 0 && line[length - 1] == '\r') {
        length--;
    }
    if (length <= CTL_WIRE_LINE_MAX) {
        memcpy(copy, line, length);
        copy[length] = '\0';
        if (split(copy, fields, &count) && handle_fields(session, fields, count, &text)) {
            return;
        }
    }
    reply_start(&text, reply, reply_size);
    append_text(&text, "error");
}
