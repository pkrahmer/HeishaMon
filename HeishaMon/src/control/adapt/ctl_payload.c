#include "ctl_payload.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 18 digits stay below INT64_MAX, so the timestamp cannot overflow while it is read. */
#define TIMESTAMP_DIGITS_MAX 18
/* The shortest member, `"":0`, takes 4 bytes, so no payload of CTL_PAYLOAD_MAX bytes holds more. */
#define MEMBERS_MAX (CTL_PAYLOAD_MAX / 4)

typedef struct {
    const char *text;
    size_t position;
    size_t end;
} cursor;

typedef struct {
    size_t start;
    size_t length;
} span;

static bool at_end(const cursor *at)
{
    return at->position >= at->end;
}

static char current(const cursor *at)
{
    return at->text[at->position];
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void skip_spaces(cursor *at)
{
    while (!at_end(at) && is_space(current(at))) {
        at->position++;
    }
}

static bool take(cursor *at, char expected)
{
    if (at_end(at) || current(at) != expected) {
        return false;
    }
    at->position++;
    return true;
}

static size_t skip_digits(cursor *at)
{
    size_t count = 0;
    while (!at_end(at) && is_digit(current(at))) {
        at->position++;
        count++;
    }
    return count;
}

/* Checks the form before strtod sees the text: strtod alone would also take a leading '+', a
 * leading point, "nan", "inf" and hexadecimal, which no source sends. */
static bool scan_number(cursor *at, span *number)
{
    number->start = at->position;
    take(at, '-');
    if (skip_digits(at) == 0) {
        return false;
    }
    if (take(at, '.') && skip_digits(at) == 0) {
        return false;
    }
    if (take(at, 'e') || take(at, 'E')) {
        if (!take(at, '-')) {
            take(at, '+');
        }
        if (skip_digits(at) == 0) {
            return false;
        }
    }
    number->length = at->position - number->start;
    return true;
}

/* strtod needs a terminating zero, which the payload's text does not have. */
static bool convert_number(const char *text, span number, double *value)
{
    char buffer[CTL_PAYLOAD_MAX + 1];
    char *after;
    double converted;
    memcpy(buffer, text + number.start, number.length);
    buffer[number.length] = '\0';
    converted = strtod(buffer, &after);
    if (after != buffer + number.length || !isfinite(converted)) {
        return false;
    }
    *value = converted;
    return true;
}

static bool read_number(cursor *at, double *value)
{
    span number;
    return scan_number(at, &number) && convert_number(at->text, number, value);
}

static bool read_timestamp(cursor *at, int64_t *timestamp_ms)
{
    int64_t sum = 0;
    size_t count = 0;
    while (!at_end(at) && is_digit(current(at))) {
        if (count == TIMESTAMP_DIGITS_MAX) {
            return false;
        }
        sum = sum * 10 + (current(at) - '0');
        at->position++;
        count++;
    }
    if (count == 0) {
        return false;
    }
    *timestamp_ms = sum;
    return true;
}

/* An escape is refused rather than decoded: no key or text these sources send holds one. */
static bool scan_string(cursor *at, span *content)
{
    if (!take(at, '"')) {
        return false;
    }
    content->start = at->position;
    while (!at_end(at) && current(at) != '"') {
        if (current(at) == '\\') {
            return false;
        }
        at->position++;
    }
    content->length = at->position - content->start;
    return take(at, '"');
}

static bool take_word(cursor *at, const char *word)
{
    size_t length = strlen(word);
    if (at->end - at->position < length || memcmp(at->text + at->position, word, length) != 0) {
        return false;
    }
    at->position += length;
    return true;
}

/* A member the adapters do not read; anything nested is refused, as no source sends it. */
static bool skip_value(cursor *at)
{
    span skipped;
    if (at_end(at)) {
        return false;
    }
    if (current(at) == '"') {
        return scan_string(at, &skipped);
    }
    if (current(at) == '-' || is_digit(current(at))) {
        return scan_number(at, &skipped);
    }
    return take_word(at, "true") || take_word(at, "false") || take_word(at, "null");
}

static bool key_is(const cursor *at, span key, const char *name)
{
    return key.length == strlen(name) && memcmp(at->text + key.start, name, key.length) == 0;
}

static bool key_seen(const cursor *at, const span *keys, size_t count, span key)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (keys[i].length == key.length
            && memcmp(at->text + keys[i].start, at->text + key.start, key.length) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    bool has_value;
    ctl_payload payload;
} object_reading;

static bool read_member_value(cursor *at, span key, object_reading *reading)
{
    if (key_is(at, key, "v") || key_is(at, key, "value")) {
        if (reading->has_value) {
            return false;
        }
        reading->has_value = true;
        return read_number(at, &reading->payload.value);
    }
    if (key_is(at, key, "ts")) {
        reading->payload.has_timestamp = true;
        return read_timestamp(at, &reading->payload.timestamp_ms);
    }
    return skip_value(at);
}

static bool read_member(cursor *at, span *keys, size_t count, object_reading *reading)
{
    span key;
    skip_spaces(at);
    if (!scan_string(at, &key) || key_seen(at, keys, count, key)) {
        return false;
    }
    keys[count] = key;
    skip_spaces(at);
    if (!take(at, ':')) {
        return false;
    }
    skip_spaces(at);
    return read_member_value(at, key, reading);
}

static bool read_object(cursor *at, ctl_payload *payload)
{
    span keys[MEMBERS_MAX];
    size_t count = 0;
    object_reading reading = {false, {0.0, false, 0}};
    if (!take(at, '{')) {
        return false;
    }
    do {
        if (count == MEMBERS_MAX || !read_member(at, keys, count, &reading)) {
            return false;
        }
        count++;
        skip_spaces(at);
    } while (take(at, ','));
    if (!take(at, '}') || !reading.has_value) {
        return false;
    }
    *payload = reading.payload;
    return true;
}

static bool read_bare_number(cursor *at, ctl_payload *payload)
{
    double value;
    if (!read_number(at, &value)) {
        return false;
    }
    payload->value = value;
    payload->has_timestamp = false;
    payload->timestamp_ms = 0;
    return true;
}

bool ctl_payload_parse(const char *text, size_t length, ctl_payload *payload)
{
    cursor at;
    ctl_payload parsed;
    bool read;
    if (length > CTL_PAYLOAD_MAX) {
        return false;
    }
    at.text = text;
    at.position = 0;
    at.end = length;
    skip_spaces(&at);
    if (!at_end(&at) && current(&at) == '{') {
        read = read_object(&at, &parsed);
    } else {
        read = read_bare_number(&at, &parsed);
    }
    skip_spaces(&at);
    if (!read || !at_end(&at)) {
        return false;
    }
    *payload = parsed;
    return true;
}
