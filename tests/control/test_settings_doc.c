#include "check.h"

#include "glue/ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DOCUMENT "docs/control/settings.md"
#define LINE_MAX_LENGTH 1024
#define LINES_MAX 1000
#define CELLS 4

/* The document names a room's settings once, for every room. */
static void document_name(const char *name, char *out, size_t size)
{
    static const char room_prefix[] = "room.0.";
    if (strncmp(name, room_prefix, strlen(room_prefix)) == 0) {
        snprintf(out, size, "room.<i>.%s", name + strlen(room_prefix));
    } else {
        snprintf(out, size, "%s", name);
    }
}

static bool is_blank(const char *start, const char *end)
{
    for (const char *c = start; c < end; c++) {
        if (*c != ' ') {
            return false;
        }
    }
    return true;
}

/* A row is "| `name` | default | allowed | what it does |", every cell holding text. */
static bool row_is_full(const char *line)
{
    const char *cell = line;
    for (size_t i = 0; i < CELLS; i++) {
        const char *end = strchr(cell + 1, '|');
        if (end == NULL || is_blank(cell + 1, end)) {
            return false;
        }
        cell = end;
    }
    return is_blank(cell + 1, cell + strcspn(cell + 1, "\r\n") + 1);
}

/* Counts the rows that start with the setting's name, and checks each is full. */
static size_t rows_of(const char *name)
{
    char start[CTL_SETTINGS_NAME_MAX + 8];
    char line[LINE_MAX_LENGTH];
    size_t rows = 0;
    FILE *file = fopen(DOCUMENT, "r");
    CHECK_TRUE(file != NULL);
    if (file == NULL) {
        return 0;
    }
    snprintf(start, sizeof start, "| `%s` |", name);
    for (size_t i = 0; i < LINES_MAX && fgets(line, sizeof line, file) != NULL; i++) {
        if (strncmp(line, start, strlen(start)) == 0) {
            rows++;
            if (!row_is_full(line)) {
                CHECK_TEXT(line, "a row with a default, an allowed range and a description");
            }
        }
    }
    fclose(file);
    return rows;
}

/* Every setting the list holds with one room has exactly one row in the document. */
static void test_one_row_per_setting(void)
{
    ctl_settings settings;
    ctl_settings_defaults(&settings);
    CHECK_TRUE(ctl_settings_set(&settings, "rooms", "1") == CTL_SETTING_OK);
    CHECK_UINT(ctl_settings_name_count(&settings), CTL_SETTINGS_FIXED_NAMES + 2);
    for (size_t i = 0; i < ctl_settings_name_count(&settings); i++) {
        char name[CTL_SETTINGS_NAME_MAX];
        char wanted[CTL_SETTINGS_NAME_MAX + 8];
        CHECK_TRUE(ctl_settings_name_at(&settings, i, name, sizeof name));
        document_name(name, wanted, sizeof wanted);
        if (rows_of(wanted) != 1) {
            CHECK_TEXT(wanted, "a setting with exactly one row");
        }
    }
}

int main(void)
{
    test_one_row_per_setting();
    return check_result();
}
