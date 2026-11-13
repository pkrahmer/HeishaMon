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
    CTL_GATE_NO_OPTIONAL_PCB,
    CTL_GATE_NO_FRAME_LOG
} ctl_gate;

/* Returns the first of these that holds. When command_topic is false, the commands go to this
 * board's serial port: DISABLED when enabled is false, LISTEN_ONLY when listen_only is true,
 * NO_OPTIONAL_PCB when optional_pcb is false, otherwise OPEN. When command_topic is true, they go
 * to another board over MQTT, and only that board's log can confirm them, while this board's
 * serial port plays no part: DISABLED when enabled is false, NO_FRAME_LOG when frame_log_topic is
 * false, otherwise OPEN. Refuses nothing. */
ctl_gate ctl_gate_state(bool enabled, bool listen_only, bool optional_pcb, bool command_topic,
                        bool frame_log_topic);

/* Returns the gate's name: "-" for OPEN, so that an open gate reads as nothing blocked, and
 * "disabled", "listen_only", "no_optional_pcb" or "no_frame_log"; "invalid" for any value outside
 * the enum. */
const char *ctl_gate_name(ctl_gate gate);

#endif
