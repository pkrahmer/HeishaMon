#include "ctl_dispatch.h"

#include "../adapt/ctl_rooms.h"
#include "../loop/ctl_loop.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SETTING_PATH "/control/set/"

/* Returns the rest of topic after prefix, or NULL when topic does not start with it. */
static const char *after_prefix(const char *topic, const char *prefix)
{
    size_t length = strlen(prefix);
    if (strncmp(topic, prefix, length) != 0) {
        return NULL;
    }
    return topic + length;
}

static bool is_room(const ctl_settings *settings, const char *topic, size_t *room)
{
    size_t i;
    for (i = 0; i < settings->room_count && i < CTL_ROOMS_MAX; i++) {
        if (strcmp(settings->room_topic[i], topic) == 0) {
            *room = i;
            return true;
        }
    }
    return false;
}

/* The inputs a topic setting other than a room's names, in the order they are checked. */
static bool is_named_input(const ctl_settings *settings, const char *topic, ctl_input *input)
{
    const struct {
        const char *setting;
        ctl_input input;
    } inputs[] = {
        {settings->pv_topic, CTL_INPUT_PV},
        {settings->outdoor_topic, CTL_INPUT_OUTDOOR},
        {settings->heat_topic, CTL_INPUT_HEAT},
        {settings->valve_topic, CTL_INPUT_VALVE},
        {settings->defrost_topic, CTL_INPUT_DEFROST},
    };
    size_t i;
    for (i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        if (strcmp(inputs[i].setting, topic) == 0) {
            *input = inputs[i].input;
            return true;
        }
    }
    return false;
}

/* Writes the name of a <base>/control/set/<name> topic, when the name is one the struct holds. */
static bool is_setting(const char *base, const char *topic, char *name)
{
    const char *rest = after_prefix(topic, base);
    size_t length;
    if (rest == NULL) {
        return false;
    }
    rest = after_prefix(rest, SETTING_PATH);
    if (rest == NULL) {
        return false;
    }
    length = strlen(rest);
    if (length == 0 || length >= CTL_SETTINGS_NAME_MAX || strchr(rest, '/') != NULL) {
        return false;
    }
    memcpy(name, rest, length + 1);
    return true;
}

ctl_topic ctl_dispatch_topic(const ctl_settings *settings, const char *base, const char *topic)
{
    static const ctl_topic none;
    ctl_topic result = none;
    /* Refusing the empty topic here is what keeps an empty topic setting from matching. */
    if (settings == NULL || base == NULL || topic == NULL || topic[0] == '\0') {
        return none;
    }
    if (is_room(settings, topic, &result.room)) {
        result.kind = CTL_TOPIC_INPUT;
        result.input = CTL_INPUT_ROOM;
    } else if (is_named_input(settings, topic, &result.input)) {
        result.kind = CTL_TOPIC_INPUT;
    } else if (strcmp(settings->frame_log_topic, topic) == 0) {
        result.kind = CTL_TOPIC_FRAME_LOG;
    } else if (is_setting(base, topic, result.name)) {
        result.kind = CTL_TOPIC_SETTING;
    }
    return result;
}

int ctl_dispatch_thermostat_state(uint8_t pcb_byte_6)
{
    return (pcb_byte_6 >> 2) & 3;
}

int ctl_dispatch_smart_grid_mode(uint8_t pcb_byte_6)
{
    return (pcb_byte_6 >> 4) & 3;
}
