#include "ctl_settings.h"

#include "../adapt/ctl_bounds.h"
#include "../adapt/ctl_payload.h"
#include "../adapt/ctl_rooms.h"
#include "../core/ctl_config.h"
#include "../core/ctl_relay.h"
#include "../core/ctl_types.h"
#include "../loop/ctl_format.h"
#include "../loop/ctl_loop.h"
#include "../route/ctl_router.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define NUMBER_DECIMALS 3
#define TRIM_LIMIT_NONE "none"
#define ROOM_PREFIX "room."
#define ROOM_TOPIC_SUFFIX ".topic"
#define ROOM_WEIGHT_SUFFIX ".weight"
/* The fixed names before the rooms' names in the list: enabled, cooling, rooms. */
#define NAMES_BEFORE_ROOMS 3

typedef enum {
    SETTING_ENABLED,
    SETTING_COOLING,
    SETTING_ROOMS,
    SETTING_ROOM_TOPIC,
    SETTING_ROOM_WEIGHT,
    SETTING_ROOM_TIMEOUT,
    SETTING_PV_TOPIC,
    SETTING_FRAME_LOG_TOPIC,
    SETTING_DESIRED_INDOOR,
    SETTING_TRANSMISSION,
    SETTING_ADJUSTMENT,
    SETTING_HYSTERESIS,
    SETTING_TRIM_LIMIT,
    SETTING_SOLAR_GAIN,
    SETTING_INTERNAL,
    SETTING_HEAT_COOL,
    SETTING_MIN_CHANGE_INTERVAL,
    SETTING_RETRY_INTERVAL,
    SETTING_MAX_RETRIES,
    SETTING_SMART_GRID,
    SETTING_COMMAND_TOPIC
} setting_id;

typedef struct {
    const char *name;
    setting_id id;
} fixed_name;

/* In the order of the list; the rooms' names come after the first NAMES_BEFORE_ROOMS. */
static const fixed_name FIXED_NAMES[] = {
    {"enabled", SETTING_ENABLED},
    {"cooling", SETTING_COOLING},
    {"rooms", SETTING_ROOMS},
    {"room_timeout_s", SETTING_ROOM_TIMEOUT},
    {"pv_topic", SETTING_PV_TOPIC},
    {"frame_log_topic", SETTING_FRAME_LOG_TOPIC},
    {"desired_indoor_c", SETTING_DESIRED_INDOOR},
    {"transmission_w_per_k", SETTING_TRANSMISSION},
    {"adjustment_w_per_k", SETTING_ADJUSTMENT},
    {"hysteresis_wh", SETTING_HYSTERESIS},
    {"trim_limit_k", SETTING_TRIM_LIMIT},
    {"solar_gain_factor", SETTING_SOLAR_GAIN},
    {"internal_w", SETTING_INTERNAL},
    {"heat_cool_heat_value", SETTING_HEAT_COOL},
    {"min_change_interval_s", SETTING_MIN_CHANGE_INTERVAL},
    {"retry_interval_s", SETTING_RETRY_INTERVAL},
    {"max_retries", SETTING_MAX_RETRIES},
    {"smart_grid", SETTING_SMART_GRID},
    {"command_topic", SETTING_COMMAND_TOPIC},
};

#define FIXED_NAME_COUNT (sizeof FIXED_NAMES / sizeof FIXED_NAMES[0])

/* A caller sizes its lists by the header's count, so the build stops when the two part ways. */
typedef char fixed_names_match_header[FIXED_NAME_COUNT == CTL_SETTINGS_FIXED_NAMES ? 1 : -1];

/* A name resolved: the setting and, for a room's setting, the room. */
typedef struct {
    setting_id id;
    size_t room;
} setting_key;

/* ---- Text ---------------------------------------------------------------------------------- */

/* The length of text, counted no further than limit, so that a text without its zero is never
 * read past the limit. */
static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
        length++;
    }
    return length;
}

static bool copy_text(const char *source, char *text, size_t size)
{
    const size_t length = bounded_length(source, size);
    if (length >= size) {
        return false;
    }
    memcpy(text, source, length + 1);
    return true;
}

/* Writes the decimal digits of number after text[*length]; returns false when they do not fit
 * with a zero after them. */
static bool append_decimal(size_t number, char *text, size_t size, size_t *length)
{
    char digits[24];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number > 0u && count < sizeof digits);
    if (*length + count >= size) {
        return false;
    }
    while (count > 0u) {
        text[(*length)++] = digits[--count];
    }
    text[*length] = '\0';
    return true;
}

static bool append_text(const char *source, char *text, size_t size, size_t *length)
{
    if (*length >= size || !copy_text(source, text + *length, size - *length)) {
        return false;
    }
    *length += strlen(text + *length);
    return true;
}

/* ---- Numbers ------------------------------------------------------------------------------- */

/* A whole number with no decimals, any other with three, and the zeros a fraction ends with cut,
 * with the point when nothing follows it: 1500, 0.02, 22.5. */
static bool format_number(double value, char *text, size_t size)
{
    char fixed[CTL_FORMAT_TEXT_MAX];
    const int decimals = value == floor(value) ? 0 : NUMBER_DECIMALS;
    size_t length;
    if (!ctl_format_fixed(value, decimals, fixed, sizeof fixed)) {
        return false;
    }
    length = strlen(fixed);
    if (decimals > 0) {
        while (fixed[length - 1] == '0') {
            length--;
        }
        if (fixed[length - 1] == '.') {
            length--;
        }
        fixed[length] = '\0';
    }
    return copy_text(fixed, text, size);
}

static bool parse_bare_number(const char *text, double *value)
{
    const size_t length = bounded_length(text, CTL_PAYLOAD_MAX + 1);
    ctl_payload payload = {0.0, false, 0};
    /* The payload parser also takes a JSON object; a setting is a bare number only. */
    if (memchr(text, '{', length) != NULL || !ctl_payload_parse(text, length, &payload)) {
        return false;
    }
    *value = payload.value;
    return true;
}

/* A number the getter can write back exactly: below the format's limit and with no more than
 * three decimals, so that a value saved through the getter reads back as the same value. */
static bool parse_number(const char *text, double *value)
{
    char written[CTL_FORMAT_TEXT_MAX];
    double parsed = 0.0;
    double reread = 0.0;
    if (!parse_bare_number(text, &parsed) || fabs(parsed) >= CTL_FORMAT_MAGNITUDE_LIMIT ||
        !format_number(parsed, written, sizeof written) || !parse_bare_number(written, &reread) ||
        reread != parsed) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_whole(const char *text, int minimum, int maximum, int *value)
{
    double parsed = 0.0;
    if (!parse_bare_number(text, &parsed) || parsed != floor(parsed) || parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_flag(const char *text, bool *flag)
{
    int value = 0;
    if (!parse_whole(text, 0, 1, &value)) {
        return false;
    }
    *flag = value == 1;
    return true;
}

static bool format_whole(int value, char *text, size_t size)
{
    return format_number((double)value, text, size);
}

/* ---- Topics -------------------------------------------------------------------------------- */

/* Printable ASCII without a space, and without the wildcards a subscription would read as
 * matching other topics. */
static bool is_topic_character(char c)
{
    return c >= 0x21 && c <= 0x7e && c != '+' && c != '#';
}

static bool parse_topic(const char *text, char *topic)
{
    const size_t length = bounded_length(text, CTL_SETTINGS_TOPIC_MAX + 1);
    size_t i;
    if (length > CTL_SETTINGS_TOPIC_MAX) {
        return false;
    }
    for (i = 0; i < length; i++) {
        if (!is_topic_character(text[i])) {
            return false;
        }
    }
    memcpy(topic, text, length + 1);
    return true;
}

/* ---- Names --------------------------------------------------------------------------------- */

static bool starts_with(const char *text, const char *prefix, size_t prefix_length)
{
    return strncmp(text, prefix, prefix_length) == 0;
}

/* Reads a room index written as the list writes it: decimal digits without a leading zero. */
static bool read_room_index(const char **text, size_t *room)
{
    const char *at = *text;
    size_t value = 0;
    size_t digits = 0;
    while (*at >= '0' && *at <= '9') {
        if (digits > 0 && value == 0) {
            return false;
        }
        value = value * 10u + (size_t)(*at - '0');
        digits++;
        at++;
        if (value >= CTL_ROOMS_MAX) {
            return false;
        }
    }
    if (digits == 0) {
        return false;
    }
    *room = value;
    *text = at;
    return true;
}

static bool find_room_name(const ctl_settings *settings, const char *name, setting_key *key)
{
    const char *at = name;
    size_t room = 0;
    if (!starts_with(at, ROOM_PREFIX, strlen(ROOM_PREFIX))) {
        return false;
    }
    at += strlen(ROOM_PREFIX);
    if (!read_room_index(&at, &room) || room >= settings->room_count) {
        return false;
    }
    if (strcmp(at, ROOM_TOPIC_SUFFIX) == 0) {
        key->id = SETTING_ROOM_TOPIC;
    } else if (strcmp(at, ROOM_WEIGHT_SUFFIX) == 0) {
        key->id = SETTING_ROOM_WEIGHT;
    } else {
        return false;
    }
    key->room = room;
    return true;
}

static bool find_name(const ctl_settings *settings, const char *name, setting_key *key)
{
    size_t i;
    if (name == NULL || bounded_length(name, CTL_SETTINGS_NAME_MAX) >= CTL_SETTINGS_NAME_MAX) {
        return false;
    }
    for (i = 0; i < FIXED_NAME_COUNT; i++) {
        if (strcmp(name, FIXED_NAMES[i].name) == 0) {
            key->id = FIXED_NAMES[i].id;
            key->room = 0;
            return true;
        }
    }
    return find_room_name(settings, name, key);
}

static bool write_room_name(size_t room, const char *suffix, char *name, size_t size)
{
    size_t length = 0;
    return append_text(ROOM_PREFIX, name, size, &length) &&
           append_decimal(room, name, size, &length) && append_text(suffix, name, size, &length);
}

/* ---- Values -------------------------------------------------------------------------------- */

/* The text a topic setting keeps, or NULL for a setting that is no topic. */
static char *topic_field(ctl_settings *settings, setting_key key)
{
    switch (key.id) {
    case SETTING_ROOM_TOPIC:
        return settings->room_topic[key.room];
    case SETTING_PV_TOPIC:
        return settings->pv_topic;
    case SETTING_FRAME_LOG_TOPIC:
        return settings->frame_log_topic;
    case SETTING_COMMAND_TOPIC:
        return settings->command_topic;
    default:
        return NULL;
    }
}

/* The double a number setting keeps, or NULL for a setting that is no plain number. */
static double *number_field(ctl_settings *settings, setting_key key)
{
    switch (key.id) {
    case SETTING_ROOM_WEIGHT:
        return &settings->room_weight[key.room];
    case SETTING_ROOM_TIMEOUT:
        return &settings->room_timeout_s;
    case SETTING_DESIRED_INDOOR:
        return &settings->desired_indoor_c;
    case SETTING_TRANSMISSION:
        return &settings->transmission_w_per_k;
    case SETTING_ADJUSTMENT:
        return &settings->adjustment_w_per_k;
    case SETTING_HYSTERESIS:
        return &settings->hysteresis_wh;
    case SETTING_SOLAR_GAIN:
        return &settings->solar_gain_factor;
    case SETTING_INTERNAL:
        return &settings->internal_w;
    case SETTING_MIN_CHANGE_INTERVAL:
        return &settings->min_change_interval_s;
    case SETTING_RETRY_INTERVAL:
        return &settings->retry_interval_s;
    default:
        return NULL;
    }
}

/* A number setting's own rule, as the settings table gives it; the rules it does not give are
 * left to the core, the rooms and the router. */
static bool number_in_rule(setting_id id, double value)
{
    switch (id) {
    case SETTING_ROOM_WEIGHT:
        return ctl_bounds_accept(CTL_BOUND_WEIGHT, value);
    case SETTING_ROOM_TIMEOUT:
        return value > 0.0;
    default:
        return true;
    }
}

static bool apply_trim_limit(ctl_settings *settings, const char *value)
{
    double limit = 0.0;
    if (strcmp(value, TRIM_LIMIT_NONE) == 0) {
        settings->trim_limit_enabled = false;
        return true;
    }
    if (!parse_number(value, &limit) || !(limit > 0.0)) {
        return false;
    }
    settings->trim_limit_enabled = true;
    settings->trim_limit_k = limit;
    return true;
}

static bool apply_rooms(ctl_settings *settings, const char *value)
{
    int count = 0;
    if (!parse_whole(value, 0, CTL_ROOMS_MAX, &count)) {
        return false;
    }
    settings->room_count = (size_t)count;
    return true;
}

/* Applies the text to the setting; false when it does not parse or breaks the setting's rule. */
static bool apply_value(ctl_settings *settings, setting_key key, const char *value)
{
    double *number = number_field(settings, key);
    char *topic = topic_field(settings, key);
    if (topic != NULL) {
        return parse_topic(value, topic);
    }
    if (number != NULL) {
        double parsed = 0.0;
        if (!parse_number(value, &parsed) || !number_in_rule(key.id, parsed)) {
            return false;
        }
        *number = parsed;
        return true;
    }
    switch (key.id) {
    case SETTING_ENABLED:
        return parse_flag(value, &settings->enabled);
    case SETTING_COOLING:
        return parse_flag(value, &settings->cooling);
    case SETTING_ROOMS:
        return apply_rooms(settings, value);
    case SETTING_TRIM_LIMIT:
        return apply_trim_limit(settings, value);
    case SETTING_HEAT_COOL:
        return parse_whole(value, -1, 1, &settings->heat_cool_heat_value);
    case SETTING_MAX_RETRIES:
        /* Any count an int holds; its range is the router's to check. */
        return parse_whole(value, INT_MIN, INT_MAX, &settings->max_retries);
    case SETTING_SMART_GRID:
        return parse_flag(value, &settings->smart_grid);
    default:
        return false;
    }
}

static bool write_value(const ctl_settings *settings, setting_key key, char *value, size_t size)
{
    /* topic_field and number_field take no const settings; they only give the address read here. */
    const char *topic = topic_field((ctl_settings *)settings, key);
    if (topic != NULL) {
        return copy_text(topic, value, size);
    }
    switch (key.id) {
    case SETTING_ENABLED:
        return format_whole(settings->enabled ? 1 : 0, value, size);
    case SETTING_COOLING:
        return format_whole(settings->cooling ? 1 : 0, value, size);
    case SETTING_ROOMS:
        return format_whole((int)settings->room_count, value, size);
    case SETTING_TRIM_LIMIT:
        return settings->trim_limit_enabled ? format_number(settings->trim_limit_k, value, size)
                                            : copy_text(TRIM_LIMIT_NONE, value, size);
    case SETTING_HEAT_COOL:
        return format_whole(settings->heat_cool_heat_value, value, size);
    case SETTING_MAX_RETRIES:
        return format_whole(settings->max_retries, value, size);
    case SETTING_SMART_GRID:
        return format_whole(settings->smart_grid ? 1 : 0, value, size);
    default: {
        const double *number = number_field((ctl_settings *)settings, key);
        return number != NULL && format_number(*number, value, size);
    }
    }
}

/* ---- The interface ------------------------------------------------------------------------- */

void ctl_settings_defaults(ctl_settings *settings)
{
    ctl_config core;
    ctl_rooms_config rooms;
    ctl_router_config router;
    size_t i;

    ctl_config_defaults(&core);
    ctl_rooms_config_defaults(&rooms, 1);
    ctl_router_config_defaults(&router);

    settings->enabled = false;
    settings->cooling = false;
    settings->room_count = 0;
    for (i = 0; i < CTL_ROOMS_MAX; i++) {
        settings->room_topic[i][0] = '\0';
        settings->room_weight[i] = rooms.weight[i];
    }
    settings->room_timeout_s = rooms.room_timeout_s;
    settings->pv_topic[0] = '\0';
    settings->frame_log_topic[0] = '\0';
    settings->desired_indoor_c = core.desired_indoor_c;
    settings->transmission_w_per_k = core.transmission_w_per_k;
    settings->adjustment_w_per_k = core.adjustment_w_per_k;
    settings->hysteresis_wh = core.hysteresis_wh;
    settings->trim_limit_enabled = core.trim_limit_enabled;
    settings->trim_limit_k = core.trim_limit_k;
    settings->solar_gain_factor = core.solar_gain_factor;
    settings->internal_w = core.fallback_w[CTL_SIGNAL_P_INTERNAL];
    settings->heat_cool_heat_value = router.heat_cool_heat_value;
    settings->min_change_interval_s = router.min_change_interval_s;
    settings->retry_interval_s = router.retry_interval_s;
    settings->max_retries = router.max_retries;
    settings->smart_grid = router.smart_grid;
    settings->command_topic[0] = '\0';
}

ctl_setting_status ctl_settings_set(ctl_settings *settings, const char *name, const char *value)
{
    setting_key key = {SETTING_ENABLED, 0};
    ctl_settings copy;
    if (!find_name(settings, name, &key)) {
        return CTL_SETTING_UNKNOWN;
    }
    copy = *settings;
    if (value == NULL || !apply_value(&copy, key, value)) {
        return CTL_SETTING_BAD_VALUE;
    }
    if (!ctl_settings_valid(&copy)) {
        return CTL_SETTING_INVALID_SET;
    }
    *settings = copy;
    return CTL_SETTING_OK;
}

bool ctl_settings_get(const ctl_settings *settings, const char *name, char *value, size_t size)
{
    setting_key key = {SETTING_ENABLED, 0};
    if (value == NULL || size == 0 || !find_name(settings, name, &key)) {
        return false;
    }
    return write_value(settings, key, value, size);
}

static size_t listed_rooms(const ctl_settings *settings)
{
    return settings->room_count < CTL_ROOMS_MAX ? settings->room_count : CTL_ROOMS_MAX;
}

size_t ctl_settings_name_count(const ctl_settings *settings)
{
    return FIXED_NAME_COUNT + 2u * listed_rooms(settings);
}

bool ctl_settings_name_at(const ctl_settings *settings, size_t index, char *name, size_t size)
{
    const size_t room_names = 2u * listed_rooms(settings);
    if (name == NULL || size == 0 || index >= ctl_settings_name_count(settings)) {
        return false;
    }
    if (index < NAMES_BEFORE_ROOMS) {
        return copy_text(FIXED_NAMES[index].name, name, size);
    }
    index -= NAMES_BEFORE_ROOMS;
    if (index < room_names) {
        const char *suffix = index % 2u == 0u ? ROOM_TOPIC_SUFFIX : ROOM_WEIGHT_SUFFIX;
        return write_room_name(index / 2u, suffix, name, size);
    }
    return copy_text(FIXED_NAMES[NAMES_BEFORE_ROOMS + index - room_names].name, name, size);
}

bool ctl_settings_valid(const ctl_settings *settings)
{
    ctl_loop_config config;
    ctl_settings_to_loop(settings, &config);
    return ctl_loop_config_valid(&config);
}

void ctl_settings_to_loop(const ctl_settings *settings, ctl_loop_config *config)
{
    /* Without rooms the loop still needs one; it never receives a value, so the core waits for
     * its inputs rather than refusing to start. */
    const size_t count = settings->room_count == 0 ? 1 : settings->room_count;
    size_t i;

    ctl_loop_config_defaults(config, count);

    config->core.desired_indoor_c = settings->desired_indoor_c;
    config->core.transmission_w_per_k = settings->transmission_w_per_k;
    config->core.adjustment_w_per_k = settings->adjustment_w_per_k;
    config->core.hysteresis_wh = settings->hysteresis_wh;
    config->core.trim_limit_enabled = settings->trim_limit_enabled;
    config->core.trim_limit_k = settings->trim_limit_k;
    config->core.solar_gain_factor = settings->solar_gain_factor;
    config->core.fallback_w[CTL_SIGNAL_P_INTERNAL] = settings->internal_w;

    if (settings->room_count > 0) {
        for (i = 0; i < count && i < CTL_ROOMS_MAX; i++) {
            config->rooms.weight[i] = settings->room_weight[i];
        }
    }
    config->rooms.room_timeout_s = settings->room_timeout_s;

    config->router.min_change_interval_s = settings->min_change_interval_s;
    config->router.retry_interval_s = settings->retry_interval_s;
    config->router.max_retries = settings->max_retries;
    config->router.heat_cool_heat_value = settings->heat_cool_heat_value;
    config->router.smart_grid = settings->smart_grid;

    config->allowed =
        (uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE));
    if (settings->cooling) {
        config->allowed |= (uint8_t)CTL_DEMAND_BIT(CTL_DEMAND_COOLING);
    }
    config->assume_landed = false;
}
