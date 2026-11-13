#include "check.h"

#include "glue/ctl_form.h"
#include "glue/ctl_html.h"
#include "glue/ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define ROW_MAX 512 /* the page's chunk */
#define PART_MAX 16384
#define LINK                                                                                    \
    "<p><a href=\"https://github.com/pkrahmer/HeishaMon/blob/develop/docs/control/settings.md\">" \
    "What the settings mean</a></p>"

static size_t occurrences(const char *text, const char *wanted)
{
    size_t count = 0;
    const size_t length = strlen(wanted);
    for (const char *at = strstr(text, wanted); at != NULL; at = strstr(at + length, wanted)) {
        count++;
    }
    return count;
}

static ctl_settings settings;
static ctl_form_report report;

static void four_rooms(void)
{
    ctl_settings_defaults(&settings);
    ctl_form_report_start(&report);
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "4") == CTL_SETTING_OK);
}

/* The settings rows say no more than the setting, its value and the save result. */
static void test_rows_without_description(void)
{
    static const char *const texts[] = {
        "Send commands to the pump",
        "MQTT topic of room",
        "The house's heat loss",
    };
    char row[ROW_MAX];
    four_rooms();
    CHECK_UINT(ctl_settings_name_count(&settings), CTL_SETTINGS_FIXED_NAMES + 8);
    for (size_t i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_form_row(&settings, &report, i, true, row, sizeof row));
        CHECK_UINT(occurrences(row, "<td"), 3);
        CHECK_UINT(occurrences(row, "</td>"), 3);
        for (size_t t = 0; t < sizeof texts / sizeof texts[0]; t++) {
            CHECK_TRUE(strstr(row, texts[t]) == NULL);
        }
    }
}

static void append(char *part, const char *text)
{
    CHECK_TRUE(strlen(part) + strlen(text) < PART_MAX);
    if (strlen(part) + strlen(text) < PART_MAX) {
        strcat(part, text);
    }
}

/* The settings part of the page: its opening text, its rows and its closing text. */
static void test_link_once(void)
{
    static char part[PART_MAX];
    char row[ROW_MAX];
    four_rooms();
    part[0] = '\0';
    append(part, CTL_HTML_SETTINGS_OPEN);
    for (size_t i = 0; i < ctl_settings_name_count(&settings); i++) {
        CHECK_TRUE(ctl_form_row(&settings, &report, i, true, row, sizeof row));
        append(part, row);
    }
    append(part, CTL_HTML_SETTINGS_CLOSE);
    CHECK_UINT(occurrences(part, LINK), 1);
    /* The link stands above the table, whose head has the three columns. */
    CHECK_TRUE(strstr(CTL_HTML_SETTINGS_OPEN, LINK) != NULL);
    CHECK_TRUE(strstr(CTL_HTML_SETTINGS_OPEN, LINK) < strstr(CTL_HTML_SETTINGS_OPEN, "<table"));
    CHECK_TRUE(strstr(part, "<tr><th>Setting</th><th>Value</th><th>Save result</th></tr>")
               != NULL);
    CHECK_TRUE(strstr(part, "What it does") == NULL);
}

int main(void)
{
    test_rows_without_description();
    test_link_once();
    return check_result();
}
