#include "check.h"

#include "glue/ctl_form.h"
#include "glue/ctl_html.h"
#include "glue/ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ROW_MAX 512 /* the page's chunk */
#define REFUSED_MARK "<tr style='color:var(--orange)'>"

/* The real settings setter, with the firmware's rule for a change: a name that had no value
   before, or whose value text differs afterwards, has changed. */
static ctl_setting_status set_setting(void *context, const char *name, const char *value,
                                      bool *changed)
{
    ctl_settings *settings = (ctl_settings *)context;
    char before[CTL_SETTINGS_VALUE_MAX];
    char after[CTL_SETTINGS_VALUE_MAX];
    bool known = ctl_settings_get(settings, name, before, sizeof before);
    ctl_setting_status status = ctl_settings_set(settings, name, value);
    *changed = status == CTL_SETTING_OK
               && (!known || !ctl_settings_get(settings, name, after, sizeof after)
                   || strcmp(before, after) != 0);
    return status;
}

static void keep_text(ctl_form *form, const char *name, const char *value)
{
    ctl_form_keep(form, name, value, strlen(value));
}

static size_t index_of(const ctl_settings *settings, const char *wanted)
{
    char name[CTL_SETTINGS_NAME_MAX];
    for (size_t i = 0; i < ctl_settings_name_count(settings); i++) {
        if (ctl_settings_name_at(settings, i, name, sizeof name) && strcmp(name, wanted) == 0) {
            return i;
        }
    }
    CHECK_TEXT(wanted, "a listed name");
    return 0;
}

static bool ends_with(const char *text, const char *end)
{
    size_t length = strlen(text);
    size_t end_length = strlen(end);
    return length >= end_length && strcmp(text + length - end_length, end) == 0;
}

static bool starts_with(const char *text, const char *start)
{
    return strncmp(text, start, strlen(start)) == 0;
}

static ctl_settings settings;
static ctl_form form;
static ctl_form_report report;

static void row_of(const char *name, char *row)
{
    CHECK_TRUE(ctl_form_row(&settings, &report, index_of(&settings, name), true, row, ROW_MAX));
}

/* The row of a setting: its name, its field and the last save's result. */
static void expect_row(const char *name, const char *value, const char *start, const char *last)
{
    char expected[ROW_MAX];
    char row[ROW_MAX];
    snprintf(expected, sizeof expected,
             "%s<td><label for=\"%s\">%s</label></td><td><input id=\"%s\" name=\"%s\" "
             "value=\"%s\"></td><td>%s</td></tr>",
             start, name, name, name, name, value, last);
    row_of(name, row);
    CHECK_TEXT(row, expected);
}

static void save_three(void)
{
    ctl_settings_defaults(&settings);
    ctl_form_start(&form);
    ctl_form_report_start(&report);
    keep_text(&form, "hysteresis_wh", "1000");
    keep_text(&form, "enabled", "0");
    keep_text(&form, "rooms", "99");
    CHECK_TRUE(ctl_form_apply(&form, &settings, set_setting, &settings, &report));
}

static void test_rows_after_save(void)
{
    char row[ROW_MAX];
    save_three();
    expect_row("hysteresis_wh", "1000", "<tr>", "saved");
    expect_row("enabled", "0", "<tr>", "");
    expect_row("rooms", "0", REFUSED_MARK, "refused: not a valid value");
    row_of("hysteresis_wh", row);
    CHECK_TRUE(ends_with(row, "<td>saved</td></tr>"));
    row_of("enabled", row);
    CHECK_TRUE(ends_with(row, "<td></td></tr>"));
    row_of("rooms", row);
    CHECK_TRUE(starts_with(row, REFUSED_MARK));
    CHECK_TRUE(strstr(row, "value=\"0\"") != NULL);
    CHECK_TRUE(ends_with(row, "<td>refused: not a valid value</td></tr>"));
    /* A setting the save did not hold has an empty last column. */
    expect_row("cooling", "0", "<tr>", "");
}

/* Every refusal of the setter has a reason of its own. */
static void test_reasons(void)
{
    ctl_settings_defaults(&settings);
    ctl_form_start(&form);
    ctl_form_report_start(&report);
    keep_text(&form, "desired_indoor_c", "40");
    keep_text(&form, "cooling", "2");
    CHECK_TRUE(!ctl_form_apply(&form, &settings, set_setting, &settings, &report));
    CHECK_UINT(ctl_form_report_result(&report, "desired_indoor_c"), CTL_SAVE_REFUSED_INVALID_SET);
    CHECK_UINT(ctl_form_report_result(&report, "cooling"), CTL_SAVE_REFUSED_BAD_VALUE);
    CHECK_UINT(ctl_form_report_result(&report, "rooms"), CTL_SAVE_UNCHANGED);
    expect_row("desired_indoor_c", "22", REFUSED_MARK, "refused: conflicts with other settings");
    expect_row("cooling", "0", REFUSED_MARK, "refused: not a valid value");
}

static ctl_setting_status refuse_unknown(void *context, const char *name, const char *value,
                                         bool *changed)
{
    (void)context;
    (void)name;
    (void)value;
    *changed = false;
    return CTL_SETTING_UNKNOWN;
}

static void test_unknown(void)
{
    ctl_settings_defaults(&settings);
    ctl_form_start(&form);
    ctl_form_report_start(&report);
    keep_text(&form, "pv_topic", "house/pv");
    CHECK_TRUE(!ctl_form_apply(&form, &settings, refuse_unknown, NULL, &report));
    expect_row("pv_topic", "", REFUSED_MARK, "refused: unknown setting");
}

/* Before any save, and once the report is emptied, every last column is empty. */
static void test_no_save(void)
{
    char row[ROW_MAX];
    save_three();
    ctl_form_report_start(&report);
    for (size_t i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_form_row(&settings, &report, i, true, row, sizeof row));
        CHECK_TRUE(starts_with(row, "<tr><td>"));
        CHECK_TRUE(ends_with(row, "<td></td></tr>"));
    }
}

/* A row whose field does not fit shows the value as too long and keeps the other columns. */
static void test_not_editable(void)
{
    const char *expected = REFUSED_MARK "<td>rooms</td><td>" CTL_HTML_TOO_LONG
                                        "</td><td>refused: not a valid value</td></tr>";
    char row[ROW_MAX];
    save_three();
    CHECK_TRUE(ctl_form_row(&settings, &report, index_of(&settings, "rooms"), false, row,
                            sizeof row));
    CHECK_TEXT(row, expected);
    CHECK_TRUE(ctl_form_row(&settings, &report, index_of(&settings, "rooms"), false, row,
                            strlen(expected) + 1));
    CHECK_TRUE(!ctl_form_row(&settings, &report, index_of(&settings, "rooms"), false, row,
                             strlen(expected)));
    /* An index not listed writes nothing and fails nothing. */
    CHECK_TRUE(ctl_form_row(&settings, &report, ctl_settings_name_count(&settings), true, row,
                            sizeof row));
    CHECK_TEXT(row, "");
}

/* No text of the page names the removed table. */
static void test_no_last_save_table(void)
{
    static const char *const sources[] = {
        "HeishaMon/src/control/glue/ctl_web.cpp",
        "HeishaMon/src/control/glue/ctl_form.c",
        "HeishaMon/src/control/glue/ctl_html.c",
    };
    char row[ROW_MAX];
    save_three();
    for (size_t i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_form_row(&settings, &report, i, true, row, sizeof row));
        CHECK_TRUE(strstr(row, "Last save") == NULL);
    }
    for (size_t i = 0; i < sizeof sources / sizeof sources[0]; i++) {
        char line[1024];
        FILE *file = fopen(sources[i], "r");
        CHECK_TRUE(file != NULL);
        if (file == NULL) {
            continue;
        }
        for (size_t lines = 0; lines < 100000 && fgets(line, sizeof line, file) != NULL;
             lines++) {
            if (strstr(line, "Last save") != NULL) {
                CHECK_TEXT(line, "a line without the removed table's title");
            }
        }
        fclose(file);
    }
}

int main(void)
{
    test_rows_after_save();
    test_reasons();
    test_unknown();
    test_no_save();
    test_not_editable();
    test_no_last_save_table();
    return check_result();
}
