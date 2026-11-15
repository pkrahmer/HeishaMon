#include "check.h"

#include "glue/ctl_reset_reason.h"

#include <stdint.h>
#include <string.h>

#define UNTOUCHED "untouched"

typedef struct {
    uint32_t reason;
    const char *text;
} reason_row;

static ctl_reset_info info_with_reason(uint32_t reason)
{
    ctl_reset_info info;
    memset(&info, 0, sizeof info);
    info.reason = reason;
    return info;
}

static void check_esp8266(const char *file, int line, const ctl_reset_info *info,
                          const char *expected)
{
    char text[CTL_RESET_REASON_TEXT_MAX];
    strcpy(text, UNTOUCHED);
    check_true(file, line, ctl_reset_reason_esp8266(info, text, sizeof text), expected);
    check_text(file, line, text, expected);
}

static void check_esp32(const char *file, int line, uint32_t reason, const char *expected)
{
    char text[CTL_RESET_REASON_TEXT_MAX];
    strcpy(text, UNTOUCHED);
    check_true(file, line, ctl_reset_reason_esp32(reason, text, sizeof text), expected);
    check_text(file, line, text, expected);
}

static void test_exception(void)
{
    ctl_reset_info info = info_with_reason(2);
    info.exccause = 28;
    info.epc1 = 0x4020f3a4u;
    info.excvaddr = 4;
    check_esp8266(__FILE__, __LINE__, &info,
                  "exception 28 epc1=0x4020f3a4 epc2=0x00000000 epc3=0x00000000 "
                  "excvaddr=0x00000004 depc=0x00000000");
}

static void test_every_field_has_its_place(void)
{
    ctl_reset_info info = info_with_reason(2);
    info.exccause = 3;
    info.epc1 = 0x1u;
    info.epc2 = 0x20u;
    info.epc3 = 0x300u;
    info.excvaddr = 0xabcdef01u;
    info.depc = 0x40000000u;
    check_esp8266(__FILE__, __LINE__, &info,
                  "exception 3 epc1=0x00000001 epc2=0x00000020 epc3=0x00000300 "
                  "excvaddr=0xabcdef01 depc=0x40000000");
}

static void test_plain_reasons(void)
{
    static const reason_row rows[] = {
        {0, "power on"},
        {1, "hardware watchdog"},
        {3, "software watchdog"},
        {4, "software restart"},
        {5, "deep sleep wake"},
        {6, "external reset"},
        {9, "reason 9"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_reset_info info = info_with_reason(rows[i].reason);
        info.exccause = 28;
        info.epc1 = 0x4020f3a4u;
        check_esp8266(__FILE__, __LINE__, &info, rows[i].text);
    }
}

static void test_esp32_reasons(void)
{
    static const reason_row rows[] = {
        {0, "unknown"},
        {1, "power on"},
        {2, "external reset"},
        {3, "software restart"},
        {4, "exception"},
        {5, "interrupt watchdog"},
        {6, "task watchdog"},
        {7, "other watchdog"},
        {8, "deep sleep wake"},
        {9, "brownout"},
        {10, "sdio reset"},
        {11, "usb reset"},
        {12, "jtag reset"},
        {13, "efuse error"},
        {14, "power glitch"},
        {15, "cpu lockup"},
        {16, "reason 16"},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_esp32(__FILE__, __LINE__, rows[i].reason, rows[i].text);
    }
}

/* The widest values in every field give the longest text, which must fit the named buffer. */
static void test_longest_text_fits(void)
{
    static const char longest[] = "exception 4294967295 epc1=0xffffffff epc2=0xffffffff "
                                  "epc3=0xffffffff excvaddr=0xffffffff depc=0xffffffff";
    ctl_reset_info info = info_with_reason(2);
    info.exccause = UINT32_MAX;
    info.epc1 = UINT32_MAX;
    info.epc2 = UINT32_MAX;
    info.epc3 = UINT32_MAX;
    info.excvaddr = UINT32_MAX;
    info.depc = UINT32_MAX;
    CHECK_UINT((uint32_t)sizeof longest, (uint32_t)CTL_RESET_REASON_TEXT_MAX);
    check_esp8266(__FILE__, __LINE__, &info, longest);
}

static void check_refused_esp8266(const char *file, int line, const ctl_reset_info *info,
                                  size_t size)
{
    char text[CTL_RESET_REASON_TEXT_MAX];
    strcpy(text, UNTOUCHED);
    check_true(file, line, !ctl_reset_reason_esp8266(info, text, size), "refused");
    check_text(file, line, text, UNTOUCHED);
}

static void check_refused_esp32(const char *file, int line, uint32_t reason, size_t size)
{
    char text[CTL_RESET_REASON_TEXT_MAX];
    strcpy(text, UNTOUCHED);
    check_true(file, line, !ctl_reset_reason_esp32(reason, text, size), "refused");
    check_text(file, line, text, UNTOUCHED);
}

/* A buffer one byte short of the text and its terminating zero is refused and left as it was;
 * the exact size is taken. */
static void test_small_buffer_is_refused(void)
{
    char text[CTL_RESET_REASON_TEXT_MAX];
    ctl_reset_info exception = info_with_reason(2);
    ctl_reset_info power_on = info_with_reason(0);
    const size_t exception_fits = sizeof "exception 0 epc1=0x00000000 epc2=0x00000000 "
                                         "epc3=0x00000000 excvaddr=0x00000000 depc=0x00000000";

    CHECK_TRUE(ctl_reset_reason_esp8266(&exception, text, exception_fits));
    check_refused_esp8266(__FILE__, __LINE__, &exception, exception_fits - 1);
    CHECK_TRUE(ctl_reset_reason_esp8266(&power_on, text, sizeof "power on"));
    CHECK_TEXT(text, "power on");
    check_refused_esp8266(__FILE__, __LINE__, &power_on, sizeof "power on" - 1);
    check_refused_esp8266(__FILE__, __LINE__, &power_on, 0);

    CHECK_TRUE(ctl_reset_reason_esp32(9, text, sizeof "brownout"));
    CHECK_TEXT(text, "brownout");
    check_refused_esp32(__FILE__, __LINE__, 9, sizeof "brownout" - 1);
    check_refused_esp32(__FILE__, __LINE__, 99, sizeof "reason 99" - 1);
}

static void test_latch_is_due_until_a_publish_is_taken(void)
{
    ctl_reset_latch latch = CTL_RESET_LATCH_INIT;
    CHECK_TRUE(ctl_reset_latch_due(&latch));
    ctl_reset_latch_record(&latch, false);
    CHECK_TRUE(ctl_reset_latch_due(&latch));
    ctl_reset_latch_record(&latch, false);
    CHECK_TRUE(ctl_reset_latch_due(&latch));
    ctl_reset_latch_record(&latch, true);
    CHECK_TRUE(!ctl_reset_latch_due(&latch));
}

static void test_latch_stays_set_for_the_boot(void)
{
    ctl_reset_latch latch = CTL_RESET_LATCH_INIT;
    ctl_reset_latch_record(&latch, true);
    CHECK_TRUE(!ctl_reset_latch_due(&latch));
    ctl_reset_latch_record(&latch, false);
    CHECK_TRUE(!ctl_reset_latch_due(&latch));
    ctl_reset_latch_record(&latch, true);
    CHECK_TRUE(!ctl_reset_latch_due(&latch));
}

int main(void)
{
    test_exception();
    test_every_field_has_its_place();
    test_plain_reasons();
    test_esp32_reasons();
    test_longest_text_fits();
    test_small_buffer_is_refused();
    test_latch_is_due_until_a_publish_is_taken();
    test_latch_stays_set_for_the_boot();
    return check_result();
}
