#include "check.h"

#include "adapt/ctl_payload.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A refusal must leave every field as it was; these values no row can produce. */
static const ctl_payload MARKER = {-12345.5, true, 777};

static bool payload_is_marker(const ctl_payload *payload)
{
    return payload->value == MARKER.value && payload->has_timestamp == MARKER.has_timestamp
           && payload->timestamp_ms == MARKER.timestamp_ms;
}

typedef struct {
    const char *text;
    double value;
    bool has_timestamp;
    int64_t timestamp_ms;
} accepted_row;

static void check_accepted(const char *file, int line, const accepted_row *row)
{
    ctl_payload payload = MARKER;
    bool parsed = ctl_payload_parse(row->text, strlen(row->text), &payload);
    check_true(file, line, parsed, row->text);
    if (!parsed) {
        return;
    }
    check_double(file, line, payload.value, row->value);
    check_true(file, line, payload.has_timestamp == row->has_timestamp, row->text);
    if (row->has_timestamp) {
        check_true(file, line, payload.timestamp_ms == row->timestamp_ms, row->text);
    }
}

static void check_refused(const char *file, int line, const char *text)
{
    ctl_payload payload = MARKER;
    check_true(file, line, !ctl_payload_parse(text, strlen(text), &payload), text);
    check_true(file, line, payload_is_marker(&payload), text);
}

static void test_bare_numbers_accepted(void)
{
    static const accepted_row rows[] = {
        {"5", 5.0, false, 0},      {"-3.5", -3.5, false, 0},  {"3200", 3200.0, false, 0},
        {"21.10", 21.1, false, 0}, {"1e3", 1000.0, false, 0}, {"2.5E-1", 0.25, false, 0},
        {" 22.7\n", 22.7, false, 0}, {"0", 0.0, false, 0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_accepted(__FILE__, __LINE__, &rows[i]);
    }
}

static void test_bare_numbers_refused(void)
{
    static const char *const rows[] = {
        "",    " ",   "+5",  ".5",   "5.",  "1e",  "1e+", "--1",
        "0x10", "nan", "inf", "1e999", "5 6", "5,6", "abc", "\"5\"",
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_refused(__FILE__, __LINE__, rows[i]);
    }
}

static void test_objects_accepted(void)
{
    static const accepted_row rows[] = {
        {"{\"ts\":1794430802765,\"v\":21.1,\"s\":0}", 21.1, true, INT64_C(1794430802765)},
        {"{\"v\":22}", 22.0, false, 0},
        {"{\"value\":-4.5}", -4.5, false, 0},
        {"{ \"v\" : 1.5 , \"unit\" : \"C\", \"ok\": true, \"x\": null, \"y\": false }", 1.5, false, 0},
        {"{\"s\":0,\"v\":0,\"ts\":0}", 0.0, true, 0},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_accepted(__FILE__, __LINE__, &rows[i]);
    }
}

static void test_objects_refused(void)
{
    static const char *const rows[] = {
        /* structure */
        "{}",
        "{\"ts\":5}",
        "{\"v\":1,\"value\":2}",
        "{\"v\":1,\"v\":2}",
        "{\"v\":\"21.1\"}",
        "{\"v\":1,\"n\":{\"a\":1}}",
        "{\"v\":1,\"n\":[1]}",
        "{\"v\":1} x",
        "{\"v\":1",
        "{v:1}",
        "{\"v\":1,}",
        /* escapes */
        "{\"v\\\"\":1}",
        "{\"v\":1,\"u\":\"a\\\"b\"}",
        /* timestamps */
        "{\"v\":1,\"ts\":-5}",
        "{\"v\":1,\"ts\":1.5}",
        "{\"v\":1,\"ts\":1234567890123456789}",
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_refused(__FILE__, __LINE__, rows[i]);
    }
}

static void test_length_limit(void)
{
    char text[CTL_PAYLOAD_MAX + 1];
    ctl_payload payload = MARKER;

    memset(text, ' ', sizeof text);
    memcpy(text, "22.7", 4);
    CHECK_TRUE(ctl_payload_parse(text, CTL_PAYLOAD_MAX, &payload));
    CHECK_DOUBLE(payload.value, 22.7);
    CHECK_TRUE(!payload.has_timestamp);

    payload = MARKER;
    CHECK_TRUE(!ctl_payload_parse(text, CTL_PAYLOAD_MAX + 1, &payload));
    CHECK_TRUE(payload_is_marker(&payload));

    payload = MARKER;
    CHECK_TRUE(ctl_payload_parse("22.7xyz", 4, &payload));
    CHECK_DOUBLE(payload.value, 22.7);
    CHECK_TRUE(!payload.has_timestamp);
}

int main(void)
{
    test_bare_numbers_accepted();
    test_bare_numbers_refused();
    test_objects_accepted();
    test_objects_refused();
    test_length_limit();
    return check_result();
}
