#include "check.h"

#include "glue/ctl_form.h"
#include "glue/ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The setter the firmware gives, without its file and topics: the real settings setter, with
 * the value text compared before and after. */
typedef struct {
    ctl_settings settings;
    int calls;
} setter_context;

static ctl_setting_status set_setting(void *context, const char *name, const char *value,
                                      bool *changed)
{
    setter_context *target = (setter_context *)context;
    char before[CTL_SETTINGS_VALUE_MAX];
    char after[CTL_SETTINGS_VALUE_MAX];
    bool known = ctl_settings_get(&target->settings, name, before, sizeof before);
    ctl_setting_status status = ctl_settings_set(&target->settings, name, value);
    target->calls++;
    *changed = status == CTL_SETTING_OK
               && (!known || !ctl_settings_get(&target->settings, name, after, sizeof after)
                   || strcmp(before, after) != 0);
    return status;
}

static void keep_text(ctl_form *form, const char *name, const char *value)
{
    ctl_form_keep(form, name, value, strlen(value));
}

static void check_setting(const ctl_settings *settings, const char *name, const char *expected)
{
    char value[CTL_SETTINGS_VALUE_MAX];
    CHECK_TRUE(ctl_settings_get(settings, name, value, sizeof value));
    CHECK_TEXT(value, expected);
}

static void check_report_entry(const ctl_form_report *report, size_t index, const char *name,
                               bool accepted)
{
    CHECK_TRUE(index < report->count);
    if (index < report->count) {
        CHECK_TEXT(report->name[index], name);
        CHECK_UINT(report->accepted[index], accepted);
    }
}

static ctl_form form;
static ctl_form_report report;
static setter_context target;

static void start(void)
{
    ctl_form_start(&form);
    ctl_form_report_start(&report);
    ctl_settings_defaults(&target.settings);
    target.calls = 0;
}

/* The browser sends the fields of new rooms before rooms itself, and a field no setting has. */
static void test_browser_order(void)
{
    start();
    keep_text(&form, "room.1.topic", "house/b");
    keep_text(&form, "nonsense", "1");
    keep_text(&form, "room.0.topic", "house/a");
    keep_text(&form, "desired_indoor_c", "21.5");
    keep_text(&form, "hysteresis_wh", "abc");
    keep_text(&form, "rooms", "2");
    keep_text(&form, "room.0.weight", "0.5");
    CHECK_TRUE(ctl_form_apply(&form, &target.settings, set_setting, &target, &report));
    CHECK_UINT(target.settings.room_count, 2);
    check_setting(&target.settings, "room.0.topic", "house/a");
    check_setting(&target.settings, "room.1.topic", "house/b");
    check_setting(&target.settings, "room.0.weight", "0.5");
    check_setting(&target.settings, "desired_indoor_c", "21.5");
    CHECK_TRUE(report.saved);
    CHECK_UINT(report.count, 6);
    check_report_entry(&report, 0, "rooms", true);
    check_report_entry(&report, 1, "room.0.topic", true);
    check_report_entry(&report, 2, "room.0.weight", true);
    check_report_entry(&report, 3, "room.1.topic", true);
    check_report_entry(&report, 4, "desired_indoor_c", true);
    check_report_entry(&report, 5, "hysteresis_wh", false);
}

/* Posting the values a setting already holds is accepted, but changes nothing. */
static void test_unchanged(void)
{
    start();
    keep_text(&form, "rooms", "0");
    keep_text(&form, "enabled", "0");
    CHECK_TRUE(!ctl_form_apply(&form, &target.settings, set_setting, &target, &report));
    CHECK_UINT(report.count, 2);
    check_report_entry(&report, 0, "enabled", true);
    check_report_entry(&report, 1, "rooms", true);
}

static void test_pieces(void)
{
    start();
    keep_text(&form, "pv_topic", "house/");
    keep_text(&form, "pv_topic", "pv/");
    ctl_form_keep(&form, "pv_topic", NULL, 0);
    keep_text(&form, "pv_topic", "power");
    /* A name that comes back after another starts its value again. */
    keep_text(&form, "desired_indoor_c", "2");
    keep_text(&form, "rooms", "1");
    keep_text(&form, "desired_indoor_c", "19");
    CHECK_UINT(form.count, 3);
    CHECK_TRUE(ctl_form_apply(&form, &target.settings, set_setting, &target, &report));
    check_setting(&target.settings, "pv_topic", "house/pv/power");
    check_setting(&target.settings, "desired_indoor_c", "19");
}

static void test_too_long(void)
{
    char piece[CTL_SETTINGS_VALUE_MAX / 2];
    memset(piece, 'a', sizeof piece);
    start();
    /* Exactly the longest value that fits, in two pieces. */
    ctl_form_keep(&form, "pv_topic", piece, sizeof piece);
    ctl_form_keep(&form, "pv_topic", piece, sizeof piece - 1);
    CHECK_TRUE(form.count == 1 && form.field[0].fits);
    /* One byte more, then a piece that alone would fit: refused whole. */
    ctl_form_keep(&form, "pv_topic", "b", 1);
    ctl_form_keep(&form, "pv_topic", "", 0);
    CHECK_TRUE(form.count == 1 && !form.field[0].fits);
    keep_text(&form, "room_timeout_s", "a\0b");
    ctl_form_keep(&form, "room_timeout_s", "a\0b", 3);
    CHECK_TRUE(form.count == 2 && !form.field[1].fits);
    CHECK_TRUE(!ctl_form_apply(&form, &target.settings, set_setting, &target, &report));
    CHECK_UINT(target.calls, 0);
    check_setting(&target.settings, "pv_topic", "");
    CHECK_UINT(report.count, 2);
    check_report_entry(&report, 0, "room_timeout_s", false);
    check_report_entry(&report, 1, "pv_topic", false);
}

static void test_unknown_name(void)
{
    char name[CTL_SETTINGS_NAME_MAX + 1];
    memset(name, 'n', sizeof name - 1);
    name[sizeof name - 1] = '\0';
    start();
    ctl_form_keep(&form, NULL, "1", 1);
    keep_text(&form, name, "1");
    CHECK_UINT(form.count, 0);
    keep_text(&form, "room.0.topic", "house/a");
    keep_text(&form, "unknown", "1");
    CHECK_UINT(form.count, 2);
    CHECK_TRUE(!ctl_form_apply(&form, &target.settings, set_setting, &target, &report));
    CHECK_UINT(target.calls, 0);
    CHECK_TRUE(report.saved);
    CHECK_UINT(report.count, 0);
}

static void test_full_form(void)
{
    start();
    for (size_t i = 0; i < CTL_FORM_FIELDS_MAX + 2; i++) {
        char name[16];
        name[0] = 'x';
        name[1] = (char)('A' + i / 26);
        name[2] = (char)('a' + i % 26);
        name[3] = '\0';
        keep_text(&form, name, "1");
    }
    CHECK_UINT(form.count, CTL_FORM_FIELDS_MAX);
    keep_text(&form, "rooms", "1");
    CHECK_UINT(form.count, CTL_FORM_FIELDS_MAX);
}

static void report_text(char *text, size_t size)
{
    char item[256];
    text[0] = '\0';
    for (size_t i = 0; i < ctl_form_report_items(&report); i++) {
        CHECK_TRUE(ctl_form_report_item(&report, i, item, sizeof item));
        CHECK_TRUE(strlen(text) + strlen(item) < size);
        strcat(text, item);
    }
}

#define REPORT_EXPECTED                                                                         \
    "<h2>Last save</h2><div class='panel'><table>"                                              \
    "<tr><td>rooms</td><td>accepted</td></tr>"                                                  \
    "<tr><td>hysteresis_wh</td><td>refused</td></tr>"                                           \
    "</table></div>"
#define ROW_EXPECTED "<tr><td>rooms</td><td>accepted</td></tr>"
#define EMPTY_EXPECTED "<p>The last save held no setting.</p>"

static void test_report_text(void)
{
    char text[512];
    char item[256];
    start();
    CHECK_UINT(ctl_form_report_items(&report), 0);
    CHECK_TRUE(!ctl_form_report_item(&report, 0, item, sizeof item));
    ctl_form_apply(&form, &target.settings, set_setting, &target, &report);
    report_text(text, sizeof text);
    CHECK_TEXT(text, EMPTY_EXPECTED);
    CHECK_TRUE(!ctl_form_report_item(&report, 0, item, sizeof EMPTY_EXPECTED - 1));

    keep_text(&form, "hysteresis_wh", "abc");
    keep_text(&form, "rooms", "1");
    ctl_form_apply(&form, &target.settings, set_setting, &target, &report);
    CHECK_UINT(ctl_form_report_items(&report), 4);
    report_text(text, sizeof text);
    CHECK_TEXT(text, REPORT_EXPECTED);
    CHECK_TRUE(ctl_form_report_item(&report, 1, item, sizeof ROW_EXPECTED));
    CHECK_TEXT(item, ROW_EXPECTED);
    CHECK_TRUE(!ctl_form_report_item(&report, 1, item, sizeof ROW_EXPECTED - 1));
    CHECK_TRUE(!ctl_form_report_item(&report, 4, item, sizeof item));

    ctl_form_report_start(&report);
    CHECK_UINT(ctl_form_report_items(&report), 0);
}

int main(void)
{
    test_browser_order();
    test_unchanged();
    test_pieces();
    test_too_long();
    test_unknown_name();
    test_full_form();
    test_report_text();
    return check_result();
}
