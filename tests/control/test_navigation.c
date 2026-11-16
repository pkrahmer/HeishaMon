#include "check.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The shared page header holds the side menu of every page; the control page must show it as it
 * is, with its own link in it, and not replace it. */
#define HEADER_PATH "HeishaMon/htmlcode.h"
#define PAGE_PATH "HeishaMon/src/control/glue/ctl_web.cpp"
#define CONTROL_LINK                                                                               \
    "    <a href=\"/control\"><span class=\"nav-icon\" aria-hidden=\"true\">&#9832;</span> "       \
    "Control</a>\n"
#define LINE_MAX_LENGTH 1024
#define LINES_MAX 100000
#define NOT_FOUND 0

/* Where the lines that matter stand in the header, counted from 1; 0 is not found. */
typedef struct {
    size_t control_lines;
    size_t control;
    size_t nav_open;
    size_t nav_close;
    size_t rules;
    size_t settings;
} header_lines;

static void note_first(size_t *where, size_t number)
{
    if (*where == NOT_FOUND) {
        *where = number;
    }
}

static void note_header_line(header_lines *found, const char *line, size_t number)
{
    if (strstr(line, "id='sideNav'") != NULL) {
        note_first(&found->nav_open, number);
    }
    if (found->nav_open != NOT_FOUND && strstr(line, "</nav>") != NULL) {
        note_first(&found->nav_close, number);
    }
    if (strstr(line, "href=\"/rules\"") != NULL) {
        note_first(&found->rules, number);
    }
    if (strstr(line, "href=\"/settings\"") != NULL) {
        note_first(&found->settings, number);
    }
    if (strstr(line, "href=\"/control\"") != NULL) {
        found->control_lines++;
        found->control = number;
        CHECK_TEXT(line, CONTROL_LINK);
    }
}

static void test_link_in_shared_menu(void)
{
    header_lines found = { 0, NOT_FOUND, NOT_FOUND, NOT_FOUND, NOT_FOUND, NOT_FOUND };
    char line[LINE_MAX_LENGTH];
    FILE *file = fopen(HEADER_PATH, "r");
    CHECK_TRUE(file != NULL);
    if (file == NULL) {
        return;
    }
    for (size_t number = 1; number <= LINES_MAX && fgets(line, sizeof line, file) != NULL;
         number++) {
        note_header_line(&found, line, number);
    }
    fclose(file);
    CHECK_UINT((uint32_t)found.control_lines, 1);
    CHECK_TRUE(found.nav_open != NOT_FOUND && found.nav_open < found.control);
    CHECK_TRUE(found.control < found.nav_close);
    /* Right between the two links, so no preprocessor line can enclose it alone. */
    CHECK_UINT((uint32_t)found.rules, (uint32_t)(found.control - 1));
    CHECK_UINT((uint32_t)found.settings, (uint32_t)(found.control + 1));
}

static void test_page_keeps_shared_menu(void)
{
    bool main_content = false;
    bool heading = false;
    bool top_bar_closed = false;
    char line[LINE_MAX_LENGTH];
    FILE *file = fopen(PAGE_PATH, "r");
    CHECK_TRUE(file != NULL);
    if (file == NULL) {
        return;
    }
    for (size_t number = 1; number <= LINES_MAX && fgets(line, sizeof line, file) != NULL;
         number++) {
        if (strstr(line, "sideNav") != NULL || strstr(line, "nav-icon") != NULL) {
            CHECK_TEXT(line, "a line that leaves the shared menu alone");
        }
        main_content = main_content || strstr(line, "<main class='main-content'>") != NULL;
        heading = heading || strcmp(line, "Control loop</h1>\n") == 0;
        top_bar_closed = top_bar_closed || strstr(line, "webBodyStart2") != NULL;
    }
    fclose(file);
    CHECK_TRUE(main_content);
    CHECK_TRUE(heading);
    CHECK_TRUE(top_bar_closed);
}

int main(void)
{
    test_link_in_shared_menu();
    test_page_keeps_shared_menu();
    return check_result();
}
