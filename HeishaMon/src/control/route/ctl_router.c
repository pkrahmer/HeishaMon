#include "ctl_router.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define DEFAULT_MIN_CHANGE_INTERVAL_S 300.0
#define DEFAULT_RETRY_INTERVAL_S 120.0
#define DEFAULT_MAX_RETRIES 3
#define HEAT_COOL_UNSET (-1)
#define MAX_RETRIES_LIMIT 10

#define STATE_NONE 0
#define STATE_COOLING 1
#define STATE_HEATING 2
#define NOTHING_SENT (-1)

static const char THERMOSTAT_COMMAND[] = "SetExternalThermostat1State";
static const char HEAT_COOL_COMMAND[] = "SetHeatCoolMode";

void ctl_router_config_defaults(ctl_router_config *config)
{
    config->min_change_interval_s = DEFAULT_MIN_CHANGE_INTERVAL_S;
    config->retry_interval_s = DEFAULT_RETRY_INTERVAL_S;
    config->max_retries = DEFAULT_MAX_RETRIES;
    config->heat_cool_heat_value = HEAT_COOL_UNSET;
}

static bool interval_valid(double seconds)
{
    return isfinite(seconds) && seconds >= 0.0;
}

bool ctl_router_config_valid(const ctl_router_config *config)
{
    return interval_valid(config->min_change_interval_s)
        && interval_valid(config->retry_interval_s)
        && config->max_retries >= 0 && config->max_retries <= MAX_RETRIES_LIMIT
        && config->heat_cool_heat_value >= HEAT_COOL_UNSET && config->heat_cool_heat_value <= 1;
}

void ctl_router_init(ctl_router *router)
{
    router->wanted_state = STATE_NONE;
    router->sent_state = NOTHING_SENT;
    router->has_sent = false;
    router->sent_at = 0.0;
    router->changed_at = 0.0;
    router->retries = 0;
    router->status = CTL_ROUTER_IDLE;
}

int ctl_router_state_for(ctl_demand demand)
{
    switch (demand) {
    case CTL_DEMAND_HEATING:
        return STATE_HEATING;
    case CTL_DEMAND_COOLING:
        return STATE_COOLING;
    default:
        return STATE_NONE;
    }
}

/* The name is a literal of this file, shorter than the field; the value is one digit. A full
 * list takes nothing more, so that the array is bounded by its own size and not by the callers. */
const char *ctl_router_status_name(ctl_router_status status)
{
    switch (status) {
    case CTL_ROUTER_IDLE:
        return "IDLE";
    case CTL_ROUTER_WAITING:
        return "WAITING";
    case CTL_ROUTER_FAULT:
        return "FAULT";
    default:
        return "INVALID";
    }
}

static void add_command(ctl_commands *commands, const char *name, int digit)
{
    ctl_command *command;
    size_t i;
    if (commands->count >= CTL_ROUTER_COMMANDS_MAX) {
        return;
    }
    command = &commands->command[commands->count];
    for (i = 0; i + 1 < CTL_ROUTER_NAME_MAX && name[i] != '\0'; i++) {
        command->name[i] = name[i];
    }
    command->name[i] = '\0';
    command->value[0] = (char)('0' + digit);
    command->value[1] = '\0';
    commands->count++;
}

/* Heat/cool mode only rides along with a demand: no demand leaves the mode as the pump has it,
 * and an unset polarity sends no mode at all rather than a guess. */
static void write_state(ctl_commands *commands, const ctl_router_config *config, int state)
{
    const int heat = config->heat_cool_heat_value;
    add_command(commands, THERMOSTAT_COMMAND, state);
    if (heat == HEAT_COOL_UNSET) {
        return;
    }
    if (state == STATE_HEATING) {
        add_command(commands, HEAT_COOL_COMMAND, heat);
    } else if (state == STATE_COOLING) {
        add_command(commands, HEAT_COOL_COMMAND, 1 - heat);
    }
}

static bool has_landed(const ctl_router *router, bool state_known, int reported_state)
{
    return state_known && reported_state == router->sent_state;
}

static void send_new_state(ctl_router *router, const ctl_router_config *config, double now,
                           ctl_commands *commands)
{
    write_state(commands, config, router->wanted_state);
    router->sent_state = router->wanted_state;
    router->has_sent = true;
    router->sent_at = now;
    router->changed_at = now;
    router->retries = 0;
    router->status = CTL_ROUTER_WAITING;
}

/* Pacing counts from the last new state, not from a repeat, so that repeats of a dropped command
 * never hold back the next change of demand. */
static void consider_new_state(ctl_router *router, const ctl_router_config *config, double now,
                               ctl_commands *commands)
{
    if (!router->has_sent || now - router->changed_at >= config->min_change_interval_s) {
        send_new_state(router, config, now, commands);
    }
}

static void consider_repeat(ctl_router *router, const ctl_router_config *config, double now,
                            ctl_commands *commands)
{
    if (now - router->sent_at < config->retry_interval_s) {
        return;
    }
    if (router->retries < config->max_retries) {
        write_state(commands, config, router->sent_state);
        router->retries++;
        router->sent_at = now;
    } else {
        router->status = CTL_ROUTER_FAULT;
    }
}

void ctl_router_step(ctl_router *router, const ctl_router_config *config, ctl_demand demand,
                     bool state_known, int reported_state, double now, ctl_commands *commands)
{
    bool landed;
    commands->count = 0;
    if (!isfinite(now) || !ctl_router_config_valid(config)) {
        return;
    }
    router->wanted_state = ctl_router_state_for(demand);
    landed = has_landed(router, state_known, reported_state);
    if (landed && router->wanted_state == router->sent_state) {
        router->retries = 0;
        router->status = CTL_ROUTER_IDLE;
    }
    if (!router->has_sent || router->wanted_state != router->sent_state) {
        consider_new_state(router, config, now, commands);
    } else if (router->status == CTL_ROUTER_WAITING && !landed) {
        consider_repeat(router, config, now, commands);
    }
}
