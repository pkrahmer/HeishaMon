/*
 * The signal table: the last reading of each of the five signals, held until the next reading
 * of the same signal, so that all five are defined at every instant and the net power between
 * two readings is a constant that integrates exactly. Resolving the table at one instant gives
 * the inputs of the energy balance, or nothing when a required signal is missing or stale.
 */
#ifndef CTL_SIGNALS_H
#define CTL_SIGNALS_H

#include <stdbool.h>
#include <stdint.h>

#include "ctl_config.h"
#include "ctl_types.h"

/* A set of signals is a bit mask; this is the bit of one signal. */
#define CTL_SIGNAL_BIT(signal) ((uint8_t)(1u << (signal)))

/* Indexed by signal; value and t mean nothing while observed is false. */
typedef struct {
    bool observed[CTL_SIGNAL_COUNT];
    double value[CTL_SIGNAL_COUNT];
    double t[CTL_SIGNAL_COUNT];
} ctl_signal_table;

typedef struct {
    double t_indoor_c;
    double t_outdoor_c;
    double p_produced_w;
    double p_solar_w;
    double p_internal_w;
    uint8_t substituted; /* CTL_SIGNAL_BIT of each optional signal that fell back */
} ctl_inputs;

/* Leaves no signal observed. Refuses nothing. */
void ctl_signals_init(ctl_signal_table *table);

/* Holds the value and its instant for the signal. Refuses nothing: the newest call wins,
 * even when its instant lies before the one it replaces. */
void ctl_signals_set(ctl_signal_table *table, ctl_signal signal, double value, double t);

/* Returns the seconds from the signal's reading to at, negative when at lies before it, and
 * INFINITY for a signal never observed. Refuses nothing. */
double ctl_signals_age(const ctl_signal_table *table, ctl_signal signal, double at);

/* Returns true when the signal was observed and its age at at is at most its max_age_s; an
 * age equal to the maximum is fresh. Returns false for a NaN at. Refuses nothing. */
bool ctl_signals_is_fresh(const ctl_signal_table *table, ctl_signal signal, double at,
                          const ctl_config *config);

/* Returns the bits of the required signals that are not fresh at at. Refuses nothing. */
uint8_t ctl_signals_unhealthy(const ctl_signal_table *table, double at, const ctl_config *config);

/* Returns the bits of the optional signals that are not fresh at at. Refuses nothing. */
uint8_t ctl_signals_stale_optional(const ctl_signal_table *table, double at,
                                   const ctl_config *config);

/* Returns the bits of every signal observed, fresh or not. Refuses nothing. */
uint8_t ctl_signals_observed(const ctl_signal_table *table);

/* Returns true and fills inputs with the held values at at, an optional signal that is not
 * fresh replaced by its fallback_w and named in substituted. Refuses, returning false and
 * leaving inputs untouched, when any required signal is not fresh at at, a NaN at included. */
bool ctl_signals_resolve(const ctl_signal_table *table, double at, const ctl_config *config,
                         ctl_inputs *inputs);

#endif
