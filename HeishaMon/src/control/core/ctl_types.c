#include "ctl_types.h"

static const char invalid_name[] = "INVALID";

/* Each table is indexed by the enum's value; the casts to int below keep a negative value,
 * which an enum may hold, from passing a check on an unsigned type. */
static const char *const demand_names[] = {"HEATING", "COOLING", "NO_DEMAND"};
static const char *const phase_names[] = {"UNINITIALIZED", "RUNNING", "HOLD", "FAULT"};
static const char *const reason_names[] = {
    "NORMAL",        "SEEDED",          "WAITING_FOR_INPUTS", "MASKED", "HELD_STALE_INPUT",
    "FAULT_TIMEOUT", "FAULT_RECOVERED", "INVALID_TIME"};
static const char *const signal_names[CTL_SIGNAL_COUNT] = {
    "T_INDOOR", "T_OUTDOOR", "P_PRODUCED", "P_SOLAR", "P_INTERNAL"};

#define NAME_COUNT(names) ((int)(sizeof(names) / sizeof((names)[0])))

static const char *name_at(const char *const *names, int count, int index)
{
    if (index < 0 || index >= count) {
        return invalid_name;
    }
    return names[index];
}

const char *ctl_demand_name(ctl_demand demand)
{
    return name_at(demand_names, NAME_COUNT(demand_names), (int)demand);
}

const char *ctl_phase_name(ctl_phase phase)
{
    return name_at(phase_names, NAME_COUNT(phase_names), (int)phase);
}

const char *ctl_reason_name(ctl_reason reason)
{
    return name_at(reason_names, NAME_COUNT(reason_names), (int)reason);
}

const char *ctl_signal_name(ctl_signal signal)
{
    return name_at(signal_names, NAME_COUNT(signal_names), (int)signal);
}

bool ctl_signal_is_required(ctl_signal signal)
{
    switch (signal) {
    case CTL_SIGNAL_T_INDOOR:
    case CTL_SIGNAL_T_OUTDOOR:
    case CTL_SIGNAL_P_PRODUCED:
        return true;
    default:
        return false;
    }
}
