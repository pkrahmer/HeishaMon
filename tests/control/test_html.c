#include "check.h"

#include "glue/ctl_html.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t size;
    bool fits;
    const char *escaped; /* checked only when it fits */
} escape_row;

#define SPECIAL "a<b>&\"c'"
#define SPECIAL_ESCAPED "a&lt;b&gt;&amp;&quot;c&#39;"

static void check_escape(void)
{
    static const escape_row rows[] = {
        {SPECIAL, 64, true, SPECIAL_ESCAPED},
        {"plain text 21.5", 64, true, "plain text 21.5"},
        {"", 1, true, ""},
        {"", 0, false, NULL},
        {SPECIAL, sizeof SPECIAL_ESCAPED, true, SPECIAL_ESCAPED},
        {SPECIAL, sizeof SPECIAL_ESCAPED - 1, false, NULL},
        {"plain", sizeof "plain", true, "plain"},
        {"plain", sizeof "plain" - 1, false, NULL},
        /* An entity never fits in part. */
        {"x&", sizeof "x&amp;" - 1, false, NULL},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char out[64];
        memset(out, 'Z', sizeof out);
        bool fits = ctl_html_escape(rows[i].text, out, rows[i].size);
        CHECK_UINT(fits, rows[i].fits);
        if (rows[i].fits && fits) {
            CHECK_TEXT(out, rows[i].escaped);
        }
    }
}

#define ROW_EXPECTED "<tr><td>a&lt;b</td><td>1 &amp; &quot;2&quot;</td></tr>"
#define INPUT_EXPECTED                                                                          \
    "<tr><td><label for=\"x&#39;y\">x&#39;y</label></td>"                                       \
    "<td><input id=\"x&#39;y\" name=\"x&#39;y\" value=\"&lt;v&gt;\"></td></tr>"

static void check_row(void)
{
    char out[256];
    CHECK_TRUE(ctl_html_row("a<b", "1 & \"2\"", out, sizeof out));
    CHECK_TEXT(out, ROW_EXPECTED);
    CHECK_TRUE(ctl_html_row("a<b", "1 & \"2\"", out, sizeof ROW_EXPECTED));
    CHECK_TEXT(out, ROW_EXPECTED);
    CHECK_TRUE(!ctl_html_row("a<b", "1 & \"2\"", out, sizeof ROW_EXPECTED - 1));
    CHECK_TRUE(ctl_html_row("rooms", "2", out, sizeof out));
    CHECK_TEXT(out, "<tr><td>rooms</td><td>2</td></tr>");
}

static void check_input(void)
{
    char out[256];
    CHECK_TRUE(ctl_html_input("x'y", "<v>", out, sizeof out));
    CHECK_TEXT(out, INPUT_EXPECTED);
    CHECK_TRUE(ctl_html_input("x'y", "<v>", out, sizeof INPUT_EXPECTED));
    CHECK_TEXT(out, INPUT_EXPECTED);
    CHECK_TRUE(!ctl_html_input("x'y", "<v>", out, sizeof INPUT_EXPECTED - 1));
    CHECK_TRUE(!ctl_html_input("x'y", "<v>", out, 0));
    CHECK_TRUE(ctl_html_input("pv_topic", "", out, sizeof out));
    CHECK_TEXT(out, "<tr><td><label for=\"pv_topic\">pv_topic</label></td>"
                    "<td><input id=\"pv_topic\" name=\"pv_topic\" value=\"\"></td></tr>");
}

int main(void)
{
    check_escape();
    check_row();
    check_input();
    return check_result();
}
