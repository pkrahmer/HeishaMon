#include "check.h"

#include "glue/ctl_form.h"
#include "glue/ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The report keeps indexes of the settings list; these checks hold it to the names. */

static ctl_setting_status set_setting(void *context, const char *name, const char *value,
                                      bool *changed)
{
    ctl_setting_status status = ctl_settings_set((ctl_settings *)context, name, value);
    *changed = status == CTL_SETTING_OK;
    return status;
}

static void keep_text(ctl_form *form, const char *name, const char *value)
{
    ctl_form_keep(form, name, value, strlen(value));
}

static size_t index_of(const ctl_settings *settings, const char *name)
{
    char listed[CTL_SETTINGS_NAME_MAX];
    for (size_t i = 0; i < ctl_settings_name_count(settings); i++) {
        if (ctl_settings_name_at(settings, i, listed, sizeof listed)
            && strcmp(listed, name) == 0) {
            return i;
        }
    }
    return CTL_SETTINGS_NAMES_MAX;
}

static ctl_settings settings;
static ctl_form form;
static ctl_form_report report;

static void save_with_rooms(void)
{
    ctl_settings_defaults(&settings);
    ctl_form_start(&form);
    ctl_form_report_start(&report);
    keep_text(&form, "integral_limit_w", "0");
    keep_text(&form, "room.1.weight", "2");
    keep_text(&form, "rooms", "2");
    keep_text(&form, "integral_w_per_kh", "25");
    CHECK_TRUE(ctl_form_apply(&form, &settings, set_setting, &settings, &report));
}

/* A save that raises `rooms` moves the names after it; the report follows them. */
static void test_names_after_rooms(void)
{
    char row[512];
    save_with_rooms();
    CHECK_UINT(report.count, 4);
    CHECK_UINT(ctl_form_report_result(&report, "rooms"), CTL_SAVE_SAVED);
    CHECK_UINT(ctl_form_report_result(&report, "room.1.weight"), CTL_SAVE_SAVED);
    CHECK_UINT(ctl_form_report_result(&report, "integral_w_per_kh"), CTL_SAVE_SAVED);
    CHECK_UINT(ctl_form_report_result(&report, "integral_limit_w"), CTL_SAVE_REFUSED_INVALID_SET);
    CHECK_UINT(ctl_form_report_result(&report, "trim_limit_k"), CTL_SAVE_UNCHANGED);
    CHECK_TRUE(ctl_form_row(&settings, &report, index_of(&settings, "integral_limit_w"), true, row,
                            sizeof row));
    CHECK_TRUE(strstr(row, "refused: conflicts with other settings") != NULL);
}

/* Once the count of names moves after the save, an index may name another setting: no result. */
static void test_count_moved_after_save(void)
{
    char row[512];
    save_with_rooms();
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "1") == CTL_SETTING_OK);
    CHECK_UINT(ctl_form_report_result(&report, "integral_w_per_kh"), CTL_SAVE_UNCHANGED);
    CHECK_UINT(ctl_form_report_result(&report, "rooms"), CTL_SAVE_UNCHANGED);
    CHECK_TRUE(ctl_form_row(&settings, &report, index_of(&settings, "integral_limit_w"), true, row,
                            sizeof row));
    CHECK_TRUE(strstr(row, "refused") == NULL);
}

/* An emptied report holds no result, whatever the settings. */
static void test_started(void)
{
    ctl_settings_defaults(&settings);
    ctl_form_report_start(&report);
    CHECK_UINT(report.count, 0);
    CHECK_UINT(ctl_form_report_result(&report, "enabled"), CTL_SAVE_UNCHANGED);
}

int main(void)
{
    test_names_after_rooms();
    test_count_moved_after_save();
    test_started();
    return check_result();
}
