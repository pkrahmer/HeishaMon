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
#define SETTING_EXPECTED                                                                        \
    "<tr><td><label for=\"x&#39;y\">x&#39;y</label></td>"                                       \
    "<td><input id=\"x&#39;y\" name=\"x&#39;y\" value=\"&lt;v&gt;\"></td>"                       \
    "<td>saved</td></tr>"
#define REFUSED_EXPECTED                                                                        \
    "<tr style='color:var(--orange)'><td>room.0.topic</td><td>" CTL_HTML_TOO_LONG "</td>"       \
    "<td>refused: &lt;why&gt;</td></tr>"

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

static void check_setting(void)
{
    char out[256];
    ctl_html_setting setting = {"x'y", "<v>", "saved", false};
    ctl_html_setting refused = {"room.0.topic", NULL, "refused: <why>", true};
    CHECK_TRUE(ctl_html_setting_row(&setting, out, sizeof out));
    CHECK_TEXT(out, SETTING_EXPECTED);
    CHECK_TRUE(ctl_html_setting_row(&setting, out, sizeof SETTING_EXPECTED));
    CHECK_TEXT(out, SETTING_EXPECTED);
    CHECK_TRUE(!ctl_html_setting_row(&setting, out, sizeof SETTING_EXPECTED - 1));
    CHECK_TRUE(!ctl_html_setting_row(&setting, out, 0));
    CHECK_TRUE(ctl_html_setting_row(&refused, out, sizeof out));
    CHECK_TEXT(out, REFUSED_EXPECTED);
    CHECK_TRUE(!ctl_html_setting_row(&refused, out, sizeof REFUSED_EXPECTED - 1));
}

int main(void)
{
    check_escape();
    check_row();
    check_setting();
    return check_result();
}
