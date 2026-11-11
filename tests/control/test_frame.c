#include "check.h"

#include "wire/ctl_frame.h"
#include "wire/ctl_wire.h"

#include <stdint.h>
#include <string.h>

#define LINE_SIZE (CTL_WIRE_LINE_MAX + CTL_FRAME_SUFFIX + 2)

static void test_checksum(void)
{
    static const char text[] = "A 3ff0000000000000";
    uint8_t expected = 0;
    size_t i;
    for (i = 0; i < strlen(text); i++) {
        expected = (uint8_t)(expected ^ (uint8_t)text[i]);
    }
    CHECK_UINT(ctl_frame_checksum("", 0), 0);
    CHECK_UINT(ctl_frame_checksum("ok", 2), 0x04);
    CHECK_UINT(ctl_frame_checksum(text, strlen(text)), expected);
}

static void test_add(void)
{
    static const struct {
        const char *text;
        size_t size; /* 0: a buffer of LINE_SIZE */
        bool added;
        const char *result;
    } rows[] = {
        {"ok", 0, true, "ok *04"},
        {"", 0, true, " *00"},
        {"ok", 6, false, "ok"},
        {"ok", 7, true, "ok *04"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char line[LINE_SIZE];
        size_t size = rows[i].size == 0 ? sizeof line : rows[i].size;
        strcpy(line, rows[i].text);
        CHECK_TRUE(ctl_frame_add(line, size) == rows[i].added);
        CHECK_TEXT(line, rows[i].result);
    }
}

static void test_strip(void)
{
    static const struct {
        const char *line;
        bool stripped;
        const char *result;
    } rows[] = {
        {"ok *04", true, "ok"},     {"ok *04\r", true, "ok"},     {" *00", true, ""},
        {"ok *05", false, "ok *05"}, {"ok *0", false, "ok *0"},   {"ok *04x", false, "ok *04x"},
        {"ok*04", false, "ok*04"},   {"ok *G4", false, "ok *G4"}, {"ok", false, "ok"},
        {"", false, ""},             {" *", false, " *"},         {"ok *0A", false, "ok *0A"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char line[LINE_SIZE];
        strcpy(line, rows[i].line);
        CHECK_TRUE(ctl_frame_strip(line) == rows[i].stripped);
        CHECK_TEXT(line, rows[i].result);
    }
}

/* One request of each kind the protocol knows, and the longest line it takes. */
static void test_requests_round_trip(void)
{
    static char longest[CTL_WIRE_LINE_MAX + 1];
    static const char *rows[] = {
        "R",
        "C hysteresis_wh 408f400000000000",
        "C max_age_s.3 4059000000000000",
        "C trim_limit_enabled 1",
        "B",
        "O 0000000000000000 0 4035000000000000",
        "A 4024000000000000",
        "M 4024000000000000 7",
        "E 4024000000000000",
        "S",
        longest,
    };
    size_t i;
    memset(longest, 'A', CTL_WIRE_LINE_MAX);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char line[LINE_SIZE];
        strcpy(line, rows[i]);
        CHECK_TRUE(ctl_frame_add(line, sizeof line));
        CHECK_TRUE(ctl_frame_strip(line));
        CHECK_TEXT(line, rows[i]);
    }
}

int main(void)
{
    test_checksum();
    test_add();
    test_strip();
    test_requests_round_trip();
    return check_result();
}
