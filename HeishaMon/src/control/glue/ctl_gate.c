#include "ctl_gate.h"

#include <stdbool.h>

ctl_gate ctl_gate_state(bool enabled, bool listen_only, bool optional_pcb, bool command_topic,
                        bool frame_log_topic)
{
    if (!enabled) {
        return CTL_GATE_DISABLED;
    }
    if (command_topic) {
        return frame_log_topic ? CTL_GATE_OPEN : CTL_GATE_NO_FRAME_LOG;
    }
    if (listen_only) {
        return CTL_GATE_LISTEN_ONLY;
    }
    if (!optional_pcb) {
        return CTL_GATE_NO_OPTIONAL_PCB;
    }
    return CTL_GATE_OPEN;
}

const char *ctl_gate_name(ctl_gate gate)
{
    switch (gate) {
    case CTL_GATE_OPEN:
        return "-";
    case CTL_GATE_DISABLED:
        return "disabled";
    case CTL_GATE_LISTEN_ONLY:
        return "listen_only";
    case CTL_GATE_NO_OPTIONAL_PCB:
        return "no_optional_pcb";
    case CTL_GATE_NO_FRAME_LOG:
        return "no_frame_log";
    }
    return "invalid";
}
