#include "ctl_html.h"

#include "../ctl_flash.h"

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

/* Appends text, a text in flash. */
static void put_flash(writer *w, const char *text)
{
    size_t length = CTL_FLASH_LENGTH(text);
    if (!w->fits || length >= w->size - w->length) {
        w->fits = false;
        return;
    }
    CTL_FLASH_COPY(w->out + w->length, text, length + 1);
    w->length += length;
}

/* A table rather than a switch: the compiler turns the switch into a table of pointers in RAM. */
typedef struct {
    char plain;
    char entity[7];
} entity;

static const entity ENTITIES[] CTL_FLASH = {
    {'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"}, {'"', "&quot;"}, {'\'', "&#39;"},
};

/* Returns the entity of c in flash, or NULL for a character written as it is. */
static const char *entity_of(char c)
{
    size_t i;
    for (i = 0; i < sizeof ENTITIES / sizeof ENTITIES[0]; i++) {
        if (CTL_FLASH_CHAR(&ENTITIES[i].plain) == c) {
            return ENTITIES[i].entity;
        }
    }
    return NULL;
}

static void put_escaped(writer *w, const char *text)
{
    char plain[2] = {'\0', '\0'};
    for (const char *c = text; *c != '\0' && w->fits; c++) {
        const char *entity = entity_of(*c);
        if (entity == NULL) {
            plain[0] = *c;
            put_raw(w, plain);
        } else {
            put_flash(w, entity);
        }
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
    put_flash(&w, CTL_FLASH_TEXT("<tr><td>"));
    put_escaped(&w, name);
    put_flash(&w, CTL_FLASH_TEXT("</td><td>"));
    put_escaped(&w, value);
    put_flash(&w, CTL_FLASH_TEXT("</td></tr>"));
    return w.fits;
}

static void put_setting_input(writer *w, const char *name, const char *value)
{
    put_flash(w, CTL_FLASH_TEXT("<td><label for=\""));
    put_escaped(w, name);
    put_flash(w, CTL_FLASH_TEXT("\">"));
    put_escaped(w, name);
    put_flash(w, CTL_FLASH_TEXT("</label></td><td><input id=\""));
    put_escaped(w, name);
    put_flash(w, CTL_FLASH_TEXT("\" name=\""));
    put_escaped(w, name);
    put_flash(w, CTL_FLASH_TEXT("\" value=\""));
    put_escaped(w, value);
    put_flash(w, CTL_FLASH_TEXT("\"></td>"));
}

/* Without an input, a save leaves the value as it is. */
static void put_setting_text(writer *w, const char *name)
{
    put_flash(w, CTL_FLASH_TEXT("<td>"));
    put_escaped(w, name);
    put_flash(w, CTL_FLASH_TEXT("</td><td>" CTL_HTML_TOO_LONG "</td>"));
}

bool ctl_html_setting_row(const ctl_html_setting *setting, char *out, size_t size)
{
    writer w = writer_start(out, size);
    put_flash(&w, setting->refused ? CTL_FLASH_TEXT("<tr style='color:var(--orange)'>")
                                    : CTL_FLASH_TEXT("<tr>"));
    if (setting->value != NULL) {
        put_setting_input(&w, setting->name, setting->value);
    } else {
        put_setting_text(&w, setting->name);
    }
    put_flash(&w, CTL_FLASH_TEXT("<td>"));
    put_escaped(&w, setting->last_save);
    put_flash(&w, CTL_FLASH_TEXT("</td></tr>"));
    return w.fits;
}
