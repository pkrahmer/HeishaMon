#include "check.h"

#include "glue/ctl_framelog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_PATH "tests/control/data/framelog_sample.txt"
#define STAMP "Wed Nov 11 22:25:49 2026 (594966970): "
#define LINE_SIZE 192
#define SEQUENCE_MAX 16
#define BYTES_PER_LINE 32
#define NO_SKIP SEQUENCE_MAX
#define MAIN_LENGTH 203
#define ANSWER_LENGTH 20

/* The received main data frame the sample holds. */
static const uint8_t MAIN_FRAME[MAIN_LENGTH] = {
    0x71, 0xC8, 0x01, 0x10, 0x9F, 0xC4, 0xE9, 0x0E, 0x33, 0x58, 0x7D, 0xA2, 0xC7,
    0xEC, 0x11, 0x36, 0x5B, 0x80, 0xA5, 0xCA, 0xEF, 0x14, 0x39, 0x5E, 0x83, 0xA8,
    0xCD, 0xF2, 0x17, 0x3C, 0x61, 0x86, 0xAB, 0xD0, 0xF5, 0x1A, 0x3F, 0x64, 0x89,
    0xAE, 0xD3, 0xF8, 0x1D, 0x42, 0x67, 0x8C, 0xB1, 0xD6, 0xFB, 0x20, 0x45, 0x6A,
    0x8F, 0xB4, 0xD9, 0xFE, 0x23, 0x48, 0x6D, 0x92, 0xB7, 0xDC, 0x01, 0x26, 0x4B,
    0x70, 0x95, 0xBA, 0xDF, 0x04, 0x29, 0x4E, 0x73, 0x98, 0xBD, 0xE2, 0x07, 0x2C,
    0x51, 0x76, 0x9B, 0xC0, 0xE5, 0x0A, 0x2F, 0x54, 0x79, 0x9E, 0xC3, 0xE8, 0x0D,
    0x32, 0x57, 0x7C, 0xA1, 0xC6, 0xEB, 0x10, 0x35, 0x5A, 0x7F, 0xA4, 0xC9, 0xEE,
    0x13, 0x38, 0x5D, 0x82, 0xA7, 0xCC, 0xF1, 0x16, 0x3B, 0x60, 0x85, 0xAA, 0xCF,
    0xF4, 0x19, 0x3E, 0x63, 0x88, 0xAD, 0xD2, 0xF7, 0x1C, 0x41, 0x66, 0x8B, 0xB0,
    0xD5, 0xFA, 0x1F, 0x44, 0x69, 0x8E, 0xB3, 0xD8, 0xFD, 0x22, 0x47, 0x6C, 0x91,
    0xB6, 0xDB, 0x00, 0x25, 0x4A, 0x6F, 0x94, 0xB9, 0xDE, 0x03, 0x28, 0x4D, 0x72,
    0x97, 0xBC, 0xE1, 0x06, 0x2B, 0x50, 0x75, 0x9A, 0xBF, 0xE4, 0x09, 0x2E, 0x53,
    0x78, 0x9D, 0xC2, 0xE7, 0x0C, 0x31, 0x56, 0x7B, 0xA0, 0xC5, 0xEA, 0x0F, 0x34,
    0x59, 0x7E, 0xA3, 0xC8, 0xED, 0x12, 0x37, 0x5C, 0x81, 0xA6, 0xCB, 0xF0, 0x15,
    0x3A, 0x5F, 0x84, 0xA9, 0xCE, 0xF3, 0x18, 0xF1,
};

/* The received answer the sample holds after the sent frame. */
static const uint8_t ANSWER[ANSWER_LENGTH] = {0x71, 0x11, 0x01, 0x50, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0x00, 0x2D};

/* The lines of a log as the board publishes them, one message each. */
typedef struct {
    size_t count;
    char line[SEQUENCE_MAX][LINE_SIZE];
} sequence;

static void add_raw(sequence *log, const char *text)
{
    snprintf(log->line[log->count++], LINE_SIZE, "%s", text);
}

static void add(sequence *log, const char *text)
{
    snprintf(log->line[log->count++], LINE_SIZE, "%s%s", STAMP, text);
}

/* The data lines of a hex dump, BYTES_PER_LINE bytes each, as upstream writes them; the line at
 * index skipped is left out. */
static void add_data(sequence *log, const uint8_t *bytes, size_t length, size_t skipped)
{
    size_t start;
    for (start = 0; start < length; start += BYTES_PER_LINE) {
        char text[LINE_SIZE] = "data: ";
        size_t used = strlen(text);
        size_t i;
        for (i = start; i < length && i < start + BYTES_PER_LINE; i++) {
            used += (size_t)snprintf(text + used, sizeof text - used, "%02X ", bytes[i]);
        }
        if (start / BYTES_PER_LINE != skipped) {
            add(log, text);
        }
    }
}

static char result_letter(ctl_framelog_result result)
{
    switch (result) {
    case CTL_FRAMELOG_NOTHING:
        return 'N';
    case CTL_FRAMELOG_FRAME:
        return 'F';
    case CTL_FRAMELOG_DROPPED:
        return 'D';
    }
    return '?';
}

/* Feeds every line and writes one letter per line: N for NOTHING, F for FRAME, D for DROPPED. */
static void feed(ctl_framelog *parser, const sequence *log, char *letters)
{
    size_t i;
    for (i = 0; i < log->count; i++) {
        letters[i] = result_letter(ctl_framelog_line(parser, log->line[i], strlen(log->line[i])));
    }
    letters[log->count] = '\0';
}

static void check_frame(const char *file, int line, const ctl_framelog *parser,
                        const uint8_t *expected, size_t length)
{
    check_uint(file, line, (uint32_t)parser->length, (uint32_t)length);
    check_true(file, line, parser->length == length && memcmp(parser->frame, expected, length) == 0,
               "the frame equals the bytes written");
}

/* ---- The sample ---------------------------------------------------------------------------- */

static bool read_sample(sequence *log)
{
    char text[LINE_SIZE];
    FILE *file = fopen(SAMPLE_PATH, "r");
    if (file == NULL) {
        return false;
    }
    log->count = 0;
    while (log->count < SEQUENCE_MAX && fgets(text, sizeof text, file) != NULL) {
        text[strcspn(text, "\r\n")] = '\0';
        add_raw(log, text);
    }
    fclose(file);
    return true;
}

static void test_sample(void)
{
    /* Header and six data lines, the seventh, the checksum line; the sent frame's two lines; the
     * answer's header, its data line and its checksum line. */
    static const char expected[] = "NNNNNNNFNNNNFN";
    sequence log;
    ctl_framelog parser;
    char letters[SEQUENCE_MAX + 1];
    size_t i;
    CHECK_TRUE(read_sample(&log));
    CHECK_UINT((uint32_t)log.count, (uint32_t)strlen(expected));
    ctl_framelog_init(&parser);
    for (i = 0; i < log.count; i++) {
        const char *text = log.line[i];
        letters[i] = result_letter(ctl_framelog_line(&parser, text, strlen(text)));
        if (i == 7) {
            check_frame(__FILE__, __LINE__, &parser, MAIN_FRAME, MAIN_LENGTH);
        } else if (i == 12) {
            check_frame(__FILE__, __LINE__, &parser, ANSWER, ANSWER_LENGTH);
        }
    }
    letters[log.count] = '\0';
    CHECK_TEXT(letters, expected);
}

/* A line that is not terminated is read no further than the length it is given. */
static void test_length_bounds_the_line(void)
{
    static const char text[] = STAMP "Received 20 bytes data, and more";
    ctl_framelog parser;
    ctl_framelog_init(&parser);
    CHECK_UINT((uint32_t)ctl_framelog_line(&parser, text, strlen(STAMP "Received 20 bytes data")),
               (uint32_t)CTL_FRAMELOG_NOTHING);
    CHECK_TRUE(parser.collecting);
    CHECK_UINT((uint32_t)parser.expected, 20u);
    CHECK_UINT((uint32_t)ctl_framelog_line(&parser, NULL, 0), (uint32_t)CTL_FRAMELOG_NOTHING);
}

/* ---- The refusals -------------------------------------------------------------------------- */

static void changed_byte(sequence *log)
{
    uint8_t frame[MAIN_LENGTH];
    memcpy(frame, MAIN_FRAME, sizeof frame);
    frame[100] ^= 0x01;
    add(log, "Received 203 bytes data");
    add_data(log, frame, sizeof frame, NO_SKIP);
}

static void missing_data_line(sequence *log)
{
    add(log, "Received 203 bytes data");
    add_data(log, MAIN_FRAME, MAIN_LENGTH, 3);
    add(log, "Received 20 bytes data");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void sent_frame(sequence *log)
{
    static const uint8_t query[] = {0xF1, 0x11, 0x01, 0x50, 0x00, 0x00, 0x40, 0xFF, 0xFF, 0xE5,
                                    0xFF, 0xFF, 0x00, 0xFF, 0xEB, 0xFF, 0xFF, 0x00, 0x00};
    add(log, "sent bytes: 20 including checksum value: 164 ");
    add_data(log, query, sizeof query, NO_SKIP);
    add(log, "Received 20 bytes data");
    add(log, "sent bytes: 20 including checksum value: 164 ");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void named_sent_frame(sequence *log)
{
    add(log, "Received 20 bytes data");
    add(log, "heatpump request query sent bytes: 111");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void malformed_byte(sequence *log)
{
    add(log, "Received 20 bytes data");
    add(log, "data: 71 11 01 50 7G 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2D ");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void missing_space(sequence *log)
{
    add(log, "Received 20 bytes data");
    add(log, "data: 71 11 01 5000 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2D ");
}

static void received_one(sequence *log)
{
    add(log, "Received 1 bytes data");
    add(log, "data: 00 ");
    add(log, "Received 1 bytes data");
    add(log, "sent bytes: 20 including checksum value: 164 ");
}

static void received_zero(sequence *log)
{
    add(log, "Received 0 bytes data");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void received_too_many(sequence *log)
{
    add(log, "Received 300 bytes data");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void received_too_many_while_collecting(sequence *log)
{
    add(log, "Received 20 bytes data");
    add(log, "Received 300 bytes data");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void received_largest(sequence *log)
{
    add(log, "Received 256 bytes data");
    add(log, "sent bytes: 20 including checksum value: 164 ");
    add(log, "Received 257 bytes data");
    add(log, "sent bytes: 20 including checksum value: 164 ");
}

static void no_stamp(sequence *log)
{
    add_raw(log, "Received 20 bytes data");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
    add(log, "Received 20 bytes data");
    add_raw(log, "data: 71 11 01 50 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2D ");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void more_than_expected(sequence *log)
{
    add(log, "Received 19 bytes data");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void lower_case_and_no_trailing_space(sequence *log)
{
    add(log, "Received 20 bytes data");
    add(log, "data: 71 11 01 50 00 00 00 00 00 00");
    add(log, "data: 00 00 00 00 00 00 00 00 00 2d");
}

static void other_lines_between(sequence *log)
{
    add(log, "Received 20 bytes data");
    add(log, "Checksum received false!");
    add(log, "Received 20 bytesdata");
    add(log, "received 20 bytes data");
    add(log, "data:71 11");
    add_data(log, ANSWER, ANSWER_LENGTH, NO_SKIP);
}

static void test_refusals(void)
{
    static const struct {
        const char *name;
        void (*build)(sequence *log);
        const char *expected;
    } rows[] = {
        {"one byte changed", changed_byte, "NNNNNNND"},
        {"a data line missing", missing_data_line, "NNNNNNNDF"},
        {"a sent frame", sent_frame, "NNNDN"},
        {"a sent frame named before its mark", named_sent_frame, "NDN"},
        {"a malformed byte", malformed_byte, "NDN"},
        {"a byte without its space", missing_space, "ND"},
        {"Received 1", received_one, "NFND"},
        {"Received 0", received_zero, "NN"},
        {"Received 300", received_too_many, "NN"},
        {"Received 300 while collecting", received_too_many_while_collecting, "NDN"},
        {"Received 256 and 257", received_largest, "NDNN"},
        {"no time stamp", no_stamp, "NNNNF"},
        {"more bytes than expected", more_than_expected, "ND"},
        {"lower case, no trailing space", lower_case_and_no_trailing_space, "NNF"},
        {"other lines between", other_lines_between, "NNNNNF"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        sequence log;
        ctl_framelog parser;
        char letters[SEQUENCE_MAX + 1];
        int failures_before = check_failures();
        log.count = 0;
        rows[i].build(&log);
        ctl_framelog_init(&parser);
        feed(&parser, &log, letters);
        CHECK_TEXT(letters, rows[i].expected);
        if (check_failures() != failures_before) {
            printf("%s:%d: the row above is \"%s\"\n", __FILE__, __LINE__, rows[i].name);
        }
    }
}

/* ---- The checksum -------------------------------------------------------------------------- */

static void test_checksum(void)
{
    uint8_t changed[ANSWER_LENGTH];
    memcpy(changed, ANSWER, sizeof changed);
    changed[ANSWER_LENGTH - 1] = 0x2E;
    CHECK_TRUE(ctl_framelog_checksum_ok(ANSWER, ANSWER_LENGTH));
    CHECK_TRUE(!ctl_framelog_checksum_ok(changed, ANSWER_LENGTH));
    CHECK_TRUE(ctl_framelog_checksum_ok(MAIN_FRAME, MAIN_LENGTH));
}

/* ---- The main data frame ------------------------------------------------------------------ */

static void test_main_data(void)
{
    static const struct {
        const char *name;
        size_t length;
        size_t changed_index;
        uint8_t changed_value;
        bool expected;
    } rows[] = {
        {"the sample's main frame", MAIN_LENGTH, 0, 0x71, true},
        {"one byte short", MAIN_LENGTH - 1, 0, 0x71, false},
        {"another header", MAIN_LENGTH, 0, 0xF1, false},
        {"another type", MAIN_LENGTH, 3, 0x21, false},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        uint8_t frame[MAIN_LENGTH];
        int failures_before = check_failures();
        memcpy(frame, MAIN_FRAME, sizeof frame);
        frame[rows[i].changed_index] = rows[i].changed_value;
        CHECK_TRUE(ctl_framelog_is_main_data(frame, rows[i].length) == rows[i].expected);
        if (check_failures() != failures_before) {
            printf("%s:%d: the row above is \"%s\"\n", __FILE__, __LINE__, rows[i].name);
        }
    }
    CHECK_TRUE(!ctl_framelog_is_main_data(ANSWER, ANSWER_LENGTH));
    CHECK_TRUE(!ctl_framelog_is_main_data(NULL, MAIN_LENGTH));
}

int main(void)
{
    test_sample();
    test_length_bounds_the_line();
    test_refusals();
    test_checksum();
    test_main_data();
    return check_result();
}
