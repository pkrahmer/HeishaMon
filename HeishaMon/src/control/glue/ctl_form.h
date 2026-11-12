/*
 * The save of the control page: the fields of one posted form, kept until the whole body has
 * arrived, then applied in the order of the settings list, so that `rooms` comes before the
 * fields of its rooms whatever order the browser sends; and the report of what the last save
 * accepted and refused, as the page shows it. It is pure: the caller hands it each piece the web
 * server parsed and a setter that changes the settings, and it touches nothing else.
 */
#ifndef CTL_FORM_H
#define CTL_FORM_H

#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>

#define CTL_FORM_FIELDS_MAX CTL_SETTINGS_NAMES_MAX /* a form holds no more fields than names */

typedef struct {
    char name[CTL_SETTINGS_NAME_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t length;
    bool fits; /* false: the value was longer than any setting takes, or held a zero byte */
} ctl_form_field;

typedef struct {
    size_t count;
    size_t last; /* the field the previous piece went to, or CTL_FORM_FIELDS_MAX */
    ctl_form_field field[CTL_FORM_FIELDS_MAX];
} ctl_form;

typedef struct {
    bool saved; /* a save has been applied since start */
    size_t count;
    char name[CTL_FORM_FIELDS_MAX][CTL_SETTINGS_NAME_MAX];
    bool accepted[CTL_FORM_FIELDS_MAX];
} ctl_form_report;

/* Changes the setting called name to value and returns the setter's answer; sets *changed when
 * the setting's value text differs afterwards. The context is the caller's. */
typedef ctl_setting_status (*ctl_form_setter)(void *context, const char *name, const char *value,
                                              bool *changed);

/* Empties the form. Refuses nothing. */
void ctl_form_start(ctl_form *form);

/* Keeps one piece of a posted field: length bytes of value under name. A piece under the name of
 * the piece before continues that field's value, since a web server hands a long value over in
 * pieces; any other piece starts the field's value again. A field whose joined value reaches
 * CTL_SETTINGS_VALUE_MAX or holds a zero byte is marked as not fitting and stays so. Refuses,
 * dropping the piece, a NULL name, a name that cannot be a setting's and a new name once the form
 * is full. A NULL value is an empty piece. */
void ctl_form_keep(ctl_form *form, const char *name, const char *value, size_t length);

/* Applies every field whose name is listed in settings with set, in the order of
 * ctl_settings_name_at, reading the count of names again after each, so that a raised `rooms`
 * lists its rooms' fields before they are reached. A field that does not fit is refused without
 * calling set; a field whose name is not listed is ignored. Writes every applied name to the
 * report, accepted when set answered OK, and marks it saved. Returns true when any setting
 * changed. Refuses nothing. */
bool ctl_form_apply(const ctl_form *form, const ctl_settings *settings, ctl_form_setter set,
                    void *context, ctl_form_report *report);

/* Empties the report: no save yet. Refuses nothing. */
void ctl_form_report_start(ctl_form_report *report);

/* Returns the number of pieces of HTML the report shows: none before a save, one line after a
 * save that held no setting, and otherwise an opening, one row per name and a closing. */
size_t ctl_form_report_items(const ctl_form_report *report);

/* Writes the report's piece at index, rows as <tr><td>NAME</td><td>accepted</td></tr> or
 * refused, and returns true. Refuses, returning false, an index not below
 * ctl_form_report_items and a size that cannot hold the piece with its terminating zero. */
bool ctl_form_report_item(const ctl_form_report *report, size_t index, char *out, size_t size);

#endif
