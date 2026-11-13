/*
 * The dispatch: which of the loop's inputs or which setting an MQTT topic is, by the settings,
 * and the thermostat state and Smart Grid mode the optional PCB frame carries. The firmware glue asks it for
 * every message, so that a topic that is not the loop's goes on to upstream untouched. It is pure:
 * it subscribes to nothing and reads no frame of its own.
 */
#ifndef CTL_DISPATCH_H
#define CTL_DISPATCH_H

#include "../loop/ctl_loop.h"
#include "ctl_settings.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CTL_TOPIC_NONE,
    CTL_TOPIC_INPUT,
    CTL_TOPIC_SETTING,
    CTL_TOPIC_FRAME_LOG /* a line of another HeishaMon's log */
} ctl_topic_kind;

typedef struct {
    ctl_topic_kind kind;
    ctl_input input;                  /* CTL_TOPIC_INPUT: the input */
    size_t room;                      /* CTL_TOPIC_INPUT with CTL_INPUT_ROOM: the room */
    char name[CTL_SETTINGS_NAME_MAX]; /* CTL_TOPIC_SETTING: the setting's name */
} ctl_topic;

/* Returns INPUT with CTL_INPUT_ROOM and the room's index for a topic equal to the topic of a
 * room below room_count, the first such room when several are equal; else INPUT with CTL_INPUT_PV
 * for a topic equal to a non-empty pv_topic; else FRAME_LOG for a topic equal to a non-empty
 * frame_log_topic;
 * SETTING with the name for <base>/control/set/<name>,
 * when the name has 1 to CTL_SETTINGS_NAME_MAX - 1 characters and no '/' (whether such a setting
 * exists is the setter's to say). Returns NONE for every other topic, an empty one included, and
 * for a NULL argument; an empty topic setting matches nothing, since an empty topic is NONE.
 * Every field the kind does not use is zero. */
ctl_topic ctl_dispatch_topic(const ctl_settings *settings, const char *base, const char *topic);

/* Returns the thermostat-1 state of byte 6 of the emulated optional PCB frame: its bits 2 and 3,
 * 0 to 3. Refuses nothing. */
int ctl_dispatch_thermostat_state(uint8_t pcb_byte_6);

/* Returns the Smart Grid mode of byte 6 of the optional PCB frame: its bits 4 and 5, 0 to 3, as
 * upstream's SetSmartGridMode writes them. Refuses nothing. */
int ctl_dispatch_smart_grid_mode(uint8_t pcb_byte_6);

#endif
