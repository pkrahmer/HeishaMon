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
        if (settings->room_topic[i][0] != '\0' && strcmp(settings->room_topic[i], topic) == 0) {
            *room = i;
            return true;
        }
    }
    return false;
}

static bool is_pv(const ctl_settings *settings, const char *topic)
{
    return settings->pv_topic[0] != '\0' && strcmp(settings->pv_topic, topic) == 0;
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
    if (settings == NULL || base == NULL || topic == NULL || topic[0] == '\0') {
        return none;
    }
    if (is_room(settings, topic, &result.room)) {
        result.kind = CTL_TOPIC_INPUT;
        result.input = CTL_INPUT_ROOM;
    } else if (is_pv(settings, topic)) {
        result.kind = CTL_TOPIC_INPUT;
        result.input = CTL_INPUT_PV;
    } else if (is_setting(base, topic, result.name)) {
        result.kind = CTL_TOPIC_SETTING;
    }
    return result;
}

int ctl_dispatch_thermostat_state(uint8_t pcb_byte_6)
{
    return (pcb_byte_6 >> 2) & 3;
}
