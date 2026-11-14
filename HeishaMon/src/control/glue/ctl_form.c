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

/* An index of the settings list must fit the byte a report entry keeps of it. */
typedef char ctl_form_names_fit_a_byte[CTL_SETTINGS_NAMES_MAX <= UINT8_MAX ? 1 : -1];

static void report_setting(ctl_form_report *report, size_t index, ctl_save_result result)
{
    if (report->count < CTL_FORM_FIELDS_MAX) {
        report->setting[report->count] = (uint8_t)index;
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
    report->settings = settings;
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
        report_setting(report, i, result);
        any_changed = any_changed || result == CTL_SAVE_SAVED;
    }
    // Only `rooms` moves the names after it, and it is applied before them, so every index
    // kept above already names its setting in the list as the save left it.
    report->names = (uint8_t)ctl_settings_name_count(settings);
    return any_changed;
}

void ctl_form_report_start(ctl_form_report *report)
{
    report->count = 0;
    report->settings = NULL;
    report->names = 0;
}

static bool report_current(const ctl_form_report *report)
{
    return report->settings != NULL
           && ctl_settings_name_count(report->settings) == report->names;
}

static ctl_save_result report_result_at(const ctl_form_report *report, size_t index)
{
    if (!report_current(report)) {
        return CTL_SAVE_UNCHANGED;
    }
    for (size_t i = 0; i < report->count && i < CTL_FORM_FIELDS_MAX; i++) {
        if (report->setting[i] == index) {
            return (ctl_save_result)report->result[i];
        }
    }
    return CTL_SAVE_UNCHANGED;
}

ctl_save_result ctl_form_report_result(const ctl_form_report *report, const char *name)
{
    char listed[CTL_SETTINGS_NAME_MAX];
    if (!report_current(report)) {
        return CTL_SAVE_UNCHANGED;
    }
    for (size_t i = 0; i < report->count && i < CTL_FORM_FIELDS_MAX; i++) {
        if (ctl_settings_name_at(report->settings, report->setting[i], listed, sizeof listed)
            && strcmp(listed, name) == 0) {
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
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!ctl_settings_name_at(settings, index, name, sizeof name)
        || !ctl_settings_get(settings, name, value, sizeof value)) {
        return true;
    }
    ctl_save_result result = report_result_at(report, index);
    ctl_html_setting row;
    row.name = name;
    row.value = editable ? value : NULL;
    row.last_save = last_save_text(result);
    row.refused = result != CTL_SAVE_UNCHANGED && result != CTL_SAVE_SAVED;
    return ctl_html_setting_row(&row, out, size);
}
