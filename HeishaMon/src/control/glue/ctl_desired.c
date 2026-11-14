#include "ctl_desired.h"

#include "../adapt/ctl_payload.h"
#include "../loop/ctl_loop.h"
#include "ctl_settings.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void forget(ctl_desired *desired, const ctl_settings *settings)
{
    /* Both topics hold at most CTL_SETTINGS_TOPIC_MAX characters and their zero. */
    memcpy(desired->topic, settings->desired_topic, sizeof desired->topic);
    desired->held = false;
    desired->setpoint_c = 0.0;
}

void ctl_desired_init(ctl_desired *desired, const ctl_settings *settings)
{
    forget(desired, settings);
}

void ctl_desired_follow(ctl_desired *desired, const ctl_settings *settings)
{
    if (strcmp(desired->topic, settings->desired_topic) != 0) {
        forget(desired, settings);
    }
}

static bool in_range(double value)
{
    return isfinite(value) && value >= CTL_DESIRED_MIN_C && value <= CTL_DESIRED_MAX_C;
}

bool ctl_desired_offer(ctl_desired *desired, const char *payload, size_t length)
{
    ctl_payload parsed = {0.0, false, 0};
    /* A thermostat that sends a value it cannot mean, or nothing, has no setpoint to keep: the
     * setting takes over rather than an older setpoint the owner has since changed. */
    desired->held = payload != NULL && ctl_payload_parse(payload, length, &parsed) &&
                    in_range(parsed.value);
    desired->setpoint_c = desired->held ? parsed.value : 0.0;
    return desired->held;
}

ctl_desired_source ctl_desired_target(const ctl_desired *desired, const ctl_settings *settings,
                                      double *target_c)
{
    if (desired->held) {
        *target_c = desired->setpoint_c;
        return CTL_DESIRED_TOPIC;
    }
    *target_c = settings->desired_indoor_c;
    return CTL_DESIRED_SETTING;
}

void ctl_desired_apply(const ctl_desired *desired, const ctl_settings *settings,
                       ctl_loop_config *config)
{
    ctl_desired_target(desired, settings, &config->core.desired_indoor_c);
}

const char *ctl_desired_source_name(ctl_desired_source source)
{
    switch (source) {
    case CTL_DESIRED_SETTING:
        return "setting";
    case CTL_DESIRED_TOPIC:
        return "topic";
    }
    return "-";
}
