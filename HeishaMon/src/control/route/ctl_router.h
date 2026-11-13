/*
 * The router: it turns the core's demand into the commands of HeishaMon's optional PCB emulation,
 * the thermostat state (2 heat, 1 cool, 0 no demand) and, when its polarity is set, the heat/cool
 * mode; or, for a pump that ignores the thermostat input, the Smart Grid mode alone (0 for a
 * demand, 1 for none). Every write wears the pump's EEPROM, so it sends only when the wanted state changes and
 * never more often than a minimum interval; commands can be dropped on the way, so it watches the
 * state the pump reports and sends again, a bounded number of times, before it reports a fault.
 * It is pure: it returns the commands to send and sends nothing itself. Instants are seconds of
 * the caller's monotonic clock.
 */
#ifndef CTL_ROUTER_H
#define CTL_ROUTER_H

#include "../core/ctl_types.h"

#include <stdbool.h>
#include <stddef.h>

#define CTL_ROUTER_COMMANDS_MAX 2
#define CTL_ROUTER_NAME_MAX 32
#define CTL_ROUTER_VALUE_MAX 4

typedef struct {
    double min_change_interval_s; /* default 300.0 */
    double retry_interval_s;      /* default 120.0 */
    int max_retries;              /* default 3 */
    int heat_cool_heat_value;     /* -1 unset (default), else 0 or 1: SetHeatCoolMode's value for heat */
    bool smart_grid;              /* default false: send SetSmartGridMode instead of the thermostat */
} ctl_router_config;

typedef enum { CTL_ROUTER_IDLE, CTL_ROUTER_WAITING, CTL_ROUTER_FAULT } ctl_router_status;

typedef struct {
    /* The state the demand asks for: the thermostat state, 0, 1 or 2, or with smart_grid the
     * Smart Grid mode, 0 or 1; sent_state, the reported state and the commands' value are the
     * same kind. */
    int wanted_state;
    int sent_state;          /* -1 until the first send */
    bool has_sent;
    double sent_at;          /* instant of the last send, first or repeat */
    double changed_at;       /* instant of the last send of a new state */
    int retries;             /* repeats of the current state so far */
    ctl_router_status status;
} ctl_router;

typedef struct {
    char name[CTL_ROUTER_NAME_MAX];
    char value[CTL_ROUTER_VALUE_MAX];
} ctl_command;

typedef struct {
    size_t count;
    ctl_command command[CTL_ROUTER_COMMANDS_MAX];
} ctl_commands;

/* Writes the defaults: 300.0 s between changes, 120.0 s before a repeat, 3 repeats, no
 * heat/cool polarity (-1), so that SetHeatCoolMode is never sent until the owner sets it, and the
 * thermostat rather than Smart Grid mode. */
void ctl_router_config_defaults(ctl_router_config *config);

/* Returns true when both intervals are finite and 0 or greater, max_retries is from 0 to 10 and
 * heat_cool_heat_value is -1, 0 or 1, with either smart_grid; refuses every other
 * configuration. */
bool ctl_router_config_valid(const ctl_router_config *config);

/* Leaves the router wanting no demand, with nothing sent (sent_state -1), no retries and the
 * status IDLE, so that the first step sends the no-demand state at once. */
void ctl_router_init(ctl_router *router);

/* Forgets what the router sent and whether it gave up, but keeps the instant of its last send as
 * the start of the pacing interval: the next step sends the wanted state once, and not sooner than
 * min_change_interval_s after the last send. A router that never sent stays so and sends at once.
 * Refuses nothing. */
void ctl_router_reopen(ctl_router *router);

/* Returns the thermostat state for a demand: 2 for HEATING, 1 for COOLING, and 0 for NONE and
 * for any value outside the enum, so that an unknown demand never heats or cools. */
int ctl_router_state_for(ctl_demand demand);

/* Returns the name of status: "IDLE", "WAITING" or "FAULT", and "INVALID" for any value outside
 * the enum, so that a broken status still reads as one. */
const char *ctl_router_status_name(ctl_router_status status);

/* Advances the router to now and writes into *commands the commands to send, none, one or two:
 * with smart_grid only ever one, SetSmartGridMode. reported_state is the state the pump shows,
 * the thermostat state or with smart_grid the Smart Grid mode, and is read only when state_known
 * is true. Refuses, sending nothing and leaving the router as it was, a now that is not finite and
 * a configuration ctl_router_config_valid refuses. */
void ctl_router_step(ctl_router *router, const ctl_router_config *config, ctl_demand demand,
                     bool state_known, int reported_state, double now, ctl_commands *commands);

#endif
