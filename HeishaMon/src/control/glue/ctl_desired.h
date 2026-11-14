/*
 * The target from a thermostat: the owner sets the house's target on a room thermostat, whose
 * setpoint arrives on the setting desired_topic. The last valid setpoint is held and is the
 * controller's target; without one, the setting desired_indoor_c is. A held setpoint never goes
 * stale, since a thermostat publishes its setpoint only when it changes, and it is never stored,
 * so that the setting keeps its own value. It is pure: it subscribes to nothing.
 */
#ifndef CTL_DESIRED_H
#define CTL_DESIRED_H

#include "../loop/ctl_loop.h"
#include "ctl_settings.h"

#include <stdbool.h>
#include <stddef.h>

#define CTL_DESIRED_MIN_C 16.0 /* the lowest setpoint taken, included */
#define CTL_DESIRED_MAX_C 26.0 /* the highest setpoint taken, included */

typedef enum {
    CTL_DESIRED_SETTING, /* the target is the setting desired_indoor_c */
    CTL_DESIRED_TOPIC    /* the target is the setpoint held from desired_topic */
} ctl_desired_source;

typedef struct {
    char topic[CTL_SETTINGS_TOPIC_MAX + 1]; /* the desired_topic the held setpoint belongs to */
    bool held;
    double setpoint_c;
} ctl_desired;

/* Holds no setpoint and belongs to the settings' desired_topic. Refuses nothing. */
void ctl_desired_init(ctl_desired *desired, const ctl_settings *settings);

/* Forgets the held setpoint when the settings' desired_topic is no longer the one it belongs to,
 * and belongs to the new one; keeps it otherwise. Refuses nothing. */
void ctl_desired_follow(ctl_desired *desired, const ctl_settings *settings);

/* Offers the length bytes of a payload from desired_topic, which need no terminating zero, and
 * returns true when it holds the payload's value as the setpoint. Refuses, returning false and
 * clearing the held setpoint, a payload ctl_payload_parse refuses (a NULL or empty one included)
 * and a value outside CTL_DESIRED_MIN_C to CTL_DESIRED_MAX_C. */
bool ctl_desired_offer(ctl_desired *desired, const char *payload, size_t length);

/* Writes the target to *target_c and returns where it comes from: the held setpoint (TOPIC), or
 * the settings' desired_indoor_c (SETTING) when none is held. Refuses nothing. */
ctl_desired_source ctl_desired_target(const ctl_desired *desired, const ctl_settings *settings,
                                      double *target_c);

/* Writes the target into the core's configuration, which ctl_settings_to_loop made from the same
 * settings. Refuses nothing. */
void ctl_desired_apply(const ctl_desired *desired, const ctl_settings *settings,
                       ctl_loop_config *config);

/* Returns "setting" or "topic"; "-" for a value that is no member. */
const char *ctl_desired_source_name(ctl_desired_source source);

#endif
