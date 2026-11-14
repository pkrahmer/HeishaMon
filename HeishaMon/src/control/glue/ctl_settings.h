/*
 * The settings: everything the owner can set, as one flat set of names with a text value each, so
 * that the settings file, the MQTT setting topics and a web page all go through the same three
 * doors: one setter that refuses a bad value and keeps the old one, one getter that writes any
 * value back as text, and one function that turns the settings into the loop's configuration.
 * It is pure: it reads and writes no file and no topic.
 */
#ifndef CTL_SETTINGS_H
#define CTL_SETTINGS_H

#include "../adapt/ctl_rooms.h"
#include "../loop/ctl_loop.h"

#include <stdbool.h>
#include <stddef.h>

#define CTL_SETTINGS_TOPIC_MAX 96  /* the longest topic, without its terminating zero */
#define CTL_SETTINGS_VALUE_MAX 100 /* the longest value text any setting gives, with its zero */
#define CTL_SETTINGS_NAME_MAX 32   /* the longest name, with its zero */
#define CTL_SETTINGS_FIXED_NAMES 21 /* the names listed whatever the count of rooms */
/* The most names the list ever holds, for a caller that keeps one entry per setting. */
#define CTL_SETTINGS_NAMES_MAX (CTL_SETTINGS_FIXED_NAMES + 2 * CTL_ROOMS_MAX)

typedef struct {
    bool enabled; /* commands reach the pump; default false */
    bool cooling; /* the core may ask for cooling */
    size_t room_count;
    char room_topic[CTL_ROOMS_MAX][CTL_SETTINGS_TOPIC_MAX + 1]; /* empty: never receives a value */
    double room_weight[CTL_ROOMS_MAX];
    double room_timeout_s;
    char pv_topic[CTL_SETTINGS_TOPIC_MAX + 1]; /* empty: no PV */
    /* Another HeishaMon's log, whose received frames this board decodes; empty: none. */
    char frame_log_topic[CTL_SETTINGS_TOPIC_MAX + 1];
    double desired_indoor_c;
    double transmission_w_per_k;
    double adjustment_w_per_k;
    double hysteresis_wh;
    bool trim_limit_enabled;
    double trim_limit_k;
    double integral_w_per_kh;
    double integral_limit_w;
    double solar_gain_factor;
    double internal_w; /* the core's fallback for P_INTERNAL */
    int heat_cool_heat_value;
    double min_change_interval_s;
    double retry_interval_s;
    int max_retries;
    bool smart_grid; /* the router sends Smart Grid mode instead of the thermostat state */
    /* The base under which another HeishaMon takes commands, as <base>/commands; empty: the
     * commands go to this board's serial port. */
    char command_topic[CTL_SETTINGS_TOPIC_MAX + 1];
} ctl_settings;

typedef enum {
    CTL_SETTING_OK,
    CTL_SETTING_UNKNOWN,     /* no such name, or a room at or above the count */
    CTL_SETTING_BAD_VALUE,   /* the text does not parse or breaks its own setting's rule */
    CTL_SETTING_INVALID_SET  /* the value is fine alone, but the loop would refuse the whole set */
} ctl_setting_status;

/* Writes every default: commands disabled, no cooling, no rooms (every room slot with an empty
 * topic and weight 1), a room timeout of 1800 s, no PV, no frame log topic, no
 * command topic, and the core's and the router's defaults, the thermostat rather than Smart Grid
 * mode among them. The result is valid. Refuses nothing. */
void ctl_settings_defaults(ctl_settings *settings);

/* Parses value for the setting called name and applies it when the whole set stays valid.
 * Returns OK when it applied it; UNKNOWN for a name that is not listed (a room index not below
 * room_count included) and for a NULL name; BAD_VALUE for a NULL value, a text that does not parse
 * or breaks the setting's own rule; INVALID_SET when ctl_settings_valid refuses the result. Every
 * result but OK leaves the settings as they were. */
ctl_setting_status ctl_settings_set(ctl_settings *settings, const char *name, const char *value);

/* Writes the value of the setting called name as text, the form ctl_settings_set takes, and
 * returns true. Refuses, returning false, a name that is not listed and a size that cannot hold
 * the text with its terminating zero; the buffer then holds nothing to rely on. */
bool ctl_settings_get(const ctl_settings *settings, const char *name, char *value, size_t size);

/* Returns the number of names listed: CTL_SETTINGS_FIXED_NAMES, and two more for each room below
 * room_count; never more than CTL_SETTINGS_NAMES_MAX. */
size_t ctl_settings_name_count(const ctl_settings *settings);

/* Writes the name at index of the list and returns true. Refuses, returning false, an index not
 * below ctl_settings_name_count and a size that cannot hold the name with its terminating zero. */
bool ctl_settings_name_at(const ctl_settings *settings, size_t index, char *name, size_t size);

/* Returns true when the configuration ctl_settings_to_loop makes passes ctl_loop_config_valid. */
bool ctl_settings_valid(const ctl_settings *settings);

/* Writes the loop's configuration: the loop's defaults for the rooms' count, with the core's
 * tuning, the room weights and timeout, the router's pacing, polarity and Smart Grid mode, the
 * allowed demands
 * and no assumed landing from the settings. No rooms gives one room of weight 1 that never
 * receives a value. Refuses nothing; whether the result is usable is ctl_settings_valid's to
 * say. */
void ctl_settings_to_loop(const ctl_settings *settings, ctl_loop_config *config);

#endif
