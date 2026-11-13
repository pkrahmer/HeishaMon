/*
 * The text of the control page: the few pieces of HTML the page builds from values, every value
 * escaped, so that a topic or a name can never break the page. It writes into the caller's
 * buffer and never cuts a text: what does not fit is refused whole. It is pure.
 */
#ifndef CTL_HTML_H
#define CTL_HTML_H

#include <stdbool.h>
#include <stddef.h>

/* Writes text with &, <, >, " and ' as &amp;, &lt;, &gt;, &quot; and &#39; and returns true.
 * Refuses, returning false, a size that cannot hold the result with its terminating zero; the
 * buffer then holds nothing to rely on. */
bool ctl_html_escape(const char *text, char *out, size_t size);

/* Writes <tr><td>NAME</td><td>VALUE</td></tr>, both escaped, and returns true. Refuses, returning
 * false, a size that cannot hold it with its terminating zero. */
bool ctl_html_row(const char *name, const char *value, char *out, size_t size);

/* What a value too long to show escaped in one piece of the page shows instead. */
#define CTL_HTML_TOO_LONG "too long to show here"

/* One row of the settings table. */
typedef struct {
    const char *name;
    const char *value;       /* NULL: too long to show; the row then has no input */
    const char *description;
    const char *last_save;   /* what the last save did to the setting; empty: nothing to say */
    bool refused;            /* the last save refused the value: the row is marked */
} ctl_html_setting;

/* Writes a table row of four cells: a label and a text input, both named NAME, the input holding
 * VALUE, or the name and CTL_HTML_TOO_LONG for a NULL value; then the description and the last
 * save's text; every text escaped. A refused row is written in the page's warning colour. Returns
 * true. Refuses, returning false, a size that cannot hold it with its terminating zero. */
bool ctl_html_setting_row(const ctl_html_setting *setting, char *out, size_t size);

#endif
