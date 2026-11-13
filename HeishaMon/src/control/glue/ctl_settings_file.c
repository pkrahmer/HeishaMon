#include "ctl_settings_file.h"

#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum {
    STATE_OPEN,        /* before the opening brace */
    STATE_FIRST_NAME,  /* after the opening brace: a name or the closing brace */
    STATE_NAME_START,  /* after a comma: a name */
    STATE_NAME,        /* inside a name */
    STATE_COLON,       /* after a name */
    STATE_VALUE_START, /* after the colon */
    STATE_TEXT,        /* inside a text value */
    STATE_SKIP_NESTED, /* inside an object or an array value */
    STATE_SKIP_SCALAR, /* inside a number, true, false or null */
    STATE_AFTER_VALUE, /* a comma or the closing brace */
    STATE_COMPLETE,
    STATE_FAILED
};

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void start_string(ctl_settings_file *file)
{
    file->length = 0;
    file->text[0] = '\0';
    file->broken = false;
    file->escaped = false;
}

/* Adds one character of a string; returns true when it closed the string. */
static bool string_char(ctl_settings_file *file, char c, size_t limit)
{
    if (file->escaped) {
        file->escaped = false;
        if (c != '"' && c != '\\' && c != '/') {
            file->broken = true;
            return false;
        }
    } else if (c == '\\') {
        file->escaped = true;
        return false;
    } else if (c == '"') {
        return true;
    }
    if (c == '\0' || file->length + 1 >= limit) {
        file->broken = true;
        return false;
    }
    file->text[file->length++] = c;
    file->text[file->length] = '\0';
    return false;
}

static void end_name(ctl_settings_file *file)
{
    memcpy(file->name, file->text, file->length + 1);
    file->name_broken = file->broken;
    file->state = STATE_COLON;
}

static ctl_settings_file_event end_text(ctl_settings_file *file, ctl_settings *settings)
{
    if (file->name_broken) {
        file->status = CTL_SETTING_UNKNOWN;
    } else if (file->broken) {
        file->status = CTL_SETTING_BAD_VALUE;
    } else {
        file->status = ctl_settings_set(settings, file->name, file->text);
    }
    file->state = STATE_AFTER_VALUE;
    return CTL_SETTINGS_FILE_TEXT;
}

static void start_value(ctl_settings_file *file, char c)
{
    if (c == '"') {
        start_string(file);
        file->state = STATE_TEXT;
    } else if (c == '{' || c == '[') {
        file->depth = 1;
        file->in_string = false;
        file->escaped = false;
        file->state = STATE_SKIP_NESTED;
    } else if (c == ',' || c == '}' || c == ']' || c == ':') {
        file->state = STATE_FAILED;
    } else {
        file->state = STATE_SKIP_SCALAR;
    }
}

/* Brackets inside a string of the skipped value do not count. */
static ctl_settings_file_event skip_nested(ctl_settings_file *file, char c)
{
    if (file->in_string) {
        if (file->escaped) {
            file->escaped = false;
        } else if (c == '\\') {
            file->escaped = true;
        } else if (c == '"') {
            file->in_string = false;
        }
        return CTL_SETTINGS_FILE_NOTHING;
    }
    if (c == '"') {
        file->in_string = true;
    } else if (c == '{' || c == '[') {
        file->depth++;
    } else if (c == '}' || c == ']') {
        file->depth--;
        if (file->depth == 0) {
            file->state = STATE_AFTER_VALUE;
            return CTL_SETTINGS_FILE_NOT_TEXT;
        }
    }
    return CTL_SETTINGS_FILE_NOTHING;
}

static void after_value(ctl_settings_file *file, char c)
{
    if (c == ',') {
        file->state = STATE_NAME_START;
    } else if (c == '}') {
        file->state = STATE_COMPLETE;
    } else if (!is_space(c)) {
        file->state = STATE_FAILED;
    }
}

/* A scalar ends at the first character that cannot be part of it, which then counts as read
 * after the value. */
static ctl_settings_file_event skip_scalar(ctl_settings_file *file, char c)
{
    if (c != ',' && c != '}' && !is_space(c)) {
        return CTL_SETTINGS_FILE_NOTHING;
    }
    file->state = STATE_AFTER_VALUE;
    after_value(file, c);
    return CTL_SETTINGS_FILE_NOT_TEXT;
}

/* Where only white space and one expected character may come. */
static void expect(ctl_settings_file *file, char c, char wanted, int next)
{
    if (c == wanted) {
        file->state = next;
    } else if (!is_space(c)) {
        file->state = STATE_FAILED;
    }
}

void ctl_settings_file_start(ctl_settings_file *file)
{
    file->state = STATE_OPEN;
    file->depth = 0;
    file->in_string = false;
    file->name[0] = '\0';
    file->name_broken = false;
    file->status = CTL_SETTING_OK;
    start_string(file);
}

ctl_settings_file_event ctl_settings_file_feed(ctl_settings_file *file, ctl_settings *settings,
                                               char c)
{
    switch (file->state) {
    case STATE_OPEN:
        expect(file, c, '{', STATE_FIRST_NAME);
        break;
    case STATE_FIRST_NAME:
        if (c == '}') {
            file->state = STATE_COMPLETE;
            break;
        }
        start_string(file);
        expect(file, c, '"', STATE_NAME);
        break;
    case STATE_NAME_START:
        start_string(file);
        expect(file, c, '"', STATE_NAME);
        break;
    case STATE_NAME:
        if (string_char(file, c, CTL_SETTINGS_NAME_MAX)) {
            end_name(file);
        }
        break;
    case STATE_COLON:
        expect(file, c, ':', STATE_VALUE_START);
        break;
    case STATE_VALUE_START:
        if (!is_space(c)) {
            start_value(file, c);
        }
        break;
    case STATE_TEXT:
        if (string_char(file, c, CTL_SETTINGS_VALUE_MAX)) {
            return end_text(file, settings);
        }
        break;
    case STATE_SKIP_NESTED:
        return skip_nested(file, c);
    case STATE_SKIP_SCALAR:
        return skip_scalar(file, c);
    case STATE_AFTER_VALUE:
        after_value(file, c);
        break;
    default:
        break;
    }
    return CTL_SETTINGS_FILE_NOTHING;
}

bool ctl_settings_file_complete(const ctl_settings_file *file)
{
    return file->state == STATE_COMPLETE;
}

size_t ctl_settings_file_pieces(const ctl_settings *settings)
{
    return ctl_settings_name_count(settings) + 2u;
}

/* Writes text in quotes after out[*length]; a value is printable ASCII, so a quote and a
 * backslash are all that need escaping. Returns false when it does not fit with a zero after it. */
static bool append_quoted(const char *text, char *out, size_t size, size_t *length)
{
    size_t i;
    if (*length + 1 >= size) {
        return false;
    }
    out[(*length)++] = '"';
    for (i = 0; text[i] != '\0'; i++) {
        const bool escape = text[i] == '"' || text[i] == '\\';
        if (*length + (escape ? 2u : 1u) >= size) {
            return false;
        }
        if (escape) {
            out[(*length)++] = '\\';
        }
        out[(*length)++] = text[i];
    }
    if (*length + 1 >= size) {
        return false;
    }
    out[(*length)++] = '"';
    out[*length] = '\0';
    return true;
}

static bool copy_piece(const char *piece, char *out, size_t size)
{
    if (strlen(piece) >= size) {
        return false;
    }
    strcpy(out, piece);
    return true;
}

static bool write_pair(const ctl_settings *settings, size_t name_index, char *out, size_t size)
{
    char name[CTL_SETTINGS_NAME_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    size_t length = 0;
    if (!ctl_settings_name_at(settings, name_index, name, sizeof name) ||
        !ctl_settings_get(settings, name, value, sizeof value)) {
        return false;
    }
    if (name_index > 0) {
        if (size < 2) {
            return false;
        }
        out[length++] = ',';
    }
    if (!append_quoted(name, out, size, &length) || length + 1 >= size) {
        return false;
    }
    out[length++] = ':';
    return append_quoted(value, out, size, &length);
}

bool ctl_settings_file_piece(const ctl_settings *settings, size_t index, char *out, size_t size)
{
    const size_t count = ctl_settings_file_pieces(settings);
    if (out == NULL || size == 0 || index >= count) {
        return false;
    }
    if (index == 0) {
        return copy_piece("{", out, size);
    }
    if (index == count - 1) {
        return copy_piece("}\n", out, size);
    }
    return write_pair(settings, index - 1, out, size);
}
