#include "trace.h"

#include "core/ctl_config.h"
#include "core/ctl_controller.h"
#include "core/ctl_relay.h"
#include "core/ctl_types.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The longest line of the format has 15 fields; a few more let the count check say "surplus". */
#define MAX_FIELDS 24
#define LEDGER_TERMS 5
#define STATE_PAIRS 4
#define DECISION_PAIRS 12
#define TEXT_MAX 96

static const char *const STATE_KEYS[STATE_PAIRS] = {"E", "int", "skip", "clamped"};
static const char *const DECISION_KEYS[DECISION_PAIRS] = {
    "demand", "reason", "phase", "E", "dt", "int", "skip", "clamped",
    "pnet", "unhealthy", "substituted", "ledger"};
static const char *const LEDGER_KEYS[LEDGER_TERMS] = {
    "ledger.produced", "ledger.solar", "ledger.internal", "ledger.transmission", "ledger.trim"};

typedef enum { CALL_OBSERVE, CALL_ADVANCE, CALL_ALLOW, CALL_EVALUATE } call_kind;

typedef struct {
    call_kind kind;
    double t;
    ctl_signal signal;
    double value;
    uint8_t demands;
} trace_call;

typedef struct {
    double energy_wh;
    double integrated_s;
    double skipped_s;
    bool clamped;
} expected_state;

typedef struct {
    ctl_demand demand;
    ctl_reason reason;
    ctl_phase phase;
    double energy_wh;
    double dt_s;
    double integrated_s;
    double skipped_s;
    bool clamped;
    bool has_p_net;
    double p_net_w;
    uint8_t unhealthy;
    uint8_t substituted;
    double ledger[LEDGER_TERMS];
} expected_decision;

typedef struct {
    FILE *output;
    const char *path;
    long line;
    long mismatch_limit;
    trace_result *result;
    ctl_config config;
    ctl_state state;
} replay;

typedef enum { READ_LINE, READ_END_OF_FILE, READ_TOO_LONG } read_status;

typedef enum { PART_VERSION, PART_HEADER, PART_BODY, PART_AFTER_END } trace_part;

/* Splits at every separator. An empty field, as two separators in a row make, is refused: the
 * format separates its fields by single characters. */
static bool split(char *text, char separator, char **fields, int max_fields, int *count)
{
    char *start = text;
    *count = 0;
    for (;;) {
        char *stop = strchr(start, separator);
        if (stop != NULL) {
            *stop = '\0';
        }
        if (*start == '\0' || *count == max_fields) {
            return false;
        }
        fields[(*count)++] = start;
        if (stop == NULL) {
            return true;
        }
        start = stop + 1;
    }
}

/* strtod alone would skip leading white space and stop at trailing text; the format allows
 * neither. */
static bool parse_number(const char *text, double *value)
{
    char *end;
    if (*text == '\0' || isspace((unsigned char)*text)) {
        return false;
    }
    *value = strtod(text, &end);
    return *end == '\0';
}

static bool parse_count(const char *text, long *value)
{
    char *end;
    if (!isdigit((unsigned char)*text)) {
        return false;
    }
    *value = strtol(text, &end, 10);
    return *end == '\0';
}

static bool parse_flag(const char *text, bool *value)
{
    if (strcmp(text, "0") == 0 || strcmp(text, "1") == 0) {
        *value = text[0] == '1';
        return true;
    }
    return false;
}

static bool parse_signal(const char *text, ctl_signal *signal)
{
    int index;
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        if (strcmp(text, ctl_signal_name((ctl_signal)index)) == 0) {
            *signal = (ctl_signal)index;
            return true;
        }
    }
    return false;
}

static bool parse_demand(const char *text, ctl_demand *demand)
{
    int index;
    for (index = 0; index <= (int)CTL_DEMAND_NONE; index++) {
        if (strcmp(text, ctl_demand_name((ctl_demand)index)) == 0) {
            *demand = (ctl_demand)index;
            return true;
        }
    }
    return false;
}

static bool parse_reason(const char *text, ctl_reason *reason)
{
    int index;
    for (index = 0; index <= (int)CTL_REASON_INVALID_TIME; index++) {
        if (strcmp(text, ctl_reason_name((ctl_reason)index)) == 0) {
            *reason = (ctl_reason)index;
            return true;
        }
    }
    return false;
}

static bool parse_phase(const char *text, ctl_phase *phase)
{
    int index;
    for (index = 0; index <= (int)CTL_PHASE_FAULT; index++) {
        if (strcmp(text, ctl_phase_name((ctl_phase)index)) == 0) {
            *phase = (ctl_phase)index;
            return true;
        }
    }
    return false;
}

/* A set is written as names separated by commas, or "-" when it is empty. The text is split in
 * place. */
static bool parse_signal_set(char *text, uint8_t *set)
{
    char *names[CTL_SIGNAL_COUNT];
    int count;
    int index;
    *set = 0;
    if (strcmp(text, "-") == 0) {
        return true;
    }
    if (!split(text, ',', names, CTL_SIGNAL_COUNT, &count)) {
        return false;
    }
    for (index = 0; index < count; index++) {
        ctl_signal signal;
        if (!parse_signal(names[index], &signal)) {
            return false;
        }
        *set |= CTL_SIGNAL_BIT(signal);
    }
    return true;
}

static bool parse_demand_set(char *text, uint8_t *set)
{
    char *names[CTL_DEMAND_NONE + 1];
    int count;
    int index;
    *set = 0;
    if (strcmp(text, "-") == 0) {
        return true;
    }
    if (!split(text, ',', names, CTL_DEMAND_NONE + 1, &count)) {
        return false;
    }
    for (index = 0; index < count; index++) {
        ctl_demand demand;
        if (!parse_demand(names[index], &demand)) {
            return false;
        }
        *set |= CTL_DEMAND_BIT(demand);
    }
    return true;
}

static bool parse_ledger(char *text, double *ledger)
{
    char *terms[LEDGER_TERMS];
    int count;
    int index;
    if (!split(text, ',', terms, LEDGER_TERMS, &count) || count != LEDGER_TERMS) {
        return false;
    }
    for (index = 0; index < LEDGER_TERMS; index++) {
        if (!parse_number(terms[index], &ledger[index])) {
            return false;
        }
    }
    return true;
}

/* Fills values with the text after "key=" of each pair, and refuses a pair whose key is not
 * the one the format gives at its place. */
static bool pair_values(char **pairs, const char *const *keys, int count, char **values)
{
    int index;
    for (index = 0; index < count; index++) {
        size_t length = strlen(keys[index]);
        if (strncmp(pairs[index], keys[index], length) != 0 || pairs[index][length] != '=') {
            return false;
        }
        values[index] = pairs[index] + length + 1;
    }
    return true;
}

static bool parse_state_pairs(char **pairs, expected_state *expected)
{
    char *values[STATE_PAIRS];
    return pair_values(pairs, STATE_KEYS, STATE_PAIRS, values) &&
           parse_number(values[0], &expected->energy_wh) &&
           parse_number(values[1], &expected->integrated_s) &&
           parse_number(values[2], &expected->skipped_s) &&
           parse_flag(values[3], &expected->clamped);
}

static bool parse_p_net(const char *text, expected_decision *expected)
{
    expected->has_p_net = strcmp(text, "none") != 0;
    expected->p_net_w = 0.0;
    return !expected->has_p_net || parse_number(text, &expected->p_net_w);
}

static bool parse_decision_pairs(char **pairs, expected_decision *expected)
{
    char *values[DECISION_PAIRS];
    return pair_values(pairs, DECISION_KEYS, DECISION_PAIRS, values) &&
           parse_demand(values[0], &expected->demand) &&
           parse_reason(values[1], &expected->reason) &&
           parse_phase(values[2], &expected->phase) &&
           parse_number(values[3], &expected->energy_wh) &&
           parse_number(values[4], &expected->dt_s) &&
           parse_number(values[5], &expected->integrated_s) &&
           parse_number(values[6], &expected->skipped_s) &&
           parse_flag(values[7], &expected->clamped) &&
           parse_p_net(values[8], expected) &&
           parse_signal_set(values[9], &expected->unhealthy) &&
           parse_signal_set(values[10], &expected->substituted) &&
           parse_ledger(values[11], expected->ledger);
}

/* The arguments of the call, without its name: fields[0] is the first argument. */
static bool parse_call_arguments(char **fields, int count, trace_call *call)
{
    switch (call->kind) {
    case CALL_OBSERVE:
        return count == 3 && parse_number(fields[0], &call->t) &&
               parse_signal(fields[1], &call->signal) && parse_number(fields[2], &call->value);
    case CALL_ALLOW:
        return count == 2 && parse_number(fields[0], &call->t) &&
               parse_demand_set(fields[1], &call->demands);
    case CALL_ADVANCE:
    case CALL_EVALUATE:
        return count == 1 && parse_number(fields[0], &call->t);
    }
    return false;
}

static bool parse_call_kind(const char *name, call_kind *kind)
{
    static const char *const NAMES[] = {"observe", "advance", "allow", "evaluate"};
    size_t index;
    for (index = 0; index < sizeof NAMES / sizeof NAMES[0]; index++) {
        if (strcmp(name, NAMES[index]) == 0) {
            *kind = (call_kind)index;
            return true;
        }
    }
    return false;
}

static bool is_stopped(const replay *run)
{
    return run->result->mismatches >= run->mismatch_limit;
}

static void report(replay *run, const char *key, const char *expected, const char *actual)
{
    fprintf(run->output, "%s:%ld: %s expected %s, got %s\n", run->path, run->line, key, expected, actual);
    run->result->mismatches++;
}

static void format_number(char *text, double value)
{
    /* 17 significant digits tell apart any two doubles. */
    snprintf(text, TEXT_MAX, "%.17g", value);
}

/* Equal by ==, so -0.0 equals 0.0, or both NaN: a NaN the reference recorded is a NaN the port
 * must hold too. */
static void compare_number(replay *run, const char *key, double expected, double actual)
{
    char expected_text[TEXT_MAX];
    char actual_text[TEXT_MAX];
    if (is_stopped(run) || expected == actual || (isnan(expected) && isnan(actual))) {
        return;
    }
    format_number(expected_text, expected);
    format_number(actual_text, actual);
    report(run, key, expected_text, actual_text);
}

static void compare_flag(replay *run, const char *key, bool expected, bool actual)
{
    if (is_stopped(run) || expected == actual) {
        return;
    }
    report(run, key, expected ? "1" : "0", actual ? "1" : "0");
}

static void compare_name(replay *run, const char *key, int expected, int actual,
                         const char *expected_name, const char *actual_name)
{
    if (is_stopped(run) || expected == actual) {
        return;
    }
    report(run, key, expected_name, actual_name);
}

/* Appends to text, a buffer of size characters, cutting what does not fit. */
static void append(char *text, size_t size, const char *tail)
{
    size_t length = strlen(text);
    snprintf(text + length, size - length, "%s", tail);
}

static void format_signal_set(char *text, size_t size, uint8_t set)
{
    int index;
    text[0] = '\0';
    for (index = 0; index < CTL_SIGNAL_COUNT; index++) {
        if ((set & CTL_SIGNAL_BIT(index)) != 0) {
            if (text[0] != '\0') {
                append(text, size, ",");
            }
            append(text, size, ctl_signal_name((ctl_signal)index));
        }
    }
    if (text[0] == '\0') {
        append(text, size, "-");
    }
}

static void compare_signal_set(replay *run, const char *key, uint8_t expected, uint8_t actual)
{
    char expected_text[TEXT_MAX];
    char actual_text[TEXT_MAX];
    if (is_stopped(run) || expected == actual) {
        return;
    }
    format_signal_set(expected_text, sizeof expected_text, expected);
    format_signal_set(actual_text, sizeof actual_text, actual);
    report(run, key, expected_text, actual_text);
}

/* "none" is the reference's word for a decision that computed no balance. */
static void compare_p_net(replay *run, const expected_decision *expected,
                          const ctl_decision *actual)
{
    char text[TEXT_MAX];
    if (is_stopped(run)) {
        return;
    }
    if (expected->has_p_net && actual->has_inputs) {
        compare_number(run, "pnet", expected->p_net_w, actual->balance.p_net_w);
    } else if (expected->has_p_net) {
        format_number(text, expected->p_net_w);
        report(run, "pnet", text, "none");
    } else if (actual->has_inputs) {
        format_number(text, actual->balance.p_net_w);
        report(run, "pnet", "none", text);
    }
}

static void compare_state(replay *run, const expected_state *expected)
{
    compare_number(run, "E", expected->energy_wh, run->state.energy_wh);
    compare_number(run, "int", expected->integrated_s, run->state.integrated_s);
    compare_number(run, "skip", expected->skipped_s, run->state.skipped_s);
    compare_flag(run, "clamped", expected->clamped, run->state.clamped);
}

static void compare_decision(replay *run, const expected_decision *expected,
                             const ctl_decision *actual)
{
    const double actual_ledger[LEDGER_TERMS] = {
        actual->ledger.produced_wh, actual->ledger.solar_wh, actual->ledger.internal_wh,
        actual->ledger.transmission_wh, actual->ledger.trim_wh};
    int term;
    compare_name(run, "demand", (int)expected->demand, (int)actual->demand,
                 ctl_demand_name(expected->demand), ctl_demand_name(actual->demand));
    compare_name(run, "reason", (int)expected->reason, (int)actual->reason,
                 ctl_reason_name(expected->reason), ctl_reason_name(actual->reason));
    compare_name(run, "phase", (int)expected->phase, (int)actual->phase,
                 ctl_phase_name(expected->phase), ctl_phase_name(actual->phase));
    compare_number(run, "E", expected->energy_wh, actual->energy_wh);
    compare_number(run, "dt", expected->dt_s, actual->dt_s);
    compare_number(run, "int", expected->integrated_s, actual->integrated_s);
    compare_number(run, "skip", expected->skipped_s, actual->skipped_s);
    compare_flag(run, "clamped", expected->clamped, actual->clamped);
    compare_p_net(run, expected, actual);
    compare_signal_set(run, "unhealthy", expected->unhealthy, actual->unhealthy);
    compare_signal_set(run, "substituted", expected->substituted, actual->substituted);
    for (term = 0; term < LEDGER_TERMS; term++) {
        compare_number(run, LEDGER_KEYS[term], expected->ledger[term], actual_ledger[term]);
    }
}

static void make_call(replay *run, const trace_call *call, ctl_decision *decision)
{
    double dt_s;
    switch (call->kind) {
    case CALL_OBSERVE:
        ctl_observe(&run->state, call->signal, call->value, call->t, &run->config);
        break;
    case CALL_ADVANCE:
        (void)ctl_advance(&run->state, call->t, &run->config, &dt_s);
        break;
    case CALL_ALLOW:
        ctl_set_allowed(&run->state, call->demands, call->t, &run->config);
        break;
    case CALL_EVALUATE:
        ctl_evaluate(&run->state, call->t, &run->config, decision);
        break;
    }
}

/* Returns the index of the field "|", or count when the line has none. */
static int find_bar(char **fields, int count)
{
    int index = 1;
    while (index < count && strcmp(fields[index], "|") != 0) {
        index++;
    }
    return index;
}

/* A call line is the call, "|", and the pairs. The whole line is read before the call is made,
 * so that a format error never follows a call. Returns false on a format error. */
static bool replay_call(replay *run, char **fields, int count)
{
    trace_call call;
    expected_state state;
    expected_decision decision;
    ctl_decision actual;
    int bar;
    int pairs;
    if (!parse_call_kind(fields[0], &call.kind)) {
        return false;
    }
    bar = find_bar(fields, count);
    if (bar == count || !parse_call_arguments(fields + 1, bar - 1, &call)) {
        return false;
    }
    pairs = count - bar - 1;
    if (call.kind == CALL_EVALUATE) {
        if (pairs != DECISION_PAIRS || !parse_decision_pairs(fields + bar + 1, &decision)) {
            return false;
        }
    } else if (pairs != STATE_PAIRS || !parse_state_pairs(fields + bar + 1, &state)) {
        return false;
    }
    make_call(run, &call, &actual);
    run->result->calls++;
    if (call.kind == CALL_EVALUATE) {
        compare_decision(run, &decision, &actual);
    } else {
        compare_state(run, &state);
    }
    return true;
}

static bool set_signal_field(double *fields, const char *signal_name, double value,
                             bool optional_only)
{
    ctl_signal signal;
    if (!parse_signal(signal_name, &signal) ||
        (optional_only && ctl_signal_is_required(signal))) {
        return false;
    }
    fields[signal] = value;
    return true;
}

/* Sets the configuration field the key names. Returns false for a key the C configuration does
 * not have and for a value that is no number. */
static bool set_config(ctl_config *config, const char *key, const char *text)
{
    static const struct {
        const char *key;
        size_t offset;
    } FIELDS[] = {
        {"desired_indoor_c", offsetof(ctl_config, desired_indoor_c)},
        {"transmission_w_per_k", offsetof(ctl_config, transmission_w_per_k)},
        {"adjustment_w_per_k", offsetof(ctl_config, adjustment_w_per_k)},
        {"hysteresis_wh", offsetof(ctl_config, hysteresis_wh)},
        {"solar_gain_factor", offsetof(ctl_config, solar_gain_factor)},
        {"energy_clamp_factor", offsetof(ctl_config, energy_clamp_factor)},
        {"max_step_s", offsetof(ctl_config, max_step_s)},
        {"tick_interval_s", offsetof(ctl_config, tick_interval_s)},
        {"integration_interval_s", offsetof(ctl_config, integration_interval_s)},
        {"input_hold_s", offsetof(ctl_config, input_hold_s)},
    };
    size_t index;
    double value;
    if (strcmp(key, "trim_limit_k") == 0 && strcmp(text, "none") == 0) {
        config->trim_limit_enabled = false;
        return true;
    }
    if (!parse_number(text, &value)) {
        return false;
    }
    if (strcmp(key, "trim_limit_k") == 0) {
        config->trim_limit_enabled = true;
        config->trim_limit_k = value;
        return true;
    }
    /* The board keeps no snapshot, so the core has no use for these two. */
    if (strcmp(key, "state_max_age_s") == 0 || strcmp(key, "persist_interval_s") == 0) {
        return true;
    }
    if (strncmp(key, "max_age_s.", 10) == 0) {
        return set_signal_field(config->max_age_s, key + 10, value, false);
    }
    if (strncmp(key, "fallback_w.", 11) == 0) {
        return set_signal_field(config->fallback_w, key + 11, value, true);
    }
    for (index = 0; index < sizeof FIELDS / sizeof FIELDS[0]; index++) {
        if (strcmp(key, FIELDS[index].key) == 0) {
            *(double *)((char *)config + FIELDS[index].offset) = value;
            return true;
        }
    }
    return false;
}

/* Returns false on a format error; *part moves to the body at "begin". */
static bool read_header_line(replay *run, char **fields, int count, trace_part *part)
{
    if (strcmp(fields[0], "scenario") == 0) {
        return count == 2;
    }
    if (strcmp(fields[0], "config") == 0) {
        return count == 3 && set_config(&run->config, fields[1], fields[2]);
    }
    if (strcmp(fields[0], "begin") == 0 && count == 1) {
        *part = PART_BODY;
        return ctl_config_validate(&run->config) == 0;
    }
    return false;
}

static bool read_body_line(replay *run, char **fields, int count, trace_part *part)
{
    long announced;
    if (strcmp(fields[0], "end") == 0) {
        *part = PART_AFTER_END;
        return count == 2 && parse_count(fields[1], &announced) &&
               announced == run->result->calls;
    }
    return replay_call(run, fields, count);
}

/* Returns false on a format error. */
static bool read_line(replay *run, char *line, trace_part *part)
{
    char *fields[MAX_FIELDS];
    int count;
    if (*part == PART_VERSION) {
        *part = PART_HEADER;
        return strcmp(line, "heatnrgx-golden 1") == 0;
    }
    if (line[0] == '\0' || line[0] == '#') {
        return true;
    }
    if (*part == PART_AFTER_END || !split(line, ' ', fields, MAX_FIELDS, &count)) {
        return false;
    }
    if (*part == PART_HEADER) {
        return read_header_line(run, fields, count, part);
    }
    return read_body_line(run, fields, count, part);
}

/* The buffer holds a line of TRACE_LINE_MAX characters, its newline and the terminator. */
static read_status next_line(FILE *file, char *buffer, size_t size)
{
    size_t length;
    if (fgets(buffer, (int)size, file) == NULL) {
        return READ_END_OF_FILE;
    }
    length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[length - 1] = '\0';
        return READ_LINE;
    }
    return length == size - 1 ? READ_TOO_LONG : READ_LINE;
}

static void replay_file(replay *run, FILE *file)
{
    char buffer[TRACE_LINE_MAX + 2];
    trace_part part = PART_VERSION;
    for (;;) {
        read_status status = next_line(file, buffer, sizeof buffer);
        run->line++;
        if (status == READ_END_OF_FILE) {
            if (part != PART_AFTER_END) {
                run->result->format_line = run->line;
            }
            return;
        }
        if (status == READ_TOO_LONG || !read_line(run, buffer, &part)) {
            run->result->format_line = run->line;
            return;
        }
        if (is_stopped(run)) {
            return;
        }
    }
}

bool trace_replay(const char *path, long mismatch_limit, trace_result *result)
{
    return trace_replay_to(stdout, path, mismatch_limit, result);
}

bool trace_replay_to(FILE *output, const char *path, long mismatch_limit, trace_result *result)
{
    replay run;
    FILE *file;
    result->calls = 0;
    result->mismatches = 0;
    result->format_line = 0;
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(output, "%s: cannot open\n", path);
        return false;
    }
    run.output = output;
    run.path = path;
    run.line = 0;
    run.mismatch_limit = mismatch_limit < 1 ? 1 : mismatch_limit;
    run.result = result;
    ctl_config_defaults(&run.config);
    ctl_state_init(&run.state);
    replay_file(&run, file);
    fclose(file);
    /* Without a format error the reader reached the end line: a file without one is an error. */
    return result->format_line == 0 && result->mismatches == 0;
}
