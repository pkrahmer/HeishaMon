/*
 * The save of the control page: the fields of one posted form, kept until the whole body has
 * arrived, then applied in the order of the settings list, so that `rooms` comes before the
 * fields of its rooms whatever order the browser sends; and the rows of the settings table, each
 * with what the last save did to its setting, beside the field it is about. It is pure: the
 * caller hands it each piece the web server parsed and a setter that changes the settings, and it
 * touches nothing else.
 */
#ifndef CTL_FORM_H
#define CTL_FORM_H

#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* What a save did to one setting. */
typedef enum {
    CTL_SAVE_UNCHANGED, /* sent with the value it had, or not sent */
    CTL_SAVE_SAVED,     /* the value changed */
    CTL_SAVE_REFUSED_BAD_VALUE,
    CTL_SAVE_REFUSED_INVALID_SET,
    CTL_SAVE_REFUSED_UNKNOWN
} ctl_save_result;

/* An entry keeps its setting's index in ctl_settings_name_at rather than its name, to spare RAM:
 * the report lives as long as the firmware. The indexes hold while the settings list keeps the
 * count of names it had after the save; once that count differs the report shows no result. */
typedef struct {
    size_t count;
    const ctl_settings *settings; /* the settings the save changed, or NULL before any save */
    uint8_t names;                /* ctl_settings_name_count of settings after the save */
    uint8_t setting[CTL_FORM_FIELDS_MAX]; /* an index of ctl_settings_name_at */
    uint8_t result[CTL_FORM_FIELDS_MAX];  /* a ctl_save_result, a byte each to spare RAM */
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
 * lists its rooms' fields before they are reached. A field that does not fit is refused as a bad
 * value without calling set; a field whose name is not listed is ignored. Writes every applied
 * name to the report with its result: saved when set answered OK and the value changed, unchanged
 * when it answered OK alone, and otherwise refused with set's reason. Returns true when any
 * setting changed. Refuses nothing. */
bool ctl_form_apply(const ctl_form *form, const ctl_settings *settings, ctl_form_setter set,
                    void *context, ctl_form_report *report);

/* Empties the report: no save to show. Refuses nothing. */
void ctl_form_report_start(ctl_form_report *report);

/* Returns what the report's save did to the setting called name: unchanged for a name it does
 * not hold, and for every name once the count of names differs from the one after the save.
 * Refuses nothing. */
ctl_save_result ctl_form_report_result(const ctl_form_report *report, const char *name);

/* Writes the settings table's row of the name at index of ctl_settings_name_at: the name, its
 * field holding the value in force (or, when editable is false, CTL_HTML_TOO_LONG and no field),
 * and what the report's save did to it, a refusal with its reason; and returns true. An index
 * not listed, or a name without a value, writes an empty text and returns true. Refuses,
 * returning false, a size that cannot hold the row with its terminating zero. */
bool ctl_form_row(const ctl_settings *settings, const ctl_form_report *report, size_t index,
                  bool editable, char *out, size_t size);

#endif
