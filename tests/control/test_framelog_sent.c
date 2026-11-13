#include "check.h"

#include "glue/ctl_framelog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STAMP "Fri Nov 13 09:22:04 2026 (125253578): "
#define LINE_SIZE 192
#define BYTES_PER_LINE 32
#define PCB_LENGTH 20
#define QUERY_LENGTH 111

/* The optional PCB frame as the board sends it; the checksum, the last byte, is not dumped. */
static const uint8_t PCB_FRAME[PCB_LENGTH] = {0xF1, 0x11, 0x01, 0x50, 0x00, 0x00, 0x40,
                                              0xFF, 0xFF, 0xE5, 0xFF, 0xFF, 0x00, 0xFF,
                                              0x00, 0xFF, 0xFF, 0x00, 0x00, 0x8F};

/* The answer the pump gives, received whole. */
static const uint8_t ANSWER[PCB_LENGTH] = {0x71, 0x11, 0x01, 0x50, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x2D};

static ctl_framelog_result feed(ctl_framelog *parser, const char *text)
{
    char line[LINE_SIZE];
    snprintf(line, sizeof line, "%s%s", STAMP, text);
    return ctl_framelog_line(parser, line, strlen(line));
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
    case CTL_FRAMELOG_SENT:
        return 'S';
    }
    return '?';
}

/* Feeds the data lines of length bytes, BYTES_PER_LINE each, as upstream writes them, and writes
 * one letter per line into letters: N for NOTHING, F for FRAME, D for DROPPED, S for SENT. */
static void feed_data(ctl_framelog *parser, const uint8_t *bytes, size_t length, char *letters)
{
    size_t start;
    size_t count = strlen(letters);
    for (start = 0; start < length; start += BYTES_PER_LINE) {
        char text[LINE_SIZE] = "data: ";
        size_t used = strlen(text);
        size_t i;
        for (i = start; i < length && i < start + BYTES_PER_LINE; i++) {
            used += (size_t)snprintf(text + used, sizeof text - used, "%02X ", bytes[i]);
        }
        letters[count++] = result_letter(feed(parser, text));
    }
    letters[count] = '\0';
}

static void check_frame(int line, const ctl_framelog *parser, const uint8_t *expected,
                        size_t length)
{
    check_uint(__FILE__, line, (uint32_t)parser->length, (uint32_t)length);
    check_true(__FILE__, line,
               parser->length == length && memcmp(parser->frame, expected, length) == 0,
               "the frame equals the bytes expected");
}

static void test_sent_then_answer(void)
{
    ctl_framelog parser;
    char letters[8] = "";
    ctl_framelog_init(&parser);
    CHECK_UINT((uint32_t)feed(&parser, "sent bytes: 20 including checksum value: 143 "),
               (uint32_t)CTL_FRAMELOG_NOTHING);
    feed_data(&parser, PCB_FRAME, PCB_LENGTH - 1, letters);
    CHECK_TEXT(letters, "S");
    check_frame(__LINE__, &parser, PCB_FRAME, PCB_LENGTH);
    CHECK_TRUE(ctl_framelog_is_optional_pcb(parser.frame, parser.length));
    CHECK_UINT((uint32_t)feed(&parser, "Received 20 bytes data"), (uint32_t)CTL_FRAMELOG_NOTHING);
    letters[0] = '\0';
    feed_data(&parser, ANSWER, PCB_LENGTH, letters);
    CHECK_TEXT(letters, "F");
    check_frame(__LINE__, &parser, ANSWER, PCB_LENGTH);
    CHECK_TRUE(!ctl_framelog_is_optional_pcb(parser.frame, parser.length));
}

static void test_bad_checksum(void)
{
    ctl_framelog parser;
    char letters[8] = "";
    ctl_framelog_init(&parser);
    CHECK_UINT((uint32_t)feed(&parser, "sent bytes: 20 including checksum value: 144 "),
               (uint32_t)CTL_FRAMELOG_NOTHING);
    feed_data(&parser, PCB_FRAME, PCB_LENGTH - 1, letters);
    CHECK_TEXT(letters, "D");
}

static void test_query(void)
{
    uint8_t query[QUERY_LENGTH] = {0x71, 0x6C, 0x01, 0x10};
    ctl_framelog parser;
    char letters[8] = "";
    query[QUERY_LENGTH - 1] = 0x12; /* 0x71 + 0x6C + 0x01 + 0x10 + 0x12 is 0 modulo 256 */
    ctl_framelog_init(&parser);
    CHECK_UINT((uint32_t)feed(&parser, "sent bytes: 111 including checksum value: 18 "),
               (uint32_t)CTL_FRAMELOG_NOTHING);
    feed_data(&parser, query, QUERY_LENGTH - 1, letters);
    CHECK_TEXT(letters, "NNNS");
    check_frame(__LINE__, &parser, query, QUERY_LENGTH);
    CHECK_TRUE(!ctl_framelog_is_optional_pcb(parser.frame, parser.length));
}

/* A sent frame breaks as a received one does; a header that does not parse starts nothing. */
static void test_breaks(void)
{
    static const struct {
        int line;
        const char *header;
        const char *between; /* a line before the data, or NULL */
        const char *expected_between;
        const char *expected;
    } rows[] = {
        {__LINE__, "sent bytes: 20 including checksum value: 143", NULL, "", "S"},
        {__LINE__, "sent bytes: 20 including checksum value: 143 ", "Received 20 bytes data", "D",
         "N"},
        {__LINE__, "sent bytes: 20 including checksum value: 143 ",
         "sent bytes: 20 including checksum value: 143 ", "D", "S"},
        {__LINE__, "sent bytes: 20 including checksum value: 143 ",
         "optional PCB datagram sent bytes: 20", "D", "N"},
        {__LINE__, "sent bytes: 19 including checksum value: 143 ", NULL, "", "D"},
        {__LINE__, "sent bytes: 20 including checksum value: 399 ", NULL, "", "N"},
        {__LINE__, "sent bytes: 1 including checksum value: 0 ", NULL, "", "N"},
        {__LINE__, "sent bytes: 257 including checksum value: 143 ", NULL, "", "N"},
        {__LINE__, "sent bytes: 20 including checksum value: ", NULL, "", "N"},
        {__LINE__, "sent bytes: 20 including checksum value: 143 x", NULL, "", "N"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_framelog parser;
        char between[2] = "";
        char letters[8] = "";
        ctl_framelog_init(&parser);
        check_uint(__FILE__, rows[i].line, (uint32_t)feed(&parser, rows[i].header),
                   (uint32_t)CTL_FRAMELOG_NOTHING);
        if (rows[i].between != NULL) {
            between[0] = result_letter(feed(&parser, rows[i].between));
        }
        check_text(__FILE__, rows[i].line, between, rows[i].expected_between);
        feed_data(&parser, PCB_FRAME, PCB_LENGTH - 1, letters);
        check_text(__FILE__, rows[i].line, letters, rows[i].expected);
    }
}

static void test_is_optional_pcb(void)
{
    uint8_t frame[PCB_LENGTH];
    memcpy(frame, PCB_FRAME, sizeof frame);
    CHECK_TRUE(ctl_framelog_is_optional_pcb(frame, PCB_LENGTH));
    CHECK_TRUE(!ctl_framelog_is_optional_pcb(frame, PCB_LENGTH - 1));
    CHECK_TRUE(!ctl_framelog_is_optional_pcb(NULL, PCB_LENGTH));
    frame[3] = 0x10;
    CHECK_TRUE(!ctl_framelog_is_optional_pcb(frame, PCB_LENGTH));
    frame[3] = 0x50;
    frame[0] = 0x71;
    CHECK_TRUE(!ctl_framelog_is_optional_pcb(frame, PCB_LENGTH));
}

int main(void)
{
    test_sent_then_answer();
    test_bad_checksum();
    test_query();
    test_breaks();
    test_is_optional_pcb();
    return check_result();
}
