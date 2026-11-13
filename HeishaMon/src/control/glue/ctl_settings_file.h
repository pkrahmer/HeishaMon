/*
 * The settings file: /control.json as a flat JSON object of names and text values. The reader
 * takes the file one character at a time, so that the board never holds the whole file, and
 * hands each text pair to the setter as it ends; a name the settings do not list is refused like
 * any unknown name, so a file an older firmware wrote still loads. The writer gives the file in
 * pieces, one per setting. It is pure: the caller reads and writes the file.
 */
#ifndef CTL_SETTINGS_FILE_H
#define CTL_SETTINGS_FILE_H

#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>

/* The longest piece the writer gives, with its zero: a comma, a name and a value, each of the two
 * in quotes and with every character escaped, and the colon between them. */
#define CTL_SETTINGS_FILE_PIECE_MAX (2 * CTL_SETTINGS_NAME_MAX + 2 * CTL_SETTINGS_VALUE_MAX + 8)

typedef enum {
    CTL_SETTINGS_FILE_NOTHING, /* no pair ended at this character */
    CTL_SETTINGS_FILE_TEXT,    /* a pair with a text value ended and went to the setter */
    CTL_SETTINGS_FILE_NOT_TEXT /* a pair with any other value ended and was skipped */
} ctl_settings_file_event;

typedef struct {
    int state;
    size_t depth;    /* the brackets open inside a value that is skipped */
    bool in_string;  /* inside a string of a value that is skipped */
    bool escaped;    /* the character before was a backslash */
    bool broken;     /* the string read holds an escape the settings never write, or is too long */
    size_t length;
    char text[CTL_SETTINGS_VALUE_MAX];
    char name[CTL_SETTINGS_NAME_MAX]; /* the name of the last pair */
    bool name_broken;
    ctl_setting_status status; /* the setter's answer to the last text pair */
} ctl_settings_file;

/* Starts reading a file. Refuses nothing. */
void ctl_settings_file_start(ctl_settings_file *file);

/* Reads one character. When it ends a pair with a text value, sets that setting with
 * ctl_settings_set, keeps the name and the setter's answer in the reader and returns TEXT; a name
 * with an escape other than \" \\ \/ or of CTL_SETTINGS_NAME_MAX characters or more is UNKNOWN
 * without the setter, and such a value BAD_VALUE. Returns NOT_TEXT, with the name kept, when it
 * ends a pair with any other value, and NOTHING otherwise. Once the text is no JSON object of
 * pairs, or after its closing brace, it reads nothing more and returns NOTHING. */
ctl_settings_file_event ctl_settings_file_feed(ctl_settings_file *file, ctl_settings *settings,
                                               char c);

/* Returns true once the closing brace of the object has been read; false while it has not and
 * when the text is no JSON object of pairs. */
bool ctl_settings_file_complete(const ctl_settings_file *file);

/* Returns the number of pieces the file is written in: an opening, one per listed name and a
 * closing. */
size_t ctl_settings_file_pieces(const ctl_settings *settings);

/* Writes the piece at index and returns true: "{", then "name":"value" for each name in the order
 * of ctl_settings_name_at, each after the first led by a comma, then "}" and a line end. Refuses,
 * returning false, an index not below ctl_settings_file_pieces and a size that cannot hold the
 * piece with its terminating zero. */
bool ctl_settings_file_piece(const ctl_settings *settings, size_t index, char *out, size_t size);

#endif
