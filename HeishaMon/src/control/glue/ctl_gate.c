#include "ctl_gate.h"

#include <stdbool.h>

ctl_gate ctl_gate_state(bool enabled, bool listen_only, bool optional_pcb)
{
    if (!enabled) {
        return CTL_GATE_DISABLED;
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
    }
    return "invalid";
}
