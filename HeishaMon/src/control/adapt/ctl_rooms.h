/*
 * The room average of the adapters: the house has several room thermostats, the core one indoor
 * temperature. Each room's reading is offered as it arrives; the glue asks for the average on its
 * own clock, and that average becomes one indoor reading for the core. Only fresh rooms count,
 * and the average exists only while the fresh rooms carry more than half of the configured
 * weight, so that a few rooms left over from a broken source cannot stand in for the house.
 * Instants are seconds of the core's monotonic clock, handed in by the caller.
 */
#ifndef CTL_ROOMS_H
#define CTL_ROOMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTL_ROOMS_MAX 16

typedef struct {
    size_t count;                 /* rooms configured, 1 to CTL_ROOMS_MAX */
    double weight[CTL_ROOMS_MAX]; /* 0 to 1000; 0 = watched, not counted */
    double room_timeout_s;        /* a room older than this leaves the average */
} ctl_rooms_config;

typedef struct {
    bool has_value[CTL_ROOMS_MAX];
    double value_c[CTL_ROOMS_MAX];
    double t[CTL_ROOMS_MAX]; /* arrival instant of the room's last accepted value */
    bool has_timestamp[CTL_ROOMS_MAX];
    int64_t timestamp_ms[CTL_ROOMS_MAX]; /* 0 when the last accepted payload had none */
} ctl_rooms;

typedef enum {
    CTL_ROOM_ACCEPTED,
    CTL_ROOM_BAD_INDEX,
    CTL_ROOM_UNPARSED,
    CTL_ROOM_IMPLAUSIBLE,
    CTL_ROOM_REPLAYED
} ctl_room_status;

typedef struct {
    bool has_average; /* false: nothing to emit */
    double average_c;
    size_t fresh_count;
    double fresh_weight;
    double total_weight;
} ctl_rooms_average;

/* Sets count to the given count, every weight to 1.0 and the room timeout to 1800.0 s. Refuses
 * nothing; whether the count is usable is ctl_rooms_config_valid's to say. */
void ctl_rooms_config_defaults(ctl_rooms_config *config, size_t count);

/* Returns true when count is 1 to CTL_ROOMS_MAX, each of the first count weights is accepted by
 * CTL_BOUND_WEIGHT, their sum is above 0, and the room timeout is finite and above 0. */
bool ctl_rooms_config_valid(const ctl_rooms_config *config);

/* Leaves every room without a value and without a timestamp. */
void ctl_rooms_init(ctl_rooms *rooms);

/* Offers the length bytes of a room's payload, arrived at instant t, and returns
 * CTL_ROOM_ACCEPTED when the room took it. Refuses, in this order and leaving the rooms as they
 * were: an index not below count or CTL_ROOMS_MAX (CTL_ROOM_BAD_INDEX), a payload the parser
 * refuses (CTL_ROOM_UNPARSED), a value CTL_BOUND_TEMPERATURE refuses or a t that is not finite
 * (CTL_ROOM_IMPLAUSIBLE), and a timestamped payload whose timestamp does not exceed the one of
 * the room's last accepted payload (CTL_ROOM_REPLAYED). A payload without a timestamp is never a
 * replay. */
ctl_room_status ctl_rooms_offer(ctl_rooms *rooms, const ctl_rooms_config *config, size_t index,
                                const char *payload, size_t length, double t);

/* Returns true when the room has a value and its age, now minus its arrival instant, is not above
 * the room timeout; a negative age is fresh. Returns false for an index not below count or
 * CTL_ROOMS_MAX, a room without a value, and a now that is NaN. */
bool ctl_rooms_is_fresh(const ctl_rooms *rooms, const ctl_rooms_config *config, size_t index,
                        double now);

/* Fills *average with the total weight, the fresh rooms' count and weight, and, when the fresh
 * weight is more than half of the total, their weighted mean summed in room order. Otherwise, and
 * for a configuration ctl_rooms_config_valid refuses, has_average is false and average_c 0.0; an
 * invalid configuration also leaves the counts and weights at 0. */
void ctl_rooms_average_at(const ctl_rooms *rooms, const ctl_rooms_config *config, double now,
                          ctl_rooms_average *average);

#endif
