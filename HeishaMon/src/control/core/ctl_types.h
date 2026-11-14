/*
 * The four enums every module of the control core speaks in: the demand it sends to the
 * heat pump, the phase of its lifecycle, the reason for a decision and the signals it
 * reads. Each has its name as text, the name of the Python reference's enum member, because
 * that is what a golden trace writes.
 */
#ifndef CTL_TYPES_H
#define CTL_TYPES_H

#include <stdbool.h>

typedef enum { CTL_DEMAND_HEATING, CTL_DEMAND_COOLING, CTL_DEMAND_NONE } ctl_demand;

typedef enum {
    CTL_PHASE_UNINITIALIZED,
    CTL_PHASE_RUNNING,
    CTL_PHASE_HOLD,
    CTL_PHASE_FAULT
} ctl_phase;

typedef enum {
    CTL_REASON_NORMAL,
    CTL_REASON_SEEDED,
    CTL_REASON_WAITING_FOR_INPUTS,
    CTL_REASON_MASKED,
    CTL_REASON_HELD_STALE_INPUT,
    CTL_REASON_FAULT_TIMEOUT,
    CTL_REASON_FAULT_RECOVERED,
    CTL_REASON_INVALID_TIME
} ctl_reason;

/* CTL_SIGNAL_COUNT is no signal: it sizes the arrays indexed by signal. */
typedef enum {
    CTL_SIGNAL_T_INDOOR,
    CTL_SIGNAL_T_OUTDOOR,
    CTL_SIGNAL_P_PRODUCED,
    CTL_SIGNAL_P_SOLAR,
    CTL_SIGNAL_P_INTERNAL,
    CTL_SIGNAL_COUNT
} ctl_signal;

/* Returns "HEATING", "COOLING" or "NO_DEMAND"; "INVALID" for a value that is no member. */
const char *ctl_demand_name(ctl_demand demand);

/* Returns "UNINITIALIZED", "RUNNING", "HOLD" or "FAULT"; "INVALID" for a value that is no
 * member. */
const char *ctl_phase_name(ctl_phase phase);

/* Returns the member's name, such as "WAITING_FOR_INPUTS"; "INVALID" for a value that is no
 * member. */
const char *ctl_reason_name(ctl_reason reason);

/* Returns the member's name, such as "T_INDOOR"; "INVALID" for CTL_SIGNAL_COUNT and for
 * every other value that is no member. */
const char *ctl_signal_name(ctl_signal signal);

/* Returns true for the signals without which the core can neither integrate nor decide:
 * T_INDOOR, T_OUTDOOR and P_PRODUCED. Returns false for the optional P_SOLAR and P_INTERNAL,
 * which have a fallback value, and for every value that is no member. */
bool ctl_signal_is_required(ctl_signal signal);

#endif
