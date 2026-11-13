#include "ctl_form.h"

#include "ctl_html.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

static void report_name(ctl_form_report *report, const char *name, ctl_save_result result)
{
    if (report->count < CTL_FORM_FIELDS_MAX) {
        strcpy(report->name[report->count], name);
        report->result[report->count] = (uint8_t)result;
        report->count++;
    }
}

static ctl_save_result result_of(ctl_setting_status status, bool changed)
{
    switch (status) {
    case CTL_SETTING_OK:
        return changed ? CTL_SAVE_SAVED : CTL_SAVE_UNCHANGED;
    case CTL_SETTING_BAD_VALUE:
        return CTL_SAVE_REFUSED_BAD_VALUE;
    case CTL_SETTING_INVALID_SET:
        return CTL_SAVE_REFUSED_INVALID_SET;
    case CTL_SETTING_UNKNOWN:
    default:
        return CTL_SAVE_REFUSED_UNKNOWN;
    }
}

static ctl_save_result apply_field(const ctl_form_field *field, const char *name,
                                   ctl_form_setter set, void *context)
{
    bool changed = false;
    if (!field->fits) {
        return CTL_SAVE_REFUSED_BAD_VALUE;
    }
    ctl_setting_status status = set(context, name, field->value, &changed);
    return result_of(status, changed);
}

bool ctl_form_apply(const ctl_form *form, const ctl_settings *settings, ctl_form_setter set,
                    void *context, ctl_form_report *report)
{
    char name[CTL_SETTINGS_NAME_MAX];
    bool any_changed = false;
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
        ctl_save_result result = apply_field(&form->field[index], name, set, context);
        report_name(report, name, result);
        any_changed = any_changed || result == CTL_SAVE_SAVED;
    }
    return any_changed;
}

void ctl_form_report_start(ctl_form_report *report)
{
    report->count = 0;
}

ctl_save_result ctl_form_report_result(const ctl_form_report *report, const char *name)
{
    for (size_t i = 0; i < report->count && i < CTL_FORM_FIELDS_MAX; i++) {
        if (strcmp(report->name[i], name) == 0) {
            return (ctl_save_result)report->result[i];
        }
    }
    return CTL_SAVE_UNCHANGED;
}

static const char *last_save_text(ctl_save_result result)
{
    switch (result) {
    case CTL_SAVE_SAVED:
        return "saved";
    case CTL_SAVE_REFUSED_BAD_VALUE:
        return "refused: not a valid value";
    case CTL_SAVE_REFUSED_INVALID_SET:
        return "refused: conflicts with other settings";
    case CTL_SAVE_REFUSED_UNKNOWN:
        return "refused: unknown setting";
    case CTL_SAVE_UNCHANGED:
    default:
        return "";
    }
}

bool ctl_form_row(const ctl_settings *settings, const ctl_form_report *report, size_t index,
                  bool editable, char *out, size_t size)
{
    char name[CTL_SETTINGS_NAME_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    char description[CTL_SETTINGS_DESCRIPTION_MAX];
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!ctl_settings_name_at(settings, index, name, sizeof name)
        || !ctl_settings_get(settings, name, value, sizeof value)
        || !ctl_settings_describe(settings, name, description, sizeof description)) {
        return true;
    }
    ctl_save_result result = ctl_form_report_result(report, name);
    ctl_html_setting row;
    row.name = name;
    row.value = editable ? value : NULL;
    row.description = description;
    row.last_save = last_save_text(result);
    row.refused = result != CTL_SAVE_UNCHANGED && result != CTL_SAVE_SAVED;
    return ctl_html_setting_row(&row, out, size);
}
