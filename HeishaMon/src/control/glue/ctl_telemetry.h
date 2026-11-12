/*
 * The telemetry: every input, status, output and setting as a subtopic and a value text, so that
 * the owner can see what the loop saw, what it decided and why, and what it sent. The glue adds
 * the topic prefix and publishes them; nothing reads them back. The entries are given one at a
 * time, by index, so that the glue never holds all of them at once.
 */
#ifndef CTL_TELEMETRY_H
#define CTL_TELEMETRY_H

#include "../adapt/ctl_rooms.h"
#include "../loop/ctl_loop.h"
#include "ctl_gate.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTL_TELEMETRY_SUBTOPIC_MAX 48 /* with its terminating zero */

typedef enum { CTL_GROUP_INPUTS, CTL_GROUP_STATUS, CTL_GROUP_OUTPUTS } ctl_group;

typedef struct {
    const ctl_loop_record *record;
    const ctl_rooms *rooms;
    const ctl_rooms_config *rooms_config;
    double uptime_s;
    bool enabled;
    ctl_gate gate; /* why commands are blocked, or OPEN */
    uint32_t frames_decoded; /* frames from another HeishaMon's log handed to the decoder */
    uint32_t frames_dropped; /* frames from that log cut short, corrupt or of no use */
} ctl_telemetry_source;

/* Returns the number of entries of the group: 9 inputs, 3 more for each configured room (up to
 * CTL_ROOMS_MAX) and the 2 frame counters after them, 10 status entries and 4 outputs; 0 for a
 * group that is no member. */
size_t ctl_telemetry_count(const ctl_telemetry_source *source, ctl_group group);

/* Writes the subtopic and the value text of the group's entry at index and returns true.
 * Refuses, returning false and writing nothing, an index not below ctl_telemetry_count, a size
 * that cannot hold the subtopic or the value with its terminating zero, and a number that
 * ctl_format_fixed refuses (a magnitude of CTL_FORMAT_MAGNITUDE_LIMIT or more): a text is never
 * cut. */
bool ctl_telemetry_entry(const ctl_telemetry_source *source, ctl_group group, size_t index,
                         char *subtopic, size_t subtopic_size, char *value, size_t value_size);

/* Writes config/<name> for the setting name at index of ctl_settings_name_at, and its value
 * text from ctl_settings_get, and returns true. Refuses, returning false and writing nothing, an
 * index not below ctl_settings_name_count and a size that cannot hold the subtopic or the value
 * with its terminating zero. */
bool ctl_telemetry_setting(const ctl_settings *settings, size_t index,
                           char *subtopic, size_t subtopic_size, char *value, size_t value_size);

#endif
