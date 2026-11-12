#include "ctl_form.h"

#include "ctl_html.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const char REPORT_OPEN[] = "<h2>Last save</h2><div class='panel'><table>";
static const char REPORT_CLOSE[] = "</table></div>";
static const char REPORT_EMPTY[] = "<p>The last save held no setting.</p>";

static size_t field_index(const ctl_form *form, const char *name)
{
    for (size_t i = 0; i < form->count && i < CTL_FORM_FIELDS_MAX; i++) {
        if (strcmp(form->field[i].name, name) == 0) {
            return i;
        }
    }
    return CTL_FORM_FIELDS_MAX;
}

static void start_value(ctl_form_field *field)
{
    field->value[0] = '\0';
    field->length = 0;
    field->fits = true;
}

// A value that does not fit is refused whole, never cut, so a later piece cannot revive it.
static void append_value(ctl_form_field *field, const char *value, size_t length)
{
    if (!field->fits || value == NULL || length == 0) {
        return;
    }
    if (length >= sizeof field->value - field->length || memchr(value, '\0', length) != NULL) {
        field->fits = false;
        return;
    }
    memcpy(&field->value[field->length], value, length);
    field->length += length;
    field->value[field->length] = '\0';
}

void ctl_form_start(ctl_form *form)
{
    form->count = 0;
    form->last = CTL_FORM_FIELDS_MAX;
}

void ctl_form_keep(ctl_form *form, const char *name, const char *value, size_t length)
{
    if (name == NULL || strlen(name) >= CTL_SETTINGS_NAME_MAX) {
        form->last = CTL_FORM_FIELDS_MAX;
        return;
    }
    size_t index = field_index(form, name);
    if (index == CTL_FORM_FIELDS_MAX) {
        if (form->count >= CTL_FORM_FIELDS_MAX) {
            form->last = CTL_FORM_FIELDS_MAX;
            return;
        }
        index = form->count++;
        strcpy(form->field[index].name, name);
    }
    ctl_form_field *field = &form->field[index];
    if (index != form->last) {
        start_value(field);
    }
    append_value(field, value, length);
    form->last = index;
}

static void report_name(ctl_form_report *report, const char *name, bool accepted)
{
    if (report->count < CTL_FORM_FIELDS_MAX) {
        strcpy(report->name[report->count], name);
        report->accepted[report->count] = accepted;
        report->count++;
    }
}

bool ctl_form_apply(const ctl_form *form, const ctl_settings *settings, ctl_form_setter set,
                    void *context, ctl_form_report *report)
{
    char name[CTL_SETTINGS_NAME_MAX];
    bool any_changed = false;
    report->saved = true;
    report->count = 0;
    // The setter changes the settings, so the count is read again after every field.
    for (size_t i = 0; i < CTL_FORM_FIELDS_MAX && i < ctl_settings_name_count(settings); i++) {
        if (!ctl_settings_name_at(settings, i, name, sizeof name)) {
            continue;
        }
        size_t index = field_index(form, name);
        if (index == CTL_FORM_FIELDS_MAX) {
            continue;
        }
        const ctl_form_field *field = &form->field[index];
        bool changed = false;
        bool accepted = field->fits && set(context, name, field->value, &changed) == CTL_SETTING_OK;
        report_name(report, name, accepted);
        any_changed = any_changed || (accepted && changed);
    }
    return any_changed;
}

void ctl_form_report_start(ctl_form_report *report)
{
    report->saved = false;
    report->count = 0;
}

size_t ctl_form_report_items(const ctl_form_report *report)
{
    if (!report->saved) {
        return 0;
    }
    return report->count == 0 ? 1 : report->count + 2;
}

static bool put_text(const char *text, char *out, size_t size)
{
    size_t length = strlen(text);
    if (length >= size) {
        return false;
    }
    memcpy(out, text, length + 1);
    return true;
}

bool ctl_form_report_item(const ctl_form_report *report, size_t index, char *out, size_t size)
{
    size_t items = ctl_form_report_items(report);
    if (index >= items) {
        return false;
    }
    if (report->count == 0) {
        return put_text(REPORT_EMPTY, out, size);
    }
    if (index == 0) {
        return put_text(REPORT_OPEN, out, size);
    }
    if (index == items - 1) {
        return put_text(REPORT_CLOSE, out, size);
    }
    size_t row = index - 1;
    return ctl_html_row(report->name[row], report->accepted[row] ? "accepted" : "refused", out,
                        size);
}
