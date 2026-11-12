/*
 * The command gate: whether the router's commands may go to the pump, and if not, the first
 * reason why. The glue decides by it what it sends, whether the loop assumes that its commands
 * landed, and what the telemetry names as the reason commands are blocked.
 */
#ifndef CTL_GATE_H
#define CTL_GATE_H

#include <stdbool.h>

typedef enum {
    CTL_GATE_OPEN,
    CTL_GATE_DISABLED,
    CTL_GATE_LISTEN_ONLY,
    CTL_GATE_NO_OPTIONAL_PCB
} ctl_gate;

/* Returns the first of these that holds: DISABLED when enabled is false, LISTEN_ONLY when
 * listen_only is true, NO_OPTIONAL_PCB when optional_pcb is false, otherwise OPEN. Refuses
 * nothing. */
ctl_gate ctl_gate_state(bool enabled, bool listen_only, bool optional_pcb);

/* Returns the gate's name: "-" for OPEN, so that an open gate reads as nothing blocked, and
 * "disabled", "listen_only" or "no_optional_pcb"; "invalid" for any value outside the enum. */
const char *ctl_gate_name(ctl_gate gate);

#endif
