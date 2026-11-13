#include "ctl_html.h"

#include <string.h>

/* A text being written into a fixed buffer; once anything did not fit, it stays refused. */
typedef struct {
    char *out;
    size_t size;
    size_t length;
    bool fits;
} writer;

static writer writer_start(char *out, size_t size)
{
    writer w;
    w.out = out;
    w.size = size;
    w.length = 0;
    w.fits = size > 0;
    if (w.fits) {
        out[0] = '\0';
    }
    return w;
}

static void put_raw(writer *w, const char *text)
{
    size_t length = strlen(text);
    if (!w->fits || length >= w->size - w->length) {
        w->fits = false;
        return;
    }
    memcpy(w->out + w->length, text, length + 1);
    w->length += length;
}

static const char *entity_of(char c)
{
    switch (c) {
    case '&':
        return "&amp;";
    case '<':
        return "&lt;";
    case '>':
        return "&gt;";
    case '"':
        return "&quot;";
    case '\'':
        return "&#39;";
    default:
        return NULL;
    }
}

static void put_escaped(writer *w, const char *text)
{
    char plain[2] = {'\0', '\0'};
    for (const char *c = text; *c != '\0' && w->fits; c++) {
        const char *entity = entity_of(*c);
        if (entity == NULL) {
            plain[0] = *c;
            entity = plain;
        }
        put_raw(w, entity);
    }
}

bool ctl_html_escape(const char *text, char *out, size_t size)
{
    writer w = writer_start(out, size);
    put_escaped(&w, text);
    return w.fits;
}

bool ctl_html_row(const char *name, const char *value, char *out, size_t size)
{
    writer w = writer_start(out, size);
    put_raw(&w, "<tr><td>");
    put_escaped(&w, name);
    put_raw(&w, "</td><td>");
    put_escaped(&w, value);
    put_raw(&w, "</td></tr>");
    return w.fits;
}

static void put_setting_input(writer *w, const char *name, const char *value)
{
    put_raw(w, "<td><label for=\"");
    put_escaped(w, name);
    put_raw(w, "\">");
    put_escaped(w, name);
    put_raw(w, "</label></td><td><input id=\"");
    put_escaped(w, name);
    put_raw(w, "\" name=\"");
    put_escaped(w, name);
    put_raw(w, "\" value=\"");
    put_escaped(w, value);
    put_raw(w, "\"></td>");
}

/* Without an input, a save leaves the value as it is. */
static void put_setting_text(writer *w, const char *name)
{
    put_raw(w, "<td>");
    put_escaped(w, name);
    put_raw(w, "</td><td>" CTL_HTML_TOO_LONG "</td>");
}

bool ctl_html_setting_row(const ctl_html_setting *setting, char *out, size_t size)
{
    writer w = writer_start(out, size);
    put_raw(&w, setting->refused ? "<tr style='color:var(--orange)'>" : "<tr>");
    if (setting->value != NULL) {
        put_setting_input(&w, setting->name, setting->value);
    } else {
        put_setting_text(&w, setting->name);
    }
    put_raw(&w, "<td>");
    put_escaped(&w, setting->description);
    put_raw(&w, "</td><td>");
    put_escaped(&w, setting->last_save);
    put_raw(&w, "</td></tr>");
    return w.fits;
}
