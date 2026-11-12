#include "ctl_framelog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STAMP_END "): "
#define RECEIVED_PREFIX "Received "
#define RECEIVED_SUFFIX " bytes data"
#define DATA_PREFIX "data: "
#define SENT_MARK "sent bytes: "
/* A main data frame starts with this byte and has this type in byte 3, as upstream checks. */
#define MAIN_HEADER 0x71u
#define MAIN_TYPE_BYTE 3u
#define MAIN_TYPE 0x10u
/* More digits than this cannot be a count up to CTL_FRAMELOG_MAX, and keep the sum from
 * overflowing. */
#define COUNT_DIGITS_MAX 4

/* A piece of a line that need not end with a zero. */
typedef struct {
    const char *text;
    size_t length;
} span;

/* Returns the index of the first occurrence of mark in line, or line.length when there is none. */
static size_t find(span line, const char *mark)
{
    const size_t mark_length = strlen(mark);
    size_t at;
    for (at = 0; at + mark_length <= line.length; at++) {
        if (memcmp(line.text + at, mark, mark_length) == 0) {
            return at;
        }
    }
    return line.length;
}

static bool starts_with(span line, const char *prefix)
{
    const size_t prefix_length = strlen(prefix);
    return line.length >= prefix_length && memcmp(line.text, prefix, prefix_length) == 0;
}

static span after(span line, size_t count)
{
    span rest;
    rest.text = line.text + count;
    rest.length = line.length - count;
    return rest;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* Reads "Received <n> bytes data" exactly; any other text is not such a line. */
static bool read_received(span message, size_t *count)
{
    size_t digits = 0;
    size_t value = 0;
    if (!starts_with(message, RECEIVED_PREFIX)) {
        return false;
    }
    message = after(message, strlen(RECEIVED_PREFIX));
    while (digits < message.length && message.text[digits] >= '0' && message.text[digits] <= '9') {
        if (digits < COUNT_DIGITS_MAX) {
            value = value * 10u + (size_t)(message.text[digits] - '0');
        } else {
            value = CTL_FRAMELOG_MAX + 1u;
        }
        digits++;
    }
    message = after(message, digits);
    if (digits == 0 || message.length != strlen(RECEIVED_SUFFIX) ||
        memcmp(message.text, RECEIVED_SUFFIX, message.length) != 0) {
        return false;
    }
    *count = value;
    return true;
}

/* Ends the frame in progress, if there is one: DROPPED when one was cut short. */
static ctl_framelog_result end_frame(ctl_framelog *log)
{
    const bool was_collecting = log->collecting;
    log->collecting = false;
    return was_collecting ? CTL_FRAMELOG_DROPPED : CTL_FRAMELOG_NOTHING;
}

static ctl_framelog_result start_frame(ctl_framelog *log, size_t count)
{
    const ctl_framelog_result ended = end_frame(log);
    if (count >= 1u && count <= CTL_FRAMELOG_MAX) {
        log->collecting = true;
        log->expected = count;
        log->length = 0;
    }
    return ended;
}

/* Appends the bytes of a data line: each two hex digits followed by a space or the line's end. */
static ctl_framelog_result append_bytes(ctl_framelog *log, span bytes)
{
    size_t at = 0;
    while (at < bytes.length) {
        int high;
        int low;
        if (bytes.length - at < 2u || log->length >= log->expected) {
            return end_frame(log);
        }
        high = hex_digit(bytes.text[at]);
        low = hex_digit(bytes.text[at + 1u]);
        if (high < 0 || low < 0 || (at + 2u < bytes.length && bytes.text[at + 2u] != ' ')) {
            return end_frame(log);
        }
        log->frame[log->length++] = (uint8_t)(high * 16 + low);
        at += 3u;
    }
    if (log->length < log->expected) {
        return CTL_FRAMELOG_NOTHING;
    }
    log->collecting = false;
    return ctl_framelog_checksum_ok(log->frame, log->length) ? CTL_FRAMELOG_FRAME
                                                              : CTL_FRAMELOG_DROPPED;
}

void ctl_framelog_init(ctl_framelog *log)
{
    log->collecting = false;
    log->expected = 0;
    log->length = 0;
}

ctl_framelog_result ctl_framelog_line(ctl_framelog *log, const char *line, size_t length)
{
    span message;
    size_t stamp_end;
    size_t count = 0;
    if (line == NULL) {
        return CTL_FRAMELOG_NOTHING;
    }
    message.text = line;
    message.length = length;
    stamp_end = find(message, STAMP_END);
    if (stamp_end == length) {
        return CTL_FRAMELOG_NOTHING;
    }
    message = after(message, stamp_end + strlen(STAMP_END));
    if (read_received(message, &count)) {
        return start_frame(log, count);
    }
    if (starts_with(message, DATA_PREFIX)) {
        return log->collecting ? append_bytes(log, after(message, strlen(DATA_PREFIX)))
                               : CTL_FRAMELOG_NOTHING;
    }
    /* The board logs its own frames after this mark, and on some boards after a few words that
     * name the frame; their data lines are never the pump's. */
    if (find(message, SENT_MARK) < message.length) {
        return end_frame(log);
    }
    return CTL_FRAMELOG_NOTHING;
}

bool ctl_framelog_checksum_ok(const uint8_t *frame, size_t length)
{
    uint8_t sum = 0;
    size_t i;
    if (frame == NULL) {
        return false;
    }
    for (i = 0; i < length; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }
    return sum == 0u;
}

bool ctl_framelog_is_main_data(const uint8_t *frame, size_t length)
{
    return frame != NULL && length == CTL_FRAMELOG_MAIN_LENGTH && frame[0] == MAIN_HEADER
           && frame[MAIN_TYPE_BYTE] == MAIN_TYPE;
}
