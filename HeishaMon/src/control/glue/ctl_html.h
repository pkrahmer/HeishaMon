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

/* Writes a table row with a label and a text input named NAME and holding VALUE, all escaped,
 * and returns true. Refuses, returning false, a size that cannot hold it with its terminating
 * zero. */
bool ctl_html_input(const char *name, const char *value, char *out, size_t size);

#endif
